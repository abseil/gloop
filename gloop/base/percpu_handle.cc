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

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "gloop/base/percpu.h"
#include "gloop/base/scheduling/scheduling_mode.h"

#if __linux__

#include <stddef.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <atomic>

#include "absl/base/call_once.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/thread_annotations.h"
#include "gloop/base/spinlock.h"

// The <sys/prctl.h> on some systems may not define these macros yet even though
// the kernel may have support for the new PR_SET_VMA syscall, so we explicitly
// define them here.
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif

#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

namespace base {
namespace subtle {
namespace percpu {
// ----------------------------------------------------------------------------
// Internal structures
// ----------------------------------------------------------------------------

// Handle:
// A Handle references memory that has been allocated for per-cpu usage.
// Since we want to be able to pass around a single "base" pointer for a
// variable and not the entire array of per-cpu addresses a consistent
// convention is important.  The size of an individual region must also be
// constant at compile time because it is shared with the per-cpu assembly
// sequences which back RSEQ primitives.
//
// We use the schema:
//  base_ptr + cpu_idx * PERCPU_BYTES_PER_REGION
//
// Specifically, base_ptr will always address the object as seen from cpu-0,
// and traversing at a PERCPU_BYTES_PER_REGION gives us value corresponding to
// each online CPU respectively.
//
// This is chosen over a straight array[NumCPUs()] since for performance we want
// to guarantee each CPU's data resides on an unshared cache-line.  The
// flip-side to this is how do we avoid wastage from such a scheme?  This is
// where the idea of a region comes in.
//
// A region is an arbitrary multiple of cache-line size (we choose PAGE_SIZE
// as the current default) which consists solely of adjacent per-cpu pointers
// for a given CPU.  While this does not place a limit on the number of active
// objects, we require the number of adjacent objects to be specified at compile
// time to keep de-referencing efficient.
namespace {

// Given backing for N objects on num_cpus CPUs previously allocated by
// AllocateBacking, return a handle for the index'th object in the backing.
//
// REQUIRES: index < N
static inline Handle GetBackingHandle(int64_t* backing, int num_cpus,
                                      int index) {
  // Calculate which region the index falls into (see comments above explaining
  // per-cpu object memory layout).
  size_t offset = (index / kObjectsPerRegion) * kObjectsPerRegion;
  offset *= num_cpus;

  // Add the offset into this region.
  offset += index % kObjectsPerRegion;

  Handle h;
  h.rep = backing + offset;
  return h;
}

// Get at least enough space for n handles (return how many we had space for
// in *actual.)
static int64_t* AllocateBacking(int num_cpus, int n, int* actual) {
  // We must allocate a multiple of the region size (the stride with which
  // corresponding items are accessed) for each CPU, since we are allocating
  // contiguous memory for all CPUs together.
  n = ((n + kObjectsPerRegion - 1) / kObjectsPerRegion) * kObjectsPerRegion;

  // We use ABSL_RAW_CHECK here, since we may be called by malloc.
  ABSL_RAW_CHECK(n > 0, "percpu-allocator: invalid size");

  // Find the system page size. Note that POSIX guarantees sysconf to be async
  // signal safe (it doesn't call malloc).
  const size_t page_size = sysconf(_SC_PAGESIZE);

  // Perform the allocation. Make sure the length argument to mmap is
  // page-aligned.
  size_t mmap_length = num_cpus * n * kObjectSize;
  mmap_length = ((mmap_length + page_size - 1) / page_size) * page_size;

  void* mem = mmap(nullptr, mmap_length, PROT_WRITE | PROT_READ,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  ABSL_RAW_CHECK(mem != MAP_FAILED,
                 "percpu-allocator: mmap for object backing failed");

  const char kName[] = "percpu_handle_region";
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, mem, mmap_length, kName);

  *actual = n;
  return static_cast<int64_t*>(mem);
}

}  // namespace

// ----------------------------------------------------------------------------
// Per-cpu memory allocator interface
// ----------------------------------------------------------------------------

ABSL_CONST_INIT static SpinLock alloc_handle_lock_(
    absl::base_internal::SCHEDULE_KERNEL_ONLY);

static int64_t* handle_freelist_ = nullptr;

static void EnqueueHandle(Handle h)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(alloc_handle_lock_) {
  // We keep a linked list in the cpu-0 value.
  GetPointerAtomic(h, 0)->store(reinterpret_cast<int64_t>(handle_freelist_),
                                std::memory_order_relaxed);
  handle_freelist_ = h.rep;
}

Handle AllocHandle() {
  Handle ret;
  const int num_cpus = NumCPUs();
  {
    SpinLockHolder h(alloc_handle_lock_);
    if (handle_freelist_ != nullptr) {
      ret.rep = handle_freelist_;
      // Link is through the CPU-0 slot.
      int64_t* next = reinterpret_cast<int64_t*>(
          GetPointerAtomic(ret, 0)->load(std::memory_order_relaxed));
      handle_freelist_ = next;
    } else {
      // allocate more backing:
      int n;
      int64_t* backing = AllocateBacking(num_cpus, 1, &n);
      // Take the first one:
      ret.rep = backing;
      // and return the rest to our freelist.
      for (int i = 1; i < n; ++i) {
        EnqueueHandle(GetBackingHandle(backing, num_cpus, i));
      }
    }
  }
  // handles are specced as zero-initialized:
  for (int i = 0; i < num_cpus; ++i) {
    GetPointerAtomic(ret, i)->store(0, std::memory_order_relaxed);
  }
  return ret;
}

// We don't bother ever returning memory to kernel (it's highly
// unlikely we'll ever need so many handles to make that matter, and
// we'd need to find some way to track the refcount of a page of
// handles.)
void FreeHandle(Handle percpu_data) {
  // Handles returned by NullHandle() do not have allocated storage.
  if (percpu_data.rep == nullptr) return;
  SpinLockHolder h(alloc_handle_lock_);
  EnqueueHandle(percpu_data);
}

#else

namespace base {
namespace subtle {
namespace percpu {

Handle AllocHandle() { return NullHandle(); }
void FreeHandle(Handle percpu_data) {}

#endif

}  // namespace percpu
}  // namespace subtle
}  // namespace base
