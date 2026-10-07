// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Removing the following header is prohibited as it can introduce undefined
// behavior.
// clang-format off
#include "gloop/enforce_gloop_support.h"
// clang-format on

#include "gloop/concurrent/percpu/counting_mutex.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/time/clock.h"

// darwin_x86_64 (or any Mac/IOS x64) is a problem, but also a dying breed
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)

#include <sched.h>
#include <syscall.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <type_traits>

#include "absl/base/internal/sysinfo.h"
#include "absl/base/internal/tsan_mutex_interface.h"
#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/futex.h"
#include "gloop/base/percpu.h"
#include "gloop/base/percpu_macros.h"
#include "gloop/base/scheduling/domain.h"

namespace concurrent {

ABSL_CONST_INIT CountingMutex::Handle CountingMutex::null_handle_{nullptr};

namespace {

using FreeList = std::deque<std::atomic<uint16_t>*>;

// Mutex guarding all operations on the handle freelist.
ABSL_CONST_INIT absl::Mutex freelist_mutex(absl::kConstInit);

// Returns a reference to the global free list, which is never destroyed.
std::deque<std::atomic<uint16_t>*>& freelist() {
  static absl::NoDestructor<std::deque<std::atomic<uint16_t>*>> list;
  return *list;
}

}  // namespace

#ifndef NDEBUG

ABSL_CONST_INIT thread_local CountingMutex::ThreadContext
    CountingMutex::tls_context_;

namespace {

// ThreadContextBlock forms a linked list of actively held reader locks on
// CountingMutex instances. These should be rare, as threads should rarely be
// holding more than 1 lock at a time.
struct ThreadContextBlock {
  ThreadContextBlock* next;
  const CountingMutex* mutexes[3];
};

// `ThreadContextData` holds per thread context data required for tracking
// active reader locks. `ThreadContextData` MUST be trivially destructible:
// CountingMutex is called from both thread and process exit handlers, which
// unfortunately can run after thread local data has been torn down, including
// the TLS objects of the thread running the exit handler. This in turn upsets
// MSAN (and possible other sanitizers) as they instrument any such non trivial
// class and declare the instance dead and void if the dtor has been invoked
// (which is reasonable), and we get uninitialized data failures under
// sanitizers.
//
// We circumvent this by making `ThreadContextData` trivially destructible, and
// having the destructor of ThreadContexta invoke to cleanup and tear down of
// `ThreadContextData` leaving it in a valid, initialized state.
//
// This should always end well as:
// - atexit() is called on the thread calling exit()
// - atexit() logic invokes all TLS dtors before invoking exit handlers.
// - trivial TLS data of a thread is never deallocated before:
//   1) all thread local dtors have run
//   2) all atexit handlers have run
ABSL_CONST_INIT thread_local struct ThreadContextData {
  const CountingMutex* mutex;
  ThreadContextBlock* blocks;
} tls_data = {nullptr, nullptr};
static_assert(std::is_trivial<ThreadContextData>::value, "");

}  // namespace

CountingMutex::ThreadContext::~ThreadContext() {
  for (auto* block = tls_data.blocks; block != nullptr;) {
    auto* next = block->next;
    delete block;
    block = next;
  }
  tls_data = {nullptr, nullptr};
}

bool CountingMutex::ThreadContext::AddMutex(const CountingMutex* mutex) {
  // Find current presence and empty slot.
  const CountingMutex** slot = nullptr;
  if (ABSL_PREDICT_FALSE(tls_data.mutex == mutex)) return false;
  for (auto* block = tls_data.blocks; block; block = block->next) {
    for (auto& rmutex : block->mutexes) {
      if (rmutex == mutex) return false;
      if (rmutex == nullptr) slot = &rmutex;
    }
  }
  // Add to list
  if (ABSL_PREDICT_TRUE(tls_data.mutex == nullptr)) {
    tls_data.mutex = mutex;
  } else if (slot) {
    *slot = mutex;
  } else {
    tls_data.blocks = new ThreadContextBlock{tls_data.blocks, {mutex}};
  }
  return true;
}

bool CountingMutex::ThreadContext::RemoveMutex(const CountingMutex* mutex) {
  if (ABSL_PREDICT_TRUE(tls_data.mutex == mutex)) {
    tls_data.mutex = nullptr;
    return true;
  }
  for (auto* block = tls_data.blocks; block; block = block->next) {
    for (auto& rmutex : block->mutexes) {
      if (rmutex == mutex) {
        rmutex = nullptr;
        return true;
      }
    }
  }
  return false;
}

bool CountingMutex::ThreadContext::HasReader(const CountingMutex* p) const {
  if (tls_data.mutex == p) return true;
  for (auto* block = tls_data.blocks; block; block = block->next) {
    for (auto* mutex : block->mutexes) {
      if (mutex == p) return true;
    }
  }
  return false;
}

#endif  // !NDEBUG

// CPU slots:
// ----------
// In all comments and documentation we refer to the uint16_t counter value
// for each CPU in the percpu Handler as a 'cpu slot' or 'cpu slot value'.
//
// Per-CPU uint16_t slots and 32-bit `pending_` overflow handling:
// ---------------------------------------------------------------
// Each 128-byte CPU region is partitioned into 64 `std::atomic<uint16_t>`
// handle slots, halving per-instance memory overhead while preserving the
// 128-byte CPU stride (`cpu << 7`). Because `pending_` must remain `uint32_t`
// for 32-bit `base::Futex` operations and unbounded global reader counts,
// `uint16_t` slots must NEVER wrap modulo 2^16: a 16-bit wrap on one CPU slot
// would not cancel out against a 32-bit wrap or another CPU's slot when summed
// in 32-bit arithmetic by `WaitReaderLocks()`.
//
// Instead, active CPU slots are strictly bounded to `{1, 3, 5, ..., 0xFFFF}`
// (representing 0 to 32,767 local readers):
//   - On lock (`RefFast<2>` / `ReaderLockSlow`): if an active slot is saturated
//     at `0xFFFF` (65,534 counts = 32,767 readers), the fast path falls back to
//     `ReaderLockInitFast` / `ReaderLockInitSlow`. Under `mu_`, the slot is
//     reset from `0xFFFF` to `3` (active with 1 local reader) and `0xFFFE`
//     (65,534) counts are flushed into the 32-bit `pending_` counter.
//   - On unlock (`RefFast<-2>` / `ReaderUnlockSlow`): if an active slot is `1`
//     (0 local readers, e.g. due to cross-CPU thread migration or a prior
//     saturation flush), the fast path falls back to `ReaderUnlockPending()`,
//     which subtracts 2 directly from `pending_`.
//
// Active vs Inactive slots:
// -------------------------
// A cpu slot is active if its value is odd, inactive if its value is 0 (even).
// All cpu slots are zero initialized. A slot can only change from active to
// inactive or vice versa while holding mutex `mu_`. Lock counts are always
// added or subtracted as multiples of 2 without changing the active bit or
// wrapping modulo 2^16.
//
// Total lock count and `pending_` values.
// ---------------------------------------
// ReaderLockSlow() will always have initialized or added a count of 2 to some
// cpu slot (or flushed saturation to `pending_`) upon return: it blocks in the
// presence of competing writer locks, and synchronizes with other readers that
// may race / compete with each other activating or flushing a CPU slot.
// However, ReaderUnlockSlow() is not allowed to block, nor is it allowed to
// 'activate' a CPU slot as it may execute concurrently with a writer awaiting
// reader locks to be released. For this purpose, if ReaderUnlockSlow() runs
// into an inactive (`0`) or empty-active (`1`) slot, it will instead decrement
// the shared `pending_` counter. The 'logical' total pending lock count is thus
// defined as:
//   `Σ (slot[0..NumCPU> - 1) WHERE (value & 1) != 0 + pending_`
//
// Writer locks
// ------------
// Simplified, a writer lock first obtains an exclusive lock on `mu_`,
// guaranteeing that no slots can be activated. It then collects the cumulative
// count of all active slots, and adds it to `pending_`, and resets the slot
// value to zero / inactive.
// At this point, `pending_` holds the number of outstanding shared locks, and
// any ReaderUnlock call is forced into the slow path, decreasing the
// `pending_` value. If `pending_` is non zero, the writer enters a Futex wait.
// Any reader that decreases `pending_` to a zero value signals the Futex.
//
// Implementation details for SLOW (non RSEQ) Lock / Unlock logic.
// ---------------------------------------------------------------
// Both ReaderLockSlow() and ReaderUnlockSlow() use a compare_exchange_weak loop
// on the current CPU's `std::atomic<uint16_t>` slot so that inactive (`0`),
// saturated (`0xFFFF`), or empty-active (`1` on unlock) slots are never
// modified in place.
//
// ReaderLockSlow: if the slot is active and `< 0xFFFF`, increments it by 2 via
// CAS. Otherwise (`(val & 1) == 0` or `val == 0xFFFF`), acquires `mu_` in
// ReaderLockInitSlow: if the slot is still inactive, stores `3`; if saturated
// at `0xFFFF`, CAS-resets it to `3` and flushes `0xFFFE` to `pending_`; else
// CAS-increments it by 2.
//
// ReaderUnlockSlow: if the slot is active and `>= 3`, decrements it by 2 via
// CAS. Otherwise (`(val & 1) == 0` or `val == 1`), decreases the shared
// `pending_` counter by 2, signaling any waiting writer if `pending_` drops to
// zero.
//
// Implementation details for FAST (RSEQ) Lock / Unlock logic.
// -----------------------------------------------------------
// The RSEQ critical section uses unlocked 16-bit load/store sequences while
// enforcing the same `{1, 3, ..., 0xFFFF}` bounds. Logically:
//
//   rseq {
//     std::atomic<uint16_t>* ptr = GetPtr(percpu_data, rseq_cpu);
//     uint16_t value = *ptr;
//     if ((value & 1) == 0) goto slow_path;
//     if (value + delta overflows/underflows uint16_t) goto slow_path;
//     *ptr = value + delta;
//   }
//
// Using CPU fences, we can make sure that the rseq section is either completed
// or restarted before we start collecting and resetting all cpu slots.
// We use an additional trick inside the RSEQ logic: we 'repoint' the
// `percpu_data_` to a static inactive handle, `null_handle_`. As the
// `percpu_data_` is loaded inside the critical section (which we make sure is
// complete or restarted), doing so directly disables any change of any CPU
// counter.
//
// In the slow path (`ReaderLockInitFast`), we use rseq under `mu_` to safely
// activate an inactive slot (`0 -> 3`), increment an active slot (`v -> v + 2`)
// or reset a saturated slot (`0xFFFF -> 3`) and flush `0xFFFE` to `pending_`.
//
// The use case for `reader_locks_`
// --------------------------------
// Our use case is 'mostly read-only data', but we want to be robust against bad
// / diabolical uses, and have the mutex behave relatively well in the presence
// of such cases. `reader_locks_` addresses the case where the lock is
// temporarily or permanently dominated by exclusive locks. `reader_locks_` is
// very cheap to maintain, and allows us to directly recognize the condition 'no
// shared lock occurred since the last exclusive lock`, and bypass any discovery
// and updates of active slots. Consequently, a CountingMutex dominated by
// exclusive locks has performance parity with `absl::Mutex`.

CountingMutex::Handle CountingMutex::InitHandle(std::atomic<uint16_t>* ptr) {
  const int num_cpus = absl::base_internal::NumCPUs();
  for (int i = 0; i < num_cpus + 2; ++i) {
    ptr[i * kPerRegion].store(0, std::memory_order_relaxed);
  }
  return Handle{ptr + 2 * kPerRegion};
}

CountingMutex::Handle CountingMutex::AllocHandle() {
  auto& list = freelist();
  {
    // Fast path: we have a free-listed handle: minimize critical section.
    absl::ReleasableMutexLock lock(freelist_mutex);
    if (ABSL_PREDICT_TRUE(!list.empty())) {
      std::atomic<uint16_t>* ptr = list.back();
      list.pop_back();
      lock.Release();
      return InitHandle(ptr);
    }
  }

  // Allocate space for NumCPUs + 2. The 2 extra entries are to allow for
  // directly addressing rseq -1 and -2 values for `uninitialized` and
  // 'not registered' indexes. This allows a single branch fast path.
  const int num_cpus = absl::base_internal::NumCPUs();
  const size_t n = kPerRegion * num_cpus + 2 * kPerRegion;
  auto* ptr = new std::atomic<uint16_t>[n];
  auto* const ptr_end = ptr + kPerRegion;
  Handle handle = InitHandle(ptr++);

  // Check if null_handle_ is already set, and move remaining onto free-list.
  absl::MutexLock lock(freelist_mutex);
  if (null_handle_.ptr == nullptr) {
    null_handle_ = InitHandle(ptr++);
  }
  while (ptr != ptr_end) {
    list.push_back(ptr++);
  }
  return handle;
}

void CountingMutex::FreeHandle(Handle handle) {
  assert(handle.ptr != nullptr);
  absl::MutexLock lock(freelist_mutex);
  freelist().push_back(handle.ptr - 2 * kPerRegion);
}

void CountingMutex::WaitReaderLocks() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  uint32_t pending = 0;
  std::atomic<uint16_t>* ptr = handle_.ptr;
  percpu_data_.store(null_handle_.ptr, std::memory_order_release);
  if (ABSL_PREDICT_TRUE(IsFast())) {
    for (int cpu = 0, n = absl::base_internal::NumCPUs(); cpu < n; ++cpu) {
      if ((ptr->load(std::memory_order_acquire) & 1) != 0) {
        // Make sure pending reader lock RSEQs are completed or restarted.
        // TODO: ideally upstream fences will at some point support
        // cpu set specific fences, reducing syscalls and internal overhead.
        base::subtle::percpu::FenceCpu(cpu);

        // Increase `pending`, removing the (always on) last bit.
        uint16_t value = ptr->load(std::memory_order_relaxed);
        ptr->store(0, std::memory_order_release);
        DCHECK(value & 1);
        pending += value - 1;
      }
      ptr += kPerRegion;
    }
  } else {
    for (int cpu = 0, n = absl::base_internal::NumCPUs(); cpu < n; ++cpu) {
      if ((ptr->load(std::memory_order_acquire) & 1) != 0) {
        // Increase `pending`, removing the (always on) last bit.
        // Note that we must use exchange(), as ReaderUnlock() calls can execute
        // concurrently, so we need to make sure those are ordered correctly.
        uint16_t value = ptr->exchange(0, std::memory_order_acq_rel);
        DCHECK(value & 1);
        pending += value - 1;
      }
      ptr += kPerRegion;
    }
  }

