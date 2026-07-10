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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_COUNTING_MUTEX_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_COUNTING_MUTEX_H_

// darwin_x86_64 (or any Mac/IOS x64) is a problem, but also a dying breed
// Map CountingMutex to absl::Mutex if building for Apple (Linux)
#if defined(__APPLE__) || defined(__EMSCRIPTEN__)

#include "absl/synchronization/mutex.h"

namespace concurrent {

using CountingMutex = absl::Mutex;
using CountingMutexLock = absl::MutexLock;
using CountingMutexWriterLock = absl::WriterMutexLock;
using CountingMutexReaderLock = absl::ReaderMutexLock;
using ReleasableCountingMutexLock = absl::ReleasableMutexLock;
using ReleasableCountingMutexWriterLock = absl::ReleasableMutexLock;

}  // namespace concurrent

#else  // defined(__APPLE__) || defined(__EMSCRIPTEN__)

#include <stdint.h>

#include <atomic>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/tsan_mutex_interface.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/percpu.h"
#include "gloop/base/percpu_macros.h"

// Determine if we provide an RSEQ implementation.
// Currently we only support x86/64 and ARM on platforms supported by percpu.
#if PERCPU_USE_RSEQ_GOTO && !defined(DISABLE_FAST_COUNTING_MUTEX) && \
    (defined(__x86_64__) || defined(__aarch64__))
#define COUNTING_MUTEX_USE_RSEQ true
#else
#define COUNTING_MUTEX_USE_RSEQ false
#endif

namespace concurrent {

// CountingMutex is a Mutex implementation using 'percpu' counters for shared
// locks, reducing the CPU cost and contention for mutex use cases dominated by
// shared reader locks.
//
// CountingMutex is intended to be a snap-in replacement for absl::Mutex for use
// cases dominated by 'mostly read-only' data access. CountingMutex provides
// fast and efficient shared lock functions, i.e. ReaderLock / ReaderUnlock,
// which are implemented using restartable sequences, avoiding the cost and
// contention that come with atomic operation based locks.
//
// CountingMutex provides a subset of most of the absl::Mutex API. It provides
// all (regular) lock functions such as `lock`, `unlock`, `lock_shared`, etc, as
// well as the most common conditional locking and wait functions such as
// `LockWhen` and `Await`. Unsupported functions such as `try_lock` logic or
// debug logging are assumed to be outside of the use case for fast shared
// locks. However, if there is a good use case for such functions, they can be
// considered for implementation. Feel free to reach out to the owners if you
// have a compelling use case.
//
// CountingMutex has the same requirements and invariants as absl::Mutex to make
// sure the two can always be interchanged. See the various methods for details.
//
// See <link> for the design and background.
class ABSL_LOCKABLE CountingMutex {
 public:
  // Destroys this instance.
  // A CountingMutex instance must not be destroyed with pending locks.
  ~CountingMutex();

  // Obtains a shared lock on this `CountingMutex`.
  // `lock_shared()` will block in the presence of an active exclusive lock.
  // This function must not be called while the current thread holds an
  // exclusive or shared lock on this instance.
  void lock_shared() ABSL_SHARED_LOCK_FUNCTION();

  ABSL_DEPRECATE_AND_INLINE()
  void ReaderLock() ABSL_SHARED_LOCK_FUNCTION() { lock_shared(); }

  // Tries to obtain a shared lock on this `CountingMutex`.
  // If this mutex can be acquired without blocking, acquires this mutex
  // for shared access and returns `true`. Otherwise, returns `false`.
  //
  // This method can also spuriously return `false` absent the presence of a
  // writer lock in the presence of high contention: writer locks will evict
  // reader locks, which will require 'per-cpu' slots to be re-initialized on
  // the first subsequent reader lock being placed. The latter requires a brief
  // lock for stability, and a `try_lock_shared()` will return `false` in this
  // scenario if it contends with another thread resetting a CPU slot to avoid
  // any possible blocking operation.
  [[nodiscard]] bool try_lock_shared() ABSL_SHARED_TRYLOCK_FUNCTION(true);

  ABSL_DEPRECATE_AND_INLINE()
  [[nodiscard]] bool ReaderTryLock() ABSL_SHARED_TRYLOCK_FUNCTION(true) {
    return try_lock_shared();
  }

