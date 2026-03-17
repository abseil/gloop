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

#ifndef CONCURRENT_PERCPU_OBJECT_EMULATED_H_
#define CONCURRENT_PERCPU_OBJECT_EMULATED_H_

#include "absl/base/internal/cycleclock.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/fixed_array.h"
#include "gloop/base/spinlock.h"
#include "gloop/base/sysinfo.h"

namespace concurrent {
namespace percpu {
namespace percpu_internal {

// An emulation of base::subtle::percpu::PerCpuSpinLock for platforms without
// the kernel support for getting the currently-running CPU ID. Darwin, for
// instance, does not support it, so PerCpuSpinLock cannot be made to work
// trivially. The cpuid x86 instruction can return an identifier for the
// currently-running core (distinguishing hyperthread contexts) but the mapping
// is not dense - i.e. for a 12 core hyperthreaded system, the maximum value it
// may return is not 23 (2 * 12 - 1) but possibly higher, like 30.
//
// To work around this limitation we use a cycle-count-based timestamp to choose
// an arbitrary bucket to grab a lock for (the native implementation uses the
// current logical CPU's index to choose a bucket). The number of buckets is -
// like the native PerCpu implementation - set to the number of logical CPUs in
// order to not disrupt callers that assume so. Each bucket has its own spinlock
// (rather than an absl::Mutex, as contention is expected to be low).
class ABSL_LOCKABLE PerCpuLock {
 public:
  explicit PerCpuLock(absl::base_internal::SchedulingMode scheduling_mode =
                          absl::base_internal::SCHEDULE_COOPERATIVE_AND_KERNEL)
      : PerCpuLock(absl::FixedArray<absl::base_internal::SchedulingMode>(
            NumCPUs(), scheduling_mode)) {}

  std::size_t Lock() ABSL_SHARED_LOCK_FUNCTION() {
    const auto cpu = absl::base_internal::CycleClock::Now() % locks_.size();
    locks_[cpu].lock.lock();
    return cpu;
  }

  void LockOn(std::size_t cpu) ABSL_SHARED_LOCK_FUNCTION() {
    locks_[cpu].lock.lock();
  }

  void Unlock(std::size_t cpu) ABSL_UNLOCK_FUNCTION() {
    locks_[cpu].lock.unlock();
  }

  void UnlockOn(std::size_t cpu) ABSL_UNLOCK_FUNCTION() {
    locks_[cpu].lock.unlock();
  }

 private:
  struct alignas(ABSL_CACHELINE_SIZE) Aligned {
    template <typename... Args>
    Aligned(Args&&... args) : lock(std::forward<Args>(args)...) {}
    SpinLock lock;
  };

  PerCpuLock(const absl::FixedArray<absl::base_internal::SchedulingMode>&
                 scheduling_modes)
      : locks_(scheduling_modes.begin(), scheduling_modes.end()) {}

  absl::FixedArray<Aligned, 0> locks_;
};

}  // namespace percpu_internal
}  // namespace percpu
}  // namespace concurrent

#endif  // CONCURRENT_PERCPU_OBJECT_EMULATED_H_