  pending += pending_.fetch_add(pending, std::memory_order_acq_rel);
  while (pending != 0) {
    ABSL_TSAN_MUTEX_PRE_DIVERT(this, 0);
    base::scheduling::Domain::StartPotentiallyBlockingRegion();
    int res = base::Futex::Futex::Wait(
        reinterpret_cast<std::atomic<int32_t>*>(&pending_), pending);
    base::scheduling::Domain::FinishPotentiallyBlockingRegion();
    ABSL_TSAN_MUTEX_POST_DIVERT(this, 0);
    DCHECK(res == 0 || res == -EAGAIN || res == -EINTR);
    pending = pending_.load(std::memory_order_acquire);
  }
}

void CountingMutex::MutexUnlockForReadLock(bool try_lock)
    ABSL_UNLOCK_FUNCTION() {
  if (try_lock) {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock, 0);
  } else {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_read_lock, 0);
  }
  mu_.unlock();
}

bool CountingMutex::MutexLockForReadLock(bool try_lock) {
  if (try_lock) {
    ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_try_read_lock);
    if (!mu_.try_lock()) {
      ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock_failed, 0);
      return false;
    }
  } else {
    mu_.lock();
    ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_read_lock);
  }
  return true;
}

bool CountingMutex::ReaderLockInitSlow(bool try_lock) {
  if (!MutexLockForReadLock(try_lock)) return false;
  std::atomic<uint16_t>* ptr = GetPtr(base::subtle::percpu::GetCurrentCpu());
  uint16_t val = ptr->load(std::memory_order_acquire);
  if ((val & 1) == 0) {
    ptr->store(3, std::memory_order_release);
  } else {
    while (!ptr->compare_exchange_weak(val, val == 0xFFFF ? 3 : val + 2,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
    }
    if (val == 0xFFFF) {
      pending_.fetch_add(0xFFFE, std::memory_order_acq_rel);
    }
  }
  reader_locks_ = true;
  MutexUnlockForReadLock(try_lock);
  return true;
}

bool CountingMutex::ReaderLockInitFast(bool try_lock) {
#if COUNTING_MUTEX_USE_RSEQ
  if (!MutexLockForReadLock(try_lock)) return false;
#if defined(__x86_64__)
  uint32_t value, overflow;
  std::atomic<uint16_t>* ptr;
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(CountingMutex_InitFast, ptr)

      // Start
      //   ptr = GetPointerAtomic(handle_, cpu)
      //   value = *ptr
      //   if (value == 0xFFFF) {
      //     *ptr = 3
      //     overflow = 1
      //   } else {
      //     *ptr = (value | 1) + 2
      //     overflow = 0
      //   }
      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(ptr)
      "shl    %[shift], %[ptr]\n"
      "lea    (%[handle_], %[ptr]), %[ptr]\n"
      "movzwl (%[ptr]), %[value]\n"
      "or     $1, %[value]\n"
      "add    $2, %[value]\n"
      "mov    %[value], %[overflow]\n"
      "shr    $16, %[overflow]\n"
      "lea    (%q[value], %q[overflow], 2), %[value]\n"
      "movw   %w[value], (%[ptr])\n"

      // Commit
      "5:\n"

      : [ptr] "=&r"(ptr), [value] "=&r"(value), [overflow] "=&r"(overflow)
      : PERCPU_RSEQ_INPUTS,
        [shift] "n"(kRegionShift),
        [handle_] "r"(handle_.ptr)
      : PERCPU_RSEQ_CLOBBERS, "memory");
  if (ABSL_PREDICT_FALSE(overflow)) {
    pending_.fetch_add(0xFFFE, std::memory_order_acq_rel);
  }
#elif defined(__aarch64__)
  uint32_t value, overflow;
  int64_t ptr, percpu_data;
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(CountingMutex_InitFast, ptr)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(ptr)
      "ldr  %[percpu_data], [%[self], %c[handle_]]\n"
      "lsl  %[ptr], %[ptr], %c[kRegionShift]\n"
      "ldrh %w[value], [%[percpu_data], %[ptr]]\n"
      "orr  %w[value], %w[value], #1\n"
      "add  %w[value], %w[value], #2\n"
      "lsr  %w[overflow], %w[value], #16\n"
      "add  %w[value], %w[value], %w[overflow], lsl #1\n"
      "strh %w[value], [%[percpu_data], %[ptr]]\n"
      "5:\n"
      : [ptr] "=&r"(ptr), [value] "=&r"(value), [overflow] "=&r"(overflow),
        [percpu_data] "=&r"(percpu_data)
      : PERCPU_RSEQ_INPUTS, [self] "r"(this),
        [handle_] "n"(offsetof(CountingMutex, handle_)),
        [kRegionShift] "n"(kRegionShift)
      : PERCPU_RSEQ_CLOBBERS, "memory");
  if (ABSL_PREDICT_FALSE(overflow)) {
    pending_.fetch_add(0xFFFE, std::memory_order_acq_rel);
  }
