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

#ifndef THIRD_PARTY_GLOOP_BASE_PERCPU_TYPES_H_
#define THIRD_PARTY_GLOOP_BASE_PERCPU_TYPES_H_

#include <stdint.h>  // for __WORDSIZE

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/low_level_scheduling.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "gloop/base/percpu.h"
#include "gloop/base/scheduling/scheduling_mode.h"

#ifdef ABSL_HAVE_SCHED_YIELD
#include <sched.h>
#endif

namespace base {
namespace subtle {
namespace percpu {

// Fast per-cpu spinlocks.  Unlike a regular spinlock multiple threads may hold
// this primitive provided they are executing on separate CPUs.  This allows
// easy serialization of per_cpu objects, e.g.:
//   MyObject objects[NR_CPUS];
//   int cpu = percpu_lock.Lock();
//   < now safe to manipulate objects[cpu] >
//
// Per-cpu locks are extremely fast, approximately 10x as fast as a regular
// SpinLock in the uncontended case when support for acceleration is present.

class ABSL_LOCKABLE PerCpuSpinLock {
 public:
  PerCpuSpinLock() : percpu_lock_(AllocHandle()) {}
  PerCpuSpinLock(const PerCpuSpinLock&) = delete;
  PerCpuSpinLock& operator=(const PerCpuSpinLock&) = delete;
  ~PerCpuSpinLock() { FreeHandle(percpu_lock_); }

  // Acquires the lock.  Returns the CPU on which it was acquired.
  //   e.g. int cpu = percpu_lock.Lock();
  //        <perform actions synchronized for cpu >
  //        percpu_lock.Unlock(cpu);
  [[nodiscard]] inline int Lock() ABSL_SHARED_LOCK_FUNCTION() {
    return LockHelper(
        [this](int64_t lock_value) { return TryLockImpl(lock_value); }, 1);
  }

  // Acquires the lock on <cpu> (whether or not we're running there.)
  // Safe against threads using Lock(), but considerably slower; use rarely
  // (for workstealing or the like).
  // REQUIRES: unlock with UnlockOn().
  void LockOn(int cpu) ABSL_SHARED_LOCK_FUNCTION();
  // REQUIRES: <cpu> was locked with LockOn().
  void UnlockOn(int cpu) ABSL_UNLOCK_FUNCTION();

  // Acquires the lock if available, returning true iff so.
  // *locked_cpu will be set to the locked CPU or -1 if none.
  [[nodiscard]] bool TryLock(int* locked_cpu)
      ABSL_SHARED_TRYLOCK_FUNCTION(true) {
    int64_t lock_value = int64_t{1} << kLockerShift;
    *locked_cpu = TryLockImpl(lock_value);
    return *locked_cpu >= 0;
  }

  // Releases lock[cpu], must be held by the calling thread.
  inline void Unlock(const int cpu) ABSL_UNLOCK_FUNCTION() {
    // Load the old value of the word, which we'll need below.
    std::atomic<int64_t>& word = *GetPointerAtomic(percpu_lock_, cpu);

    // Store an "unlocked" value into the word, with appropriate
    // synchronization.
    UnlockWord(cpu, word);
  }

 private:
  // Our representation:
  //
  // Per-CPU, we have a single atomic word. Iff zero, the per-cpu lock is not
  // held. Otherwise:
  //
  // Bits 63..32 for 64 bit systems, and bits 31-1 for 32 bit systems, contain
  // the locker's identity. For locks acquired through Lock() or TryLock(),
  // this is 1. For locks acquired through LockOn() this is a value guaranteed
  // to be unique to the lock holder. See the LockOn() implementation for
  // details.
  //
  // Bit 0 contains the result of a call to
  // absl::base_internal::SchedulingGuard::DisableRescheduling(), if one was
  // made, or 0 otherwise. For cooperative locks, this is always zero.
  //
  // For 64 bit systems, bits 31-1 are reserved for future use.
  //
  // The locker_id should be 1, in the case of locks acquired through Lock() or
  // TryLock(), or the TID in the case of locks acquired through LockOn.
  // try_lock attempts to update the lock word iff it is currently zero, and
  // return the locked CPU iff successful.
  static constexpr int64_t kDisabledScheduling = 1;

  // We have plenty of space available on 64-bits, leave room for more flags.
  // On 32-bits, bits are more sparse, only leave space for what we absolutely
  // need.
  static_assert(sizeof(void*) == 8 || sizeof(void*) == 4,
                "unsupported pointer size");
  static constexpr int kLockerShift = sizeof(void*) == 8 ? 32 : 1;