  // Releases a shared lock on this `CountingMutex`.
  // This function must match a corresponding previous call to `lock_shared`
  // by the current thread.
  void unlock_shared() ABSL_UNLOCK_FUNCTION();

  ABSL_DEPRECATE_AND_INLINE()
  void ReaderUnlock() ABSL_UNLOCK_FUNCTION() { unlock_shared(); }

  // Acquires an exclusive lock on this `CountingMutex`.
  // `lock()` will block in the presence of any shared or exclusive locks until
  // all concurrent locks are released, and an exclusive lock is obtained.  This
  // function must not be called while the current thread holds an exclusive or
  // shared lock on this instance.
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION();

  ABSL_DEPRECATE_AND_INLINE()
  void WriterLock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { lock(); }

  // Releases an exclusive lock on this `CountingMutex`.
  // This function must match a corresponding previous call to `lock`
  // by the current thread.
  void unlock() ABSL_UNLOCK_FUNCTION();

  ABSL_DEPRECATE_AND_INLINE()
  void WriterUnlock() ABSL_UNLOCK_FUNCTION() { unlock(); }

  // `Lock` is an alias for `lock` and provided for API compatibility.
  ABSL_DEPRECATE_AND_INLINE()
  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { lock(); }

  // `Unlock` is an alias for `unlock` and provided for API compatibility.
  ABSL_DEPRECATE_AND_INLINE()
  void Unlock() ABSL_UNLOCK_FUNCTION() { unlock(); }

  // Require that the mutex be held exclusively (write mode) by this thread.
  //
  // If the mutex is not currently held, this function may report an error
  // (typically by crashing with a diagnostic) or it may do nothing. This
  // function is intended only as a tool to assist debugging; it doesn't
  // guarantee correctness.
  void AssertHeld() const ABSL_ASSERT_EXCLUSIVE_LOCK();

  // Require that the mutex be held at least in shared mode (read mode) by this
  // thread.
  //
  // If the mutex is not currently held, this function may report an error
  // (typically by crashing with a diagnostic) or it may do nothing. This
  // function is intended only as a tool to assist debugging; it doesn't
  // guarantee correctness.
  void AssertReaderHeld() const ABSL_ASSERT_SHARED_LOCK();

  // Return immediately if this thread does not hold this `CountingMutex` in any
  // mode; otherwise, may report an error (typically by crashing with a
  // diagnostic), or may return immediately. This function is intended only as
  // a tool to assist debugging; it doesn't guarantee correctness.
  void AssertNotHeld() const;

  // Blocks until simultaneously both `cond` is `true` and this `Mutex` is
  // reacquired. If the condition is initially `true`, `Await()` will skip the
  // Unlock/Lock re-acquire step.
  //
  // `Await()` requires that this thread holds this `Mutex` in some mode.
  void Await(const absl::Condition& cond) ABSL_SHARED_LOCKS_REQUIRED(this) {
    if (!cond.Eval()) AwaitSlow(cond);
  }

  // CountingMutex::AwaitWithTimeout()
  // CountingMutex::AwaitWithDeadline()
  //
  // Unlocks this `CountingMutex` and blocks until simultaneously:
  //   - either `cond` is true or the {timeout has expired, deadline has passed}
  //     and
  //   - this `CountingMutex` can be reacquired,
  // then reacquire this `CountingMutex` in the same mode in which it was
  // previously held, returning `true` iff `cond` is `true` on return.
  //
  // If the condition is initially `true`, the implementation *may* skip the
  // release/re-acquire step and return immediately.
  //
  // Deadlines in the past are equivalent to an immediate deadline.
  // Negative timeouts are equivalent to a zero timeout.
  //
  // This method requires that this thread holds this `CountingMutex` in some
  // mode.
  bool AwaitWithTimeout(const absl::Condition& cond, absl::Duration timeout)
      ABSL_SHARED_LOCKS_REQUIRED(this) {
    return cond.Eval() || AwaitSlow(cond, timeout);
  }

  bool AwaitWithDeadline(const absl::Condition& cond, absl::Time deadline)
      ABSL_SHARED_LOCKS_REQUIRED(this) {
    return cond.Eval() || AwaitSlow(cond, deadline);
  }

