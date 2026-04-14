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

using FreeList = std::deque<std::atomic<uint32_t>*>;

// Mutex guarding all operations on the handle freelist.
ABSL_CONST_INIT absl::Mutex freelist_mutex(absl::kConstInit);

// Returns a reference to the global free list, which is never destroyed.
std::deque<std::atomic<uint32_t>*>& freelist() {
  static absl::NoDestructor<std::deque<std::atomic<uint32_t>*>> list;
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
// In all comments and documentation we refer to the uint32_t counter value
// for each CPU in the percpu Handler as a 'cpu slot' or 'cpu slot value'.
//
// Recasting percpu handles to uint32_t values:
// --------------------------------------------
// We internally cast / map the percpu handle's std::atomic<int64_t>* to an
// std::atomic<uint32_t>*. We require the value to be unsigned, as we allow all
// counters to overflow. The number of outstanding locks is always less than an
// int32 value, however, threads could diabolically migrate to some CPU A when
// obtaining a lock. and migrate to CPU B when releasing a lock, causing the one
// counter to continuously increase and the other to continuously decrease. With
// defined overflow, this is not an issue, as we only care about the cumulative
// value of all slots, which will then always be (over or underflow to) the
// correct value.
//
// Active vs Inactive slots:
// -------------------------
// A cpu slot is active if its value is odd, inactive if its value is even. All
// cpu slots are zero initialized. A slot can only change from active to
// inactive or vice versa while holding mutex `mu_`. Lock counts are always
// added or subtracted as multiples of 2. I.e., any add or sub operation never
// changes the active state of the slot.
// The value of an 'inactive slot' has no meaning: we allow the code to
// optimistically increment or decrement the count of a CPU slot assuming it's
// active. Ignoring the value of inactive slots simplifies the logic  as we
// don't need to care about 'rolling back' a change on an inactive slot.
//
// Total lock count and `pending_` values.
// ---------------------------------------
// ReaderLockSlow() will always have initialized or added a count of 2 to some
// cpu slot upon return: it blocks in the presence of competing writer locks,
// and synchronizes with other readers that may race / compete with each other
// activating a CPU slot.
// However, ReaderUnlockSlow() is not allowed to block, nor is it allowed to
// 'activate' a CPU slot as it may execute concurrently with a writer awaiting
// reader locks to be released. For this purpose, if ReaderUnlockSlow() runs
// into an inactive slot, it will instead decrement the shared `pending_`
// counter. The 'logical' total pending lock count is thus defined as:
//   `Σ slot[0..NumCPU> WHERE value & 1 <> 0 + pending_`
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
// Both ReaderLockSlow() and ReaderUnlockSlow() optimistically add or subtract a
// delta of 2 from the current cpu's slot. A non conditional fetch_add/fetch_sub
// is faster than a load/CAS alternative, especially in highly concurrent use
// cases on high core machines. Both functions check the result of the
// fetch_add/fetch_sub to confirm the slot to be active.
//
// ReaderLockSlow: if the slot is found inactive after the fetch_add(), the
// `mu_` mutex is exclusively locked. As multiple readers could be competing on
// the same CPU slot absent RSEQ critical sections and fences, the code checks
// if the slot is active again after obtaining the exclusive lock. If the slot
// is still inactive, it sets it to a value of 3, representing a count of 1 and
// active state (2 + 1). It is possible that another reader competed with the
// current thread, and already activated the slot, in which case the code simply
// adds a lock count to it, i.e.: fetch_add(2).
//
// ReaderUnlockSlow: if the slot is found inactive after the fetch_sub(), we
// simply decrease the shared `pending_` counter by 2, optionally signaling a
// potentially waiting writer if we caused the `pending_` value to drop to zero.
//
// Implementation details for FAST (RSEQ) Lock / Unlock logic.
// -----------------------------------------------------------
// The RSEQ based code is similar to the slow code: the same invariants on
// active and inactive slots apply. The RSEQ critical section allows us to use
// normal unlocked load/store sequences. Instead of optimistically decreasing
// the value, we load the current value, check the active state (odd), and if
// active, store value + 2. Logically:
//
//   rseq {
//     std::atomic<uint32_t>* ptr = GetPtr(percpu_data, rseq_cpu);
//     uint32_t value = *ptr;
//     if value == 0 goto slow_path
//     value += delta
//     *ptr = value + delta;
//   }
//
// Using CPU fences, we can make sure that the rseq section is either completed
// or restarted before we start collecting and resetting all cpu slots.
// We use an additional trick inside the RSEQ logic: we 'repoint' the
// `percpu_data_` to a static inactive handle, `null_handle_`. As the
// `percpu_data_` is loaded inside the critical section (which we make sure is
// complete or restarted), doing so directly disables any change of any CPU
// counter, and we have a single (predicted) branch inside the rseq.
//
// In the slow path, we use rseq to safely initialize the CPU slot: the `mu_`
// locks synchronizes against any writer lock, the rseq makes sure we can
// naively initialize the slot, as we don't have any optimistic updates
// competing on the same CPU.
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

CountingMutex::Handle CountingMutex::InitHandle(std::atomic<uint32_t>* ptr) {
  const int num_cpus = base::subtle::percpu::NumCPUs();

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
      std::atomic<uint32_t>* ptr = list.back();
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
  auto* ptr = new std::atomic<uint32_t>[n];
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
  std::atomic<uint32_t>* ptr = handle_.ptr;
  percpu_data_.store(null_handle_.ptr, std::memory_order_release);
  if (ABSL_PREDICT_TRUE(IsFast())) {
    for (int cpu = 0, n = absl::base_internal::NumCPUs(); cpu < n; ++cpu) {
      if ((ptr->load(std::memory_order_acquire) & 1) != 0) {
        // Make sure pending reader lock RSEQs are completed or restarted.
        // TODO: ideally upstream fences will at some point support
        // cpu set specific fences, reducing syscalls and internal overhead.
        base::subtle::percpu::FenceCpu(cpu);

        // Increase `pending`, removing the (always on) last bit.
        uint32_t value = ptr->load(std::memory_order_relaxed);
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
        uint32_t value = ptr->exchange(0, std::memory_order_acq_rel);
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
  std::atomic<uint32_t>* ptr = GetPtr(base::subtle::percpu::GetCurrentCpu());
  if (ptr->load(std::memory_order_acquire) & 1) {
    ptr->fetch_add(2, std::memory_order_acq_rel);
  } else {
    ptr->store(3, std::memory_order_release);
  }
  reader_locks_ = true;
  MutexUnlockForReadLock(try_lock);
  return true;
}

bool CountingMutex::ReaderLockInitFast(bool try_lock) {
#if COUNTING_MUTEX_USE_RSEQ
  if (!MutexLockForReadLock(try_lock)) return false;
#if defined(__x86_64__)
  uint32_t value;
  std::atomic<uint64_t>* ptr;
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(CountingMutex_InitFast, ptr)

      // Start
      //   ptr = GetPointerAtomic(handle_, cpu)
      //   value = *ptr
      //   value |= 1
      //   value += 2
      //   *ptr = value
      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(ptr)
      "shl   %[shift], %[ptr]\n"
      "lea   (%[handle_], %[ptr]), %[ptr]\n"
      "mov   (%[ptr]), %[value]\n"
      "or    $1, %[value]\n"
      "add   $2, %[value]\n"
      "mov   %[value], (%[ptr])\n"

      // Commit
      "5:\n"

      : [ptr] "=&r"(ptr), [value] "=&r"(value)
      : PERCPU_RSEQ_INPUTS,
        [shift] "n"(kRegionShift),
        [handle_] "r"(handle_)
      : PERCPU_RSEQ_CLOBBERS);
#elif defined(__aarch64__)
  uint32_t value;
  int64_t ptr, percpu_data;
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(CountingMutex_RefFast, ptr)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(ptr)
      "ldr %[percpu_data], [%[self], %c[handle_]]\n"
      "lsl %[ptr], %[ptr], %c[kRegionShift]\n"
      "ldr %w[value], [%[percpu_data], %[ptr]]\n"
      "orr %w[value], %w[value], 1\n"
      "add %w[value], %w[value], 2\n"
      "str %w[value], [%[percpu_data], %[ptr]]\n"
      "5:\n"
      : [ptr] "=&r"(ptr), [value] "=&r"(value), [percpu_data] "=&r"(percpu_data)
      : PERCPU_RSEQ_INPUTS, [self] "r"(this),
        [handle_] "n"(offsetof(CountingMutex, handle_)),
        [kRegionShift] "n"(kRegionShift)
      : PERCPU_RSEQ_CLOBBERS);
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
