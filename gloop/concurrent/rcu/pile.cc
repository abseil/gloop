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

#include "gloop/concurrent/rcu/pile.h"

#include <errno.h>
#include <sys/mman.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/no_destructor.h"
#include "absl/debugging/leak_check.h"
#include "gloop/base/percpu.h"
#include "gloop/base/percpu_types.h"  // IWYU pragma: keep
#include "gloop/base/scoped_sigmask.h"
#include "gloop/base/signal_util_subtle.h"  // IWYU pragma: keep
#include "gloop/util/atomic_danger/atomic_danger.h"
#include "tcmalloc/internal/sysinfo.h"

namespace base {
namespace rcu {

using ::base::subtle::percpu::AllocHandle;
using ::base::subtle::percpu::FreeHandle;

void UnmapSlice(void* p, size_t size, int i, int cpu) {
  absl::UnRegisterLivePointers(p, size);
  if (0 != munmap(p, size)) {
    ABSL_RAW_LOG(FATAL, "Unmapping %p slice %d on %d failed: %d", p, i, cpu,
                 errno);
  }
}

void* MapSlice(size_t size, size_t slice, int cpu) {
  void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) {
    ABSL_RAW_LOG(FATAL, "Mapping slice %zu on %d failed: %d", slice, cpu,
                 errno);
  }
  absl::RegisterLivePointers(ptr, size);
  return ptr;
}

void PercpuGrowingArray::FreeSlice(int i, int cpu) {
  void* p = reinterpret_cast<void*>(
      GetPointerAtomic(slices_[i], cpu)->load(std::memory_order_relaxed));
  if (p == nullptr) return;
  GetPointerAtomic(slices_[i], cpu)->store(0, std::memory_order_relaxed);
  UnmapSlice(p, SliceSize(i), i, cpu);
}

class LSANOnlyPercpuLockHolder {
 public:
  LSANOnlyPercpuLockHolder() {
    if (absl::HaveLeakSanitizer()) {
      static absl::NoDestructor<::base::subtle::percpu::PerCpuSpinLock> lock;
      cpu_ = lock->Lock();
      lock_ = lock.get();
    }
  }
  ~LSANOnlyPercpuLockHolder() {
    if (lock_) lock_->Unlock(cpu_);
  }

 private:
  ::base::ScopedSigmask mask_;
  ::base::subtle::percpu::PerCpuSpinLock* lock_ = nullptr;
  int cpu_;
};

void PercpuGrowingArray::EnsureSliceSlow(std::atomic<int64_t>* loc,
                                         size_t slice, int cpu) {
  size_t size = SliceSize(slice);
  // Allocate an appropriate number of pages, and try to write it into
  // the slice. If we lose the race, that's fine--their pages are as
  // good as ours--just unmap and move on.  Note we must used unhooked
  // mmaps, because hooks are generally not signal safe.

  // If we're under leak sanitizer, it becomes very expensive to map a
  // slice and way more likely we will race doing so, which is doubly
  // bad.  Avoid that by taking a signal safe lock.
  LSANOnlyPercpuLockHolder h;
  (void)h;
  void* ptr = MapSlice(size, slice, cpu);
  int64_t raw = reinterpret_cast<int64_t>(ptr);
  // Release pairs with Acquire in later Ensure.
  if (0 !=
      atomic_danger::CompareAndSwap(loc, 0, raw, std::memory_order_release)) {
    // Make sure we have visibility to the other guy's slice. (This
    // should just be Barrier_CompareAndSwap, but we haven't got it.
    (void)loc->load(std::memory_order_acquire);
    UnmapSlice(ptr, size, slice, cpu);
  }
}

void PercpuGrowingArray::Init() {
  for (int i = 0; i < kNumSlices; ++i) {
    slices_[i] = AllocHandle();
  }
}

void PercpuGrowingArray::Destroy() {
  for (int i = 0; i < kNumSlices; ++i) {
    for (int cpu : Range(tcmalloc::tcmalloc_internal::NumCPUs())) {
      FreeSlice(i, cpu);
    }
    FreeHandle(slices_[i]);
  }
}

}  // namespace rcu
}  // namespace base