  // CountingMutex::LockWhen()
  // CountingMutex::ReaderLockWhen()
  // CountingMutex::WriterLockWhen()
  //
  // Blocks until simultaneously both `cond` is `true` and this `Mutex` can
  // be acquired, then atomically acquires this `CountingMutex`. `LockWhen()` is
  // logically equivalent to `*Lock(); Await();` though they may have different
  // performance characteristics.
  void LockWhen(const absl::Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    lock();
    Await(cond);
  }

  void ReaderLockWhen(const absl::Condition& cond) ABSL_SHARED_LOCK_FUNCTION() {
    lock_shared();
    Await(cond);
  }

  void WriterLockWhen(const absl::Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    LockWhen(cond);
  }

 private:
#ifndef NDEBUG
  // ThreadContext is used to track per-thread lock state to enforce invariants
  // and facilitate 'AssertHeld/AssertNotHeld' debug assertions.
  class ThreadContext {
   public:
    ~ThreadContext();

    // Adds this mutex to the list of read locks.
    // Returns true on success, false if this mutex is already listed.
    bool AddMutex(const CountingMutex* mutex);

    // Removes this mutex from the list of read locks.
    // Returns true on success, false if this mutex is not listed.
    bool RemoveMutex(const CountingMutex* mutex);

    // Returns true if the provided mutex is in this context.
    bool HasReader(const CountingMutex* p) const;
  };
#endif  // !NDEBUG

  // Handle layout constants. The value for kRegionSize should be at least
  // ABSL_CACHELINE_SIZE, but we enforce this only in the unit test, as the
  // consequence is loss of efficiency, not worth breaking entire builds for
  // a specific (newly added) architecture.
  static constexpr int kRegionSize = 128;
  static constexpr int kRegionShift = 7;
  static constexpr int kPerRegion = kRegionSize / sizeof(uint32_t);

  // Handle instance
  struct Handle {
    std::atomic<uint32_t>* ptr = nullptr;
  };

  // Returns true if CountingMutex RSEQ code is enabled, and RSEQ is available.
  static bool IsFast();

  // Returns the current CPU id
  static int CpuId();

  // Returns the current CPU of the current thread.
  static int GetCurrentCpu();

  // Tries to obtain (`try_lock = true`) or obtains (`try_lock = false`)
  // the internal mutex for the purpose of initializing a per cpu reader
  // lock after a concurrent write lock evicted all slots.
  // Emits the required lock or try_lock TSAN signals depending on the
  // `try_lock` value. Returns true on success.
  bool MutexLockForReadLock(bool try_lock) ABSL_SHARED_TRYLOCK_FUNCTION(true);

  // Releases the internal mutex, emitting the required lock or try_lock
  // TSAN signals depending on the `try_lock` value.
  void MutexUnlockForReadLock(bool try_lock) ABSL_UNLOCK_FUNCTION();

  // Fallback function for when the fast path in Reader(Try)Lock fails.
  // Investigates the reason for RefFast to fail, and depending on the cause
  // initializes rseq and/or the cpu slot for the current thread, or calls into
  // ReaderLockSlow() if we know RefFast failed because rseq is not available.
  bool ReaderLockFallback(bool try_lock) ABSL_SHARED_TRYLOCK_FUNCTION(true);

  // Fallback function for when the fast path in ReaderUnlock fails.
  // Investigates the reason for RefFast to fail, and depending on the cause
  // calls the ReaderUnlockPending() slow path, or ReaderUnlockSlow() if we know
  // that RefFast failed because rseq is not available.
  void ReaderUnlockFallback();

  // Fallback functions using locked atomic operations if rseq is not available.
  bool ReaderLockSlow(bool try_lock) ABSL_SHARED_TRYLOCK_FUNCTION(true);
  void ReaderUnlockSlow();

  // Obtains the main mutex, actives the current CPU slot and increments its
  // counter. May block in the presence of write locks if `try_lock == false`.
  // If `try_lock == true`, then this function will try to obtain the main
  // mutex, and if that fails, return `false` indicating the try lock failed.
  bool ReaderLockInit(bool try_lock) ABSL_SHARED_TRYLOCK_FUNCTION(true);
  bool ReaderLockInitFast(bool try_lock) ABSL_SHARED_TRYLOCK_FUNCTION(true);
  bool ReaderLockInitSlow(bool try_lock) ABSL_SHARED_TRYLOCK_FUNCTION(true);

