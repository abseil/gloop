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

// Copyright 2011 Google Inc. All Rights Reserved.

//
#include "gloop/base/percpu_types.h"

#ifdef MEMORY_SANITIZER
#include <sanitizer/msan_interface.h>
#endif

#include <atomic>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock_wait.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/optimization.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/percpu.h"
#include "gloop/util/atomic_danger/atomic_danger.h"

namespace base {
namespace subtle {
namespace percpu {

void PerCpuSpinLock::Delay(int iteration) {
  // We expect PerCpuSpinLocks to be even less contended than regular SpinLocks,
  // so we sleep for less than the suggested delay.
  absl::SleepFor(absl::Nanoseconds(
      absl::base_internal::SpinLockSuggestedDelayNS(iteration) / 16));
}

void PerCpuSpinLock::LockOn(int cpu) {
  std::atomic<int64_t>* const word = GetPointerAtomic(percpu_lock_, cpu);
  const int64_t unique = absl::base_internal::GetTID();
  if (ABSL_PREDICT_TRUE(percpu::IsFast())) {
    LockHelper(
        [&](int64_t lock_value) {
          // We use a unique (to this thread) lock value, and Fence, to
          // synchronize cross-cpu and cpu-local lock operations. A cpu-local
          // lock is: RSEQ {
          //    int64_t old = base::subtle::NoBarrier_Load(word);
          //    if (old != 0) { // restart }
          //    base::subtle::NoBarrier_Store(word, lock_value);
          //  }
          // In the presence of a concurrent Lock we ensure that either:
          // A) Lock() observes our CompareAndSwap, because it either happens
          //    before the Load, or our Fence interrupt the sequence before
          //    Store.  In this case, Lock() will wait for us.
          // B) Lock clobbers our CompareAndSwap and acquires the lock
          //    "successfully"; however, when FenceCPU returns, we are
          //    guaranteed visibility of that write (since all sequences either
          //    happen-before Fence or happen-after our CAS).
          //
          // Since our lock value is unique (and in particular can't be confused
          // for any other LockOn or Lock), we can determine which of these
          // cases hold by re-examining the lock word after Fence--if our value
          // is there, we're in case A, and hold the lock.  If we see anything
          // else, we are in case B, got clobbered, and need to retry. Note that
          // our CompareAndSwap also ensures mutual exclusion between concurrent
          // LockOn calls.
          //
          // It's important that `unique` isn't 0 or 1, but our TID should never
          // be.
          if (atomic_danger::CompareAndSwap(word, 0, lock_value,
                                            std::memory_order_acquire) != 0) {
            return -1;
          }
          percpu::FenceCpu(cpu);
          return word->load(std::memory_order_relaxed) == lock_value
                     ? AnnotateAcquiredCpuForTsan(cpu)
                     : -1;
        },
        unique);
  } else {
    LockHelper(
        [&](int64_t lock_value) {
          // The above algorithm is correct in the slow case as well, but since
          // the slow case is relying on atomics, we can directly use atomics
          // here without the need to fence.
          return atomic_danger::CompareAndSwap(word, 0, lock_value,
                                               std::memory_order_acquire) == 0
                     ? cpu
                     : -1;
        },
        unique);
  }
}

void PerCpuSpinLock::UnlockOn(int cpu) {
  // At the moment, Unlock is sufficient--I just maintain a different
  // unlock function in case we change the implementation.  Verify this
  // in debug mode.
  ABSL_RAW_DCHECK(
      GetPointerAtomic(percpu_lock_, cpu)->load(std::memory_order_relaxed) != 0,
      "UnlockOn with unlocked CPU");
  ABSL_RAW_DCHECK(
      GetPointerAtomic(percpu_lock_, cpu)->load(std::memory_order_relaxed) != 1,
      "UnlockOn used after Lock()");

  Unlock(cpu);
}

}  // namespace percpu
}  // namespace subtle
}  // namespace base