#else  // defined(__aarch64__)
  static_assert(false, "Invalid platform inside ReaderLockInitFast()");
#endif
  reader_locks_ = true;
  MutexUnlockForReadLock(try_lock);
#endif  // COUNTING_MUTEX_USE_RSEQ
  return true;
}

bool CountingMutex::ReaderLockInit(bool try_lock)
    ABSL_SHARED_TRYLOCK_FUNCTION(true) {
  if (IsFast()) {
    return ReaderLockInitFast(try_lock);
  } else {
    return ReaderLockInitSlow(try_lock);
  }
}

void CountingMutex::ReaderUnlockPending() {
  if (pending_.fetch_sub(2, std::memory_order_acq_rel) == 2) {
    ABSL_TSAN_MUTEX_PRE_SIGNAL(this, 0);
    base::Futex::Wake(reinterpret_cast<std::atomic<int32_t>*>(&pending_),
                      INT_MAX);
    ABSL_TSAN_MUTEX_POST_SIGNAL(this, 0);
  }
}

void CountingMutex::AwaitSlow(const absl::Condition& cond)
    ABSL_SHARED_LOCKS_REQUIRED(this) {
  bool result = AwaitCommonSlow(cond, nullptr);
  DCHECK(result);
}

bool CountingMutex::AwaitSlow(const absl::Condition& cond,
                              absl::Duration timeout)
    ABSL_SHARED_LOCKS_REQUIRED(this) {
  return AwaitSlow(cond, absl::Now() + timeout);
}