  // Decreases the global 'pending_` counter, and signals any waiters if zero.
  void ReaderUnlockPending();

  // Fences all CPUs potentially interacting on this instance, resets all CPU
  // counters to 0, and waits for all reader locks to be released.
  void WaitReaderLocks() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Increases the reader lock count for the current CPU slot by `delta`
  // Returns true on success, false if the current CPU slot is inactive.
  //
  // Note that the implementation loads (dereferences) `percpu_data_` inside the
  // critical section: write locks repoint `percpu_data_` and fence concurrent
  // readers, guaranteeing readers are either complete or restarted, and in the
  // latter case always observe the 'repointed' `percpu_data_` value forcing
  // these read locks into the slow path.
  template <int32_t delta>
  bool RefFast();

  // Returns a std::atomic<uint32_t> pointer into the slot for `cpu`.
  std::atomic<uint32_t>* GetPtr(int cpu);

  void AwaitSlow(const absl::Condition& cond) ABSL_SHARED_LOCKS_REQUIRED(this);
  bool AwaitSlow(const absl::Condition& cond, absl::Duration timeout)
      ABSL_SHARED_LOCKS_REQUIRED(this);
  bool AwaitSlow(const absl::Condition& cond, absl::Time deadline)
      ABSL_SHARED_LOCKS_REQUIRED(this);
  bool AwaitCommonSlow(const absl::Condition& cond, const absl::Time* deadline)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  // Allocates a handle allowing indexes in the half open range [-2, NumCPUs).
  static Handle AllocHandle();

  // Frees a previously allocated handle.
  static void FreeHandle(Handle handle);

  // Returns an initialized handle from the provided ptr.
  static Handle InitHandle(std::atomic<uint32_t>* ptr);

  // Handle for this mutex.
  Handle handle_ = AllocHandle();

  // Fast per cpu data pointer. May be temporarily repointed to `null_handle_`.
  std::atomic<std::atomic<uint32_t>*> percpu_data_{handle_.ptr};

  std::atomic<uint32_t> pending_{0};
  bool reader_locks_ ABSL_GUARDED_BY(this) = false;
  absl::Mutex mu_;

  // global handle used for repointing disabled percpu_data_ values.
  ABSL_CONST_INIT static Handle null_handle_;

#ifdef NDEBUG
  constexpr void own_locked_context(bool) {}
  constexpr void own_shared_context(bool) {}
  constexpr void add_shared_lock() {}
  constexpr void remove_shared_lock() {}
  constexpr void remove_shared_lock_if_not(bool) {}
#else
  const ThreadContext* locked_context() const {
    return locked_context_.load(std::memory_order_relaxed);
  }

  void own_locked_context(bool owned) {
    ThreadContext* context = owned ? &tls_context_ : nullptr;
    locked_context_.store(context, std::memory_order_relaxed);
  }

  void add_shared_lock() { tls_context_.AddMutex(this); }
  void remove_shared_lock() { tls_context_.RemoveMutex(this); }
  void remove_shared_lock_if_not(bool locked) {
    if (!locked) tls_context_.RemoveMutex(this);
  }

  // Thread local data tracking reader lock state.
  ABSL_CONST_INIT static thread_local ThreadContext tls_context_;