  // Attempts to acquire `percpu_lock_[current cpu]` using `lock_value`. If
  // successful, returns the locked CPU; otherwise, returns -1.
  ABSL_ATTRIBUTE_HOT int TryLockImpl(int64_t lock_value) {
    if (ABSL_PREDICT_TRUE(percpu::IsFastNoInit())) {
      return AnnotateAcquiredCpuForTsan(
          RseqFunction_PerCpuTryLock(RseqAbi(), percpu_lock_, lock_value));
    } else {
      // We wrap this in a lambda so that we can move it out-of-line, since we
      // expect this path to be very cold.
      return [=, this]() ABSL_ATTRIBUTE_COLD {
        // This may force percpu initialization; the fast path in TryLockImpl()
        // uses IsFastNoInit().
        if (percpu::IsFast()) {
          return AnnotateAcquiredCpuForTsan(
              RseqFunction_PerCpuTryLock(RseqAbi(), percpu_lock_, lock_value));
        } else {
          const int cpu = GetCurrentCpu();
          int64_t previous = 0;
          if (GetPointerAtomic(percpu_lock_, cpu)
                  ->compare_exchange_strong(previous, lock_value,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
            return cpu;
          }
          return -1;
        }
      }();
    }
  }

  // Adapts a `TryLocker try_lock`, which models std::function<int(int64_t)>,
  // to a `Lock` method: invoke it until it returns a non-negative CPU, with
  // appropriate backoff.
  template <typename TryLocker>
  ABSL_ATTRIBUTE_HOT int LockHelper(TryLocker try_lock, int64_t locker_id)
      ABSL_SHARED_LOCK_FUNCTION() {
    ABSL_RAW_DCHECK(
        ((static_cast<uint64_t>(locker_id) << kLockerShift) >> kLockerShift) ==
            static_cast<uint64_t>(locker_id),
        "locker_id unexpectedly contains high bits which would be lost by the "
        "shift below. A basic assumption about the range of values used for "
        "locker_id has been violated.");
    // If, at some future point, we wanted to do PerCpuSpinLock contention
    // profiling, we'd want to change the lock word representation and modify
    // this method to measure cycle delays.
    int64_t lock_value = locker_id << kLockerShift;
    const auto result = try_lock(lock_value);
    if (ABSL_PREDICT_TRUE(result >= 0)) {
      return result;
    }
    // We wrap this in a lambda so that we can move it out-of-line, since we
    // expect this path to be very cold.
    return [=]() ABSL_ATTRIBUTE_COLD {
#ifdef ABSL_HAVE_SCHED_YIELD
      sched_yield();
#endif
      int iteration = 1;
      int result;
      do {
        Delay(iteration++);
        result = try_lock(lock_value);
      } while (result < 0);
      return result;
    }();
  }

  // Delays for iteration number `iteration`.
  static void Delay(int iteration);

  // Iff running under tsan, this annotates that we successfully acquired the
  // lock for CPU `cpu`; otherwise, has no effect. Returns `cpu`.
  int AnnotateAcquiredCpuForTsan(int cpu) {
    if (cpu >= 0) {
      TSANAcquire(GetPointerAtomic(percpu_lock_, cpu));
    }
    return cpu;
  }

  // Write zero to *word, which is assumed to be the word for the given CPU
  // number,  in such a way that all writes made before the unlock will be
  // visible to the next thread that takes the lock for the CPU.
  void UnlockWord(const int cpu, std::atomic<int64_t>& word) {
    // C++'s release/acquire semantics should be sufficient for ensuring writes
    // before the unlock are visible to the next thread that takes the lock. On
    // x86 and ARM in particular we can get away with just doing a release
    // store. x86 doesn't need any special logic to make this work, and for
    // ARM we do a load-acquire in RseqFunction_PerCpuTryLock to pair with
    // this where supported.
    //
    // This is sufficient to prevent the compiler from re-ordering pre-Unlock
    // writes after this point (see note below). It will automatically give us
    // the equivalent of C++'s release/acquire semantics when paired with the
    // instruction loading the lock word in the next call to
    // `RseqFunction_PerCpuTryLock`.
#if defined(__x86_64__) || (defined(__aarch64__) && defined(__ARM_FEATURE_RCPC))
    word.store(0, std::memory_order_release);
    return;
#endif

    // Prevent the compiler from sinking any of the pre-Unlock code to below
    // this point, or hoisting any of the code below above it.
    //
    // In particular, the load of the CPU number below must not appear in
    // assembly program order until after the code that comes before the call to
    // this method in C++ program order.
    CompilerBarrier();

    // Fast and common path: if we're in fast mode and on the target CPU
    // already, it is enough to simply write the lock word, because the current
    // CPU is guaranteed to have already observed the writes that came before
    // the call to this method (and therefore the next software thread that
    // locks on this CPU will see them). This holds even if we are rescheduled
    // before or after the call to RseqCpuId.
    const int current_cpu = RseqCpuId();

    if (ABSL_PREDICT_TRUE(current_cpu == cpu)) {
      word.store(0, std::memory_order_relaxed);
      return;
    }

    // Otherwise, we must ensure that the target CPU has observed the locked
    // section writes by the time it next acquires the lock. In fast mode, the
    // `FenceCpu` call here forms a release/acquire pair with the next call to
    // `RseqFunction_PerCpuTryLock` for `current_cpu`.
    if (ABSL_PREDICT_TRUE(current_cpu >= kCpuIdInitialized)) {
      FenceCpu(cpu);
      word.store(0, std::memory_order_relaxed);
      return;
    }

    // Otherwise we must be in slow mode. Release to the lock word, pairing with
    // the acquire-CAS in the `TryLockImpl` slow path.
    ABSL_RAW_CHECK(!IsFast(), "");
    word.store(0, std::memory_order_release);
  }

  Handle percpu_lock_;
};

class ABSL_SCOPED_LOCKABLE PerCpuSpinLockHolder {
 public:
  inline explicit PerCpuSpinLockHolder(
      PerCpuSpinLock& l ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_SHARED_LOCK_FUNCTION(l)
      : lock_(l) {
    cpu_ = lock_.Lock();
  }

  inline explicit PerCpuSpinLockHolder(PerCpuSpinLock* l)
      ABSL_SHARED_LOCK_FUNCTION(*l)
      : PerCpuSpinLockHolder(*l) {}

  PerCpuSpinLockHolder(const PerCpuSpinLockHolder&) = delete;
  PerCpuSpinLockHolder& operator=(const PerCpuSpinLockHolder&) = delete;

  inline ~PerCpuSpinLockHolder() ABSL_UNLOCK_FUNCTION() { lock_.Unlock(cpu_); }

  int CpuHeld() { return cpu_; }

 private:
  PerCpuSpinLock& lock_;
  int cpu_;
};

}  // namespace percpu
}  // namespace subtle
}  // namespace base
#endif  // THIRD_PARTY_GLOOP_BASE_PERCPU_TYPES_H_