bool CountingMutex::AwaitSlow(const absl::Condition& cond, absl::Time deadline)
    ABSL_SHARED_LOCKS_REQUIRED(this) {
  return AwaitCommonSlow(cond, &deadline);
}

bool CountingMutex::AwaitCommonSlow(const absl::Condition& cond,
                                    const absl::Time* deadline)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // `reader_locks_ = true` indicates ~some thread~ is holding a reader lock.
  // As we require 'some' lock to be held, the absence of a a reader lock
  // definitively tells us we are holding a writer lock.
  bool writer_lock = !reader_locks_;

  // Use naive logic. We could optimize this somewhat, but the condition
  // should be expected to be true most times, and reader evictions are
  // expensive anyway, and readers must be evicted for condition to change.
  bool should_continue = true;
  while (should_continue) {
    writer_lock ? unlock() : unlock_shared();

    // `cond` will be executed with `mu_` being held, but without readers
    // being evicted, which is equivalent to a shared lock, and callers may
    // want to or need to call `AssertReaderHeld()`
    add_shared_lock();

    // Await
    if (deadline != nullptr) {
      should_continue &= mu_.LockWhenWithDeadline(cond, *deadline);
    } else {
      mu_.LockWhen(cond);
    }
    mu_.unlock();

    // Remove shared lock state, and relock
    remove_shared_lock();
    writer_lock ? lock() : lock_shared();

    if (cond.Eval()) return true;
  }

  return false;
}

void ReleasableCountingMutexWriterLock::Release() {
  ABSL_RAW_CHECK(
      this->mu_ != nullptr,
      "ReleasableCountingMutexLock::Release may only be called once");
  this->mu_->unlock();
  this->mu_ = nullptr;
}

}  // namespace concurrent

#endif  // !__APPLE__ && !__EMSCRIPTEN__