  // The thread owning an exclusive lock on this instance, or nullptr if this
  // lock is not exclusively held by any thread.
  std::atomic<const ThreadContext*> locked_context_{nullptr};
#endif
};

inline CountingMutex::~CountingMutex() { FreeHandle(handle_); }

inline bool CountingMutex::IsFast() {
  return COUNTING_MUTEX_USE_RSEQ && base::subtle::percpu::IsFast();
}

inline int CountingMutex::CpuId() {
  return COUNTING_MUTEX_USE_RSEQ ? base::subtle::percpu::RseqCpuId()
                                 : base::subtle::percpu::kCpuIdUnsupported;
}

inline int CountingMutex::GetCurrentCpu() {
#ifdef ABSL_HAVE_SCHED_GETCPU
  return sched_getcpu();
#else
  // TODO: pick TLS random CPU platforms not having getcpu?
  // Alternatively: we don't care about such platforms here.
  return 0;
#endif
}

inline std::atomic<uint32_t>* CountingMutex::GetPtr(int cpu) {
  return percpu_data_.load(std::memory_order_acquire) + cpu * kPerRegion;
}

template <int32_t delta>
bool CountingMutex::RefFast() {
#if COUNTING_MUTEX_USE_RSEQ
#if defined(__x86_64__)
  uint32_t value;
  std::atomic<uint32_t>*ptr, *percpu_data;
  asm volatile goto(
      PERCPU_RSEQ_PROLOGUE(CountingMutex_RefFast, ptr)

      // Start
      //   percpu_data = percpu_data_
      //   ptr = GetPointerAtomic(percpu_data, rseq_cpu)
      //   value = *ptr
      //   if (value & 1) == 0 goto slow_path
      //   value += delta
      //   *ptr = value
      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(ptr)
      "mov   (%[percpu_data_]), %[percpu_data]\n"
      "shl   %[shift], %[ptr]\n"
      "lea   (%[percpu_data], %[ptr]), %[ptr]\n"
      "mov   (%[ptr]), %[value]\n"
      "test  $1, %[value]\n"
      "jz    %[slow_path]\n"
      "add   %[delta], %[value]\n"
      "mov   %[value], (%[ptr])\n"
      // Commit
      "5:\n"

      : [ptr] "=&r"(ptr), [value] "=&r"(value), [percpu_data] "=&r"(percpu_data)
      : PERCPU_RSEQ_INPUTS, [shift] "n"(kRegionShift),
        [delta] "n"(delta), [percpu_data_] "r"(&percpu_data_)
      : PERCPU_RSEQ_CLOBBERS
      : slow_path);
  return true;
slow_path:
#elif defined(__aarch64__)
  uint32_t value;
  int64_t ptr, percpu_data;
  asm volatile goto(
      // CountingMutex_RefFast rseq meta and trampoline.
      PERCPU_RSEQ_PROLOGUE(CountingMutex_RefFast, ptr)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(ptr)
      "ldr %[percpu_data], [%[self], %c[percpu_data_]]\n"
      "lsl %[ptr], %[ptr], %c[kRegionShift]\n"
      "ldr %w[value], [%[percpu_data], %[ptr]]\n"
      "tbnz %w[value], 0, 6f\n"
      "b %[slow_path]\n"
      "6:\n"
      "add %w[value], %w[value], %w[delta]\n"
      "str %w[value], [%[percpu_data], %[ptr]]\n"
      "5:\n"
      : [ptr] "=&r"(ptr), [value] "=&r"(value), [percpu_data] "=&r"(percpu_data)
      : PERCPU_RSEQ_INPUTS, [self] "r"(this),
        [percpu_data_] "n"(offsetof(CountingMutex, percpu_data_)),
        [kRegionShift] "n"(kRegionShift), [delta] "r"(delta)
      : PERCPU_RSEQ_CLOBBERS
      : slow_path);
  return true;
slow_path:
#else  // defined(__aarch64__)
  static_assert(false, "Invalid platform inside FastRef()");
#endif
#endif  // COUNTING_MUTEX_USE_RSEQ
  return false;
}

inline bool CountingMutex::ReaderLockSlow(bool try_lock) {
  ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_try_read_lock);
  std::atomic<uint32_t>* ptr = GetPtr(GetCurrentCpu());
  if ((ptr->fetch_add(2, std::memory_order_acq_rel) & 1) == 0) {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock_failed, 0);
    return ReaderLockInitSlow(try_lock);
  } else {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock, 0);
    return true;
  }
}

inline void CountingMutex::ReaderUnlockSlow() {
  std::atomic<uint32_t>* ptr = GetPtr(GetCurrentCpu());
  if ((ptr->fetch_sub(2, std::memory_order_acq_rel) & 1) == 0) {
    ReaderUnlockPending();
  }
}

inline bool CountingMutex::ReaderLockFallback(bool try_lock) {
  // Take slow path if rseq is unavailable, else init thread and/or cpu slot.
  const int cpu_id = CpuId();
  if (cpu_id == base::subtle::percpu::kCpuIdUnsupported) {
    return ReaderLockSlow(try_lock);
  } else {
    return ReaderLockInit(try_lock);
  }
}

