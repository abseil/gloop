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
// Provides skeleton RSEQ functions which raise a hard error in the case of
// being erroneously called on an unsupported platform.

#include <stdint.h>

#include <atomic>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/sysinfo.h"
#include "gloop/base/percpu.h"
#include "gloop/base/percpu_types.h"
#include "gloop/base/scoped_sigmask.h"

namespace base {
namespace subtle {
namespace percpu {

namespace {

// Storage for the slow-mode lock, properly aligned.
std::aligned_storage<sizeof(PerCpuSpinLock), alignof(PerCpuSpinLock)>::type
    cas_lock_storage_;

// Pointers to the slow-mode lock. Must not be used until constructed.
PerCpuSpinLock* cas_lock_ =
    reinterpret_cast<PerCpuSpinLock*>(&cas_lock_storage_);

}  // namespace

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

ABSL_ATTRIBUTE_NOINLINE void EnsureSlowModeInitialized() {
  ScopedSigmask mask;
  static absl::once_flag once;
  absl::base_internal::LowLevelCallOnce(
      &once, [] { new (&cas_lock_storage_) PerCpuSpinLock; });
}

// ----------------------------------------------------------------------------
// Implementation of unaccelerated (no RSEQ) per-cpu operations
// ----------------------------------------------------------------------------

FetchAddResult AtomicFetchAddSlow(Handle percpu_data, int64_t delta) {
  ScopedSigmask mask;
  FetchAddResult r;
  r.cpu = cas_lock_->Lock();
  r.previous =
      GetPointerAtomic(percpu_data, r.cpu)->load(std::memory_order_relaxed);
  GetPointerAtomic(percpu_data, r.cpu)
      ->store(r.previous + delta, std::memory_order_relaxed);
  cas_lock_->Unlock(r.cpu);

  return r;
}

int CompareAndSwapCheckSlow(int target_cpu, std::atomic<int64_t>* p,
                            int64_t old_val, int64_t new_val,
                            std::atomic<int64_t>* check_ptr,
                            int64_t check_val) {
  ScopedSigmask mask;
  int ret, cpu;

  cpu = cas_lock_->Lock();
  if (cpu != target_cpu) {
    ret = cpu;
  } else if (check_ptr != nullptr &&
             check_ptr->load(std::memory_order_acquire) != check_val) {
    ret = -1;
  } else if (p->load(std::memory_order_acquire) != old_val) {
    ret = -1;
  } else {
    p->store(new_val, std::memory_order_release);
    ret = target_cpu;
  }
  cas_lock_->Unlock(cpu);

  return ret;
}

// ----------------------------------------------------------------------------
// Slow fence operations
// ----------------------------------------------------------------------------

static void FenceTakeSpinlocks(int cpu) {
  ABSL_RAW_CHECK(
      !IsFast(),
      "should never call this in Fast mode, we can't do cross cpu locks "
      "with RSEQ");
  cas_lock_->LockOn(cpu);
  cas_lock_->UnlockOn(cpu);
}

namespace percpu_internal {

void FenceFallback() {
  CompilerBarrier();

  // If we're not using RSEQ, percpu operations are just under
  // various spinlocks.  Take all of them at least once (on each
  // CPU) to make sure we aren't racing with any such operation.
  int num_cpus = absl::base_internal::NumCPUs();
  ScopedSigmask mask;
  for (int i = 0; i < num_cpus; ++i) {
    FenceTakeSpinlocks(i);
  }
}

void FenceCpuFallback(int cpu) {
  ScopedSigmask mask;
  FenceTakeSpinlocks(cpu);
}

}  // namespace percpu_internal

#if !PERCPU_USE_RSEQ

extern "C" {

// Avoid link errors: assembly code may reference this symbol
ABSL_CONST_INIT size_t __rseq_virtual_flat_cpu_id_offset = 0;

}  // extern "C"

static void Unsupported() {
  ABSL_RAW_LOG(FATAL, "RSEQ function called on unsupported platform.");
}

ABSL_ATTRIBUTE_NOINLINE bool InitThreadCpuId() { return false; }

extern "C" {

int RseqFunction_PerCpuTryLock(volatile kernel_rseq*, Handle, int64_t) {
  Unsupported();
  return -1;
}

FetchAddResult RseqFunction_PerCpuAtomicFetchAdd(volatile kernel_rseq*, Handle,
                                                 int64_t) {
  Unsupported();
  return {-1, 0};
}

int RseqFunction_PerCpuCmpxchg64(volatile kernel_rseq*, int, int64_t*, int64_t,
                                 int64_t) {
  Unsupported();
  return -1;
}

int RseqFunction_PerCpuCmpxchgCheck64(volatile kernel_rseq*, int, int64_t*,
                                      int64_t, int64_t, int64_t*, int64_t) {
  Unsupported();
  return -1;
}

RseqCycleCounterResult RseqFunction_PerCpuReadCycleCounter(
    volatile kernel_rseq*) {
  Unsupported();
  return RseqCycleCounterResult();
}

}  // extern "C"

#endif  // !PERCPU_USE_RSEQ

}  // namespace percpu
}  // namespace subtle
}  // namespace base