inline void CountingMutex::ReaderUnlockFallback() {
  // Take slow path if rseq is unavailable, else unlock through shared counter.
  const int cpu_id = CpuId();
  if (cpu_id == base::subtle::percpu::kCpuIdUnsupported) {
    ReaderUnlockSlow();
  } else {
    ReaderUnlockPending();
  }
}

inline void CountingMutex::lock_shared() ABSL_SHARED_LOCK_FUNCTION() {
  AssertNotHeld();
  // Add the shared lock to the context before getting the lock. Some paths
  // obtaining the reader lock will lock and unlock `mu_` and the unlock may
  // evaluate awaiting conditions that could call `AssertReaderHeld()`.
  // Once these slow paths obtain `mu_`, there is no exclusive lock, and we
  // are effectively in shared mode.
  add_shared_lock();

  ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_try_read_lock);
  if (ABSL_PREDICT_TRUE(RefFast<2>())) {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock, 0);
  } else {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock_failed, 0);
    ReaderLockFallback(false);
  }
}

inline void CountingMutex::unlock_shared() ABSL_UNLOCK_FUNCTION() {
  remove_shared_lock();
  ABSL_TSAN_MUTEX_PRE_UNLOCK(this, __tsan_mutex_read_lock);
  if (ABSL_PREDICT_FALSE(!RefFast<-2>())) {
    ReaderUnlockFallback();
  }
  ABSL_TSAN_MUTEX_POST_UNLOCK(this, __tsan_mutex_read_lock);
}

inline bool CountingMutex::try_lock_shared()
    ABSL_SHARED_TRYLOCK_FUNCTION(true) {
  // See 'lock_shared()` on adding the shared lock optimistically.
  add_shared_lock();

  ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_try_read_lock);
  if (ABSL_PREDICT_TRUE(RefFast<2>())) {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock, 0);
    return true;
  } else {
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_read_lock_failed, 0);
    bool locked = ReaderLockFallback(true);
    remove_shared_lock_if_not(locked);
    return locked;
  }
}

inline void CountingMutex::lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() {
  AssertNotHeld();
  mu_.lock();
  ABSL_TSAN_MUTEX_PRE_LOCK(this, 0);
  if (reader_locks_) {
    WaitReaderLocks();
    reader_locks_ = false;
  }
  own_locked_context(true);
  ABSL_TSAN_MUTEX_POST_LOCK(this, 0, 0);
}

inline void CountingMutex::unlock() ABSL_UNLOCK_FUNCTION() {
  ABSL_TSAN_MUTEX_PRE_UNLOCK(this, 0);
  // Allow readers to obtain shared slots from here on
  DCHECK(!reader_locks_);
  percpu_data_.store(handle_.ptr, std::memory_order_release);

  // Unlocking the mutex may execute a waiting condition, which gets executed
  // on this thread context, so downgrade to shared while the lock is in an
  // "undetermined, may execute conditions" state.
  own_locked_context(false);
  ABSL_TSAN_MUTEX_POST_UNLOCK(this, 0);
  add_shared_lock();
  mu_.unlock();
  remove_shared_lock();
}

class ABSL_SCOPED_LOCKABLE CountingMutexReaderLock {
 public:
  explicit CountingMutexReaderLock(
      CountingMutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_SHARED_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu.lock_shared();
  }

  ABSL_DEPRECATE_AND_INLINE()
  explicit CountingMutexReaderLock(CountingMutex* mu)
      ABSL_SHARED_LOCK_FUNCTION(mu)
      : CountingMutexReaderLock(*mu) {}

  CountingMutexReaderLock(const CountingMutexReaderLock&) = delete;
  CountingMutexReaderLock(CountingMutexReaderLock&&) = delete;
  CountingMutexReaderLock& operator=(const CountingMutexReaderLock&) = delete;
  CountingMutexReaderLock& operator=(CountingMutexReaderLock&&) = delete;

  ~CountingMutexReaderLock() ABSL_UNLOCK_FUNCTION() {
    this->mu_.unlock_shared();
  }

 private:
  CountingMutex& mu_;
};

class ABSL_SCOPED_LOCKABLE CountingMutexWriterLock {
 public:
  explicit CountingMutexWriterLock(
      CountingMutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu.lock();
  }

  ABSL_DEPRECATE_AND_INLINE()
  explicit CountingMutexWriterLock(CountingMutex* mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : CountingMutexWriterLock(*mu) {}

  CountingMutexWriterLock(const CountingMutexWriterLock&) = delete;
  CountingMutexWriterLock(CountingMutexWriterLock&&) = delete;
  CountingMutexWriterLock& operator=(const CountingMutexWriterLock&) = delete;
  CountingMutexWriterLock& operator=(CountingMutexWriterLock&&) = delete;

  ~CountingMutexWriterLock() ABSL_UNLOCK_FUNCTION() { this->mu_.unlock(); }

 private:
  CountingMutex& mu_;
};

class ABSL_SCOPED_LOCKABLE ReleasableCountingMutexWriterLock {
 public:
  explicit ReleasableCountingMutexWriterLock(
      CountingMutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(&mu) {
    mu.lock();
  }

  ABSL_DEPRECATE_AND_INLINE()
  explicit ReleasableCountingMutexWriterLock(CountingMutex* mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : ReleasableCountingMutexWriterLock(*mu) {}

  ReleasableCountingMutexWriterLock(const ReleasableCountingMutexWriterLock&) =
      delete;
  ReleasableCountingMutexWriterLock(ReleasableCountingMutexWriterLock&&) =
      delete;
  ReleasableCountingMutexWriterLock& operator=(
      const ReleasableCountingMutexWriterLock&) = delete;
  ReleasableCountingMutexWriterLock& operator=(
      ReleasableCountingMutexWriterLock&&) = delete;

  ~ReleasableCountingMutexWriterLock() ABSL_UNLOCK_FUNCTION() {
    if (this->mu_ != nullptr) {
      this->mu_->unlock();
    }
  }

  void Release() ABSL_UNLOCK_FUNCTION();

 private:
  CountingMutex* mu_;
};

// CountingMutexLockMaybe is API compatible with absl::MutexLockMaybe
class ABSL_SCOPED_LOCKABLE CountingMutexLockMaybe {
 public:
  explicit CountingMutexLockMaybe(CountingMutex* absl_nullable mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    if (mu_ != nullptr) {
      mu_->lock();
    }
  }

  CountingMutexLockMaybe(CountingMutex* absl_nullable mu,
                         const absl::Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    if (mu_ != nullptr) {
      mu_->LockWhen(cond);
    }
  }

  ~CountingMutexLockMaybe() ABSL_UNLOCK_FUNCTION() {
    if (mu_ != nullptr) {
      mu_->unlock();
    }
  }

 private:
  CountingMutex* absl_nullable const mu_;
  CountingMutexLockMaybe(const CountingMutexLockMaybe&) = delete;
  CountingMutexLockMaybe(CountingMutexLockMaybe&&) = delete;
  CountingMutexLockMaybe& operator=(const CountingMutexLockMaybe&) = delete;
  CountingMutexLockMaybe& operator=(CountingMutexLockMaybe&&) = delete;
};

inline void CountingMutex::AssertHeld() const {
#ifndef NDEBUG
  if (locked_context() != &tls_context_) {
    ABSL_RAW_LOG(FATAL,
                 "thread should hold an exclusive lock on CountingMutex %p",
                 this);
  }
#endif
}

inline void CountingMutex::AssertNotHeld() const {
#ifndef NDEBUG
  if (locked_context() == &tls_context_ || tls_context_.HasReader(this)) {
    ABSL_RAW_LOG(FATAL, "thread should not hold a lock on CountingMutex %p",
                 this);
  }
#endif
}

inline void CountingMutex::AssertReaderHeld() const {
#ifndef NDEBUG
  if (locked_context() != &tls_context_ && !tls_context_.HasReader(this)) {
    ABSL_RAW_LOG(FATAL, "thread should hold a lock on CountingMutex %p", this);
  }
#endif
}

using CountingMutexLock = CountingMutexWriterLock;
using ReleasableCountingMutexLock = ReleasableCountingMutexWriterLock;

}  // namespace concurrent

#endif  // defined(__APPLE__) || defined(__EMSCRIPTEN__)

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_COUNTING_MUTEX_H_
