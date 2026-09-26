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

#include "gloop/base/percpu.h"

#include <cstdlib>
#include <cstring>

#if __linux__ && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && \
    defined(__has_include) && __has_include("tcmalloc/internal/sysinfo.h")
#include "tcmalloc/internal/sysinfo.h"
#else
#include "absl/base/internal/sysinfo.h"
#endif

#if PERCPU_USE_RSEQ

#include <errno.h>
#include <sched.h>
#include <stddef.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "gloop/base/global_symbol_compliance.h"
#include "gloop/base/scoped_sigmask.h"
#include "tcmalloc/internal/linux_syscall_support.h"

namespace {

using base::subtle::percpu::RseqVcpuMode;

static constexpr uint32_t kRseqFlagVcpus = 1U << 31;
// vCPU-variant flags set the vCPU feature bit and a specific variant bit.
static constexpr uint32_t kRseqFlagVcpusPerL3 = (1U << 28) | kRseqFlagVcpus;

static constexpr int kMEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ = (1 << 8);

enum PerCpuStatus {
  // Using unaccelerated per-CPU operations.
  kSlowMode,

  // Using accelerated per-CPU operations, and sys_membarrier understands
  // MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ.
  //
  kFastModeWithFence,
};

// Used to track the initialization of per-cpu operations.
static PerCpuStatus per_cpu_state = kSlowMode;

// There are targets which try to cover every rseq_vcpu_mode option and should
// be adjusted whenever a flag options is added or removed.
RseqVcpuMode VcpuMode() {
  const char* env = getenv("PERCPU_VCPU_MODE");
  const absl::string_view text = absl::NullSafeStringView(env);

  if (text.empty()) {
    return RseqVcpuMode::kFlat;
  }

  // Alternatives
  if (text == "flat") return RseqVcpuMode::kFlat;
  if (text == "none") return RseqVcpuMode::kNone;
  if (text == "l3") return RseqVcpuMode::kFlatPerL3;
  if (text == "mm") return RseqVcpuMode::kMM;
  // Bah
  ABSL_RAW_LOG(ERROR, "Unrecognized rseq_vcpu_mode '%s'", env);
  return RseqVcpuMode::kNone;
}

}  // namespace

namespace base {
namespace subtle {
namespace percpu {

// ----------------------------------------------------------------------------
// Internal structures
// ----------------------------------------------------------------------------

namespace percpu_internal {

// Return whether we should skip trying to use rseq.
//
// By default this is based on the process's environment. The symbol is defined
// as weak so that tests that need to turn off restartable sequences can do so
// effectively at compile time by defining this symbol.
ABSL_ATTRIBUTE_WEAK bool disable_rseq() {
  // TODO Enable this rseq support in new glibc.
  return true;
}

}  // namespace percpu_internal

// Is this thread's __rseq_abi struct currently registered with the kernel?
static bool ThreadRegistered() { return RseqCpuId() >= kCpuIdInitialized; }

// using_rseq_vcpus contains the active vcpu mode. This value will be kNone
// until rseq has been initialized, and vcpu is available and not disabled.
static RseqVcpuMode using_rseq_vcpu_mode = RseqVcpuMode::kNone;

// Define struct rseq_cs to have the same contents and alignment as the kernel
// assumes.
struct rseq_cs {
  uint32_t version;
  uint32_t flags;
  uint64_t start_ip;
  uint64_t post_commit_offset;
  uint64_t abort_ip;
} __attribute__((aligned(4 * sizeof(uint64_t))));

extern "C" {
// We provide a per-thread value (defined in percpu_.c) which both tracks
// thread-local initialization state and (with RSEQ) provides an atomic
// in-memory reference for this thread's execution CPU.  This value is only
// valid when the thread is currently executing
// Possible values:
//   Unavailable/uninitialized:
//     { kCpuIdUnsupported, kCpuIdUninitialized }
//   Initialized, available:
//     [0, NumCpus())    (Always updated at context-switch)
ABSL_CONST_INIT ABSL_ATTRIBUTE_WEAK thread_local volatile kernel_rseq
    __rseq_abi = {
        0, static_cast<unsigned>(kCpuIdUninitialized),   0, 0, 0,
        0, {{kCpuIdUninitialized, kCpuIdUninitialized}},
    };

ABSL_CONST_INIT __attribute__((used)) size_t __rseq_virtual_flat_cpu_id_offset =
    offsetof(kernel_rseq, cpu_id);

}  // extern "C"

RseqVcpuMode GetRseqVcpuMode() {
  IsFast();  // Make sure the thread is initialized.
  return using_rseq_vcpu_mode;
}

// Flags to pass to sys_rseq when registering a thread.
static uint32_t rseq_register_flags = 0;

// Returns flags to be passed to rseq() syscall based on RSEQ VCPU mode.
static uint32_t RseqVcpuModeToFlags(RseqVcpuMode mode) {
  switch (mode) {
    case RseqVcpuMode::kNone:
      return 0;
    case RseqVcpuMode::kFlat:
      return kRseqFlagVcpus;
    case RseqVcpuMode::kFlatPerL3:
      return kRseqFlagVcpusPerL3;
    case RseqVcpuMode::kMM:
      return 0;  // accessing mm_cid needs normal RSEQ on a modern kernel
  }
}

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1U
#endif

static int RegisterRseq(const uint32_t flags) {
  if (syscall(__NR_rseq, &__rseq_abi, sizeof(__rseq_abi), flags,
              PERCPU_RSEQ_SIGNATURE)) {
    return errno;
  }

  // Assumption check: we should now show up as registered.
  ABSL_RAW_CHECK(ThreadRegistered(), "Unexpectedly not registered.");

  return 0;
}

static int UnregisterRseq() {
  constexpr uint32_t kRSEQ_FLAG_UNREGISTER = (1 << 0);
  if (syscall(__NR_rseq, &__rseq_abi, sizeof(__rseq_abi), kRSEQ_FLAG_UNREGISTER,
              PERCPU_RSEQ_SIGNATURE)) {
    return errno;
  }

  // Assumption check: we should now show up as unregistered again.
  ABSL_RAW_DCHECK(!ThreadRegistered(), "CPU ID not uninitialized");

  return 0;
}

// Perform process-wide initialization.
//
// This function sets per_cpu_state and other globals. It must be called no more
// than once.
static void InitRseqForProcess() {
  ABSL_RAW_DCHECK(per_cpu_state == kSlowMode,
                  "InitRseqForProcess called twice.");

  // Bail out if we're not supposed to initialize at all.
  if (percpu_internal::disable_rseq() || HasDuplicateGlobalSymbols()) return;

  // Use the open source fence implementation based on membarrier(2) then
  // register our intent to use it with the kernel, finding out whether the
  // kernel supports it in the process.
  //
  // Check whether MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ is available. if it
  // is, this implies that rseq is also available.
  //
  // membarrier(2) will return 0 only if the register command is available and
  // the architecture supports the MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ command
  // we use later.
  if (syscall(SYS_membarrier, kMEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ,
              0, 0) == 0) {
    per_cpu_state = kFastModeWithFence;
  } else {
    per_cpu_state = kSlowMode;
    return;
  }

  // If we're supposed to be using VCPU mode, try first to register in that mode
  // (dynamically detecting kernel support by falling through if we fail).
  int rseq_errno = -1;

  auto TryRegister = [&](RseqVcpuMode vcpu_mode) {
    if (vcpu_mode == RseqVcpuMode::kMM) {
      auto auxv = getauxval(AT_RSEQ_FEATURE_SIZE);
      if (auxv < offsetof(kernel_rseq, mm_cid) + sizeof(__rseq_abi.mm_cid)) {
        return false;
      }
    }

    const uint32_t flags = RseqVcpuModeToFlags(vcpu_mode);
    rseq_errno = RegisterRseq(flags);

    if (rseq_errno == 0) {
      // Save globals.
      using_rseq_vcpu_mode = vcpu_mode;
      rseq_register_flags = flags;
      switch (vcpu_mode) {
        case RseqVcpuMode::kFlat:
        case RseqVcpuMode::kFlatPerL3:
          __rseq_virtual_flat_cpu_id_offset = offsetof(kernel_rseq, vcpu_id);
          break;
        case RseqVcpuMode::kMM:
          __rseq_virtual_flat_cpu_id_offset = offsetof(kernel_rseq, mm_cid);
          break;
        case RseqVcpuMode::kNone:
          break;
      }

      return true;
    } else if (rseq_errno != ENOSYS && rseq_errno != EINVAL &&
               rseq_errno != ENOTSUP) {
      // Do not emit warnings or errors on ENOSYS, EINVAL, ENOTSUP: we have
      // architectures, emulations and kernels where RSEQ / RSEQ VCPU is not
      // available, and we have tests failing on messages emitted at startup.
      ABSL_RAW_LOG(WARNING,
                   "Failed to initialize RSEQ VCPUs in mode %d, error = %d",
                   static_cast<int>(vcpu_mode), rseq_errno);
    }

    return false;
  };

  // IMPORTANT: If the requested vCPU mode is unavailable, we should downgrade
  // to the current fleet-wide default (see VcpuMode()).
  switch (RseqVcpuMode vcpu_mode = VcpuMode()) {
    case RseqVcpuMode::kMM:
    case RseqVcpuMode::kFlatPerL3:
      if (TryRegister(vcpu_mode)) {
        break;
      }
      [[fallthrough]];
    case RseqVcpuMode::kFlat:
      if (TryRegister(RseqVcpuMode::kFlat)) {
        break;
      }
      [[fallthrough]];
    case RseqVcpuMode::kNone:
      if (TryRegister(RseqVcpuMode::kNone)) {
        break;
      }
  }

  // If we already set per_cpu_state above due to detecting fence support, we're
  // done.
  if (rseq_errno == 0) {
    return;
  }

  if (rseq_errno != EINVAL) {
    // rseq can return EINVAL when a `struct rseq` has already been registered
    // for the process (e.g. if we running under a libc which does this).
    // Avoid generating noisy logs in this case.
    ABSL_RAW_LOG(WARNING,
                 "rseq syscall failed with errno %d after membarrier syscall "
                 "succeeded.",
                 rseq_errno);
  }

  // rseq may fail even if the membarrier call above succeeded. Possible
  // reasons include:
  //  - Some containers implement/allow sys_membarrier but not sys_rseq (see
  //    b/187998112 for context).
  //  - We may be running in a process where rseq has already been registered.
  //
  // Fall back to slow mode on any failures.
  per_cpu_state = kSlowMode;
}

bool UsingRseqVirtualCpus() {
  switch (using_rseq_vcpu_mode) {
    case RseqVcpuMode::kFlat:
    case RseqVcpuMode::kFlatPerL3:
    case RseqVcpuMode::kMM:
      return true;
    case RseqVcpuMode::kNone:
      return false;
  }
}

void UnregisterThread() {
  // If this thread is already not registered, there's nothing for us to do.
  if (!ThreadRegistered()) {
    return;
  }

  // Unregister our struct from the kernel.
  if (const int err = UnregisterRseq(); err != 0) {
#ifndef NDEBUG
    ABSL_RAW_LOG(FATAL, "Failed to unregister. errno: %d", err);
#endif
  }
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

ABSL_ATTRIBUTE_NOINLINE bool InitThreadCpuId() {
  // On the first trip through this function do the necessary process-wide
  // initialization work.
  //
  // We do this with all signals disabled so that we don't deadlock due to
  // re-entering from a signal handler.
  //
  // We use a global atomic to record an 'initialized' state as a fast path
  // check, which allows us to avoid the signal mask syscall that we must
  // use to prevent nested initialization during a signal deadlocking on
  // LowLevelOnceInit, before we can enter the 'init once' logic.
  ABSL_CONST_INIT static std::atomic<bool> initialized(false);
  if (!initialized.load(std::memory_order_acquire)) {
    ScopedSigmask mask;

    ABSL_CONST_INIT static absl::once_flag once;
    absl::base_internal::LowLevelCallOnce(&once, [&] {
      // Perform process-wide initialization, setting per_cpu_state.
      InitRseqForProcess();

      // If we're in slow mode, make sure that the slow mode locks are safe to
      // use once this function returns.
      if (per_cpu_state == kSlowMode) {
        EnsureSlowModeInitialized();
      }

      // Set `initialized` to true after all initialization has completed.
      // The below store orders with the load acquire further up, i.e., all
      // initialization and side effects thereof are visible to any thread
      // observing a true value in the fast path check.
      initialized.store(true, std::memory_order_release);
    });
  }

  // Now ensure this particular thread is registered if we didn't decide to use
  // slow mode. (It may have been taken care of by the process-wide logic above
  // if we were the first to call.)
  if (per_cpu_state != kSlowMode && !ThreadRegistered()) {
    // Mask signals and double check thread registration afterwards.  If we
    // encounter a signal between ThreadRegistered() above and RegisterRseq()
    // and that signal initializes per-CPU, RegisterRseq here will fail with
    // EBUSY.
    ScopedSigmask mask;

    if (!ThreadRegistered()) {
      const int old_rseq_cpu_id = RseqCpuId();
      if (const int err = RegisterRseq(rseq_register_flags); err != 0) {
        const int new_rseq_cpu_id = RseqCpuId();
        // Since process-wide initialization succeeded we expect that
        // registering the thread should too.
        ABSL_RAW_LOG(FATAL,
                     "Thread registration failed with errno %d "
                     "(__rseq_abi.cpu_id = %d -> %d)",
                     err, old_rseq_cpu_id, new_rseq_cpu_id);
      }
    }
  }

  // If we've decided to use slow mode, set the thread-local CPU ID to
  // kCpuIdUnsupported so that IsFast doesn't call this function again for this
  // thread.
  if (per_cpu_state == kSlowMode) {
    __rseq_abi.cpu_id = kCpuIdUnsupported;
  }

  return per_cpu_state == kFastModeWithFence;
}

namespace percpu_internal {

void FenceUnsafe() {
  // Prevent compiler re-ordering of code below.
  CompilerBarrier();

  // Other operations (or all in RSEQ mode) might just be running on
  // another CPU.  Do something about that: use RSEQ::Fence() to
  // just send interrupts and restart any such operation.
  percpu_internal::UpstreamRseqFenceCpu(-1);
}

void FenceCpuUnsafe(int cpu) {
  // 'cpu' is actually a VCPU, so we don't know which actual CPU to interrupt,
  // so interrupt all of them.
  percpu_internal::UpstreamRseqFenceCpu(cpu);
}

bool UsingUpstreamRseqFenceCpu() { return per_cpu_state == kFastModeWithFence; }

void UpstreamRseqFenceCpu(int cpu) {
  ABSL_RAW_CHECK(per_cpu_state == kFastModeWithFence,
                 "upstream fence unavailable.");
  constexpr int kMEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ = (1 << 7);
  constexpr int kMEMBARRIER_CMD_FLAG_CPU = (1 << 0);

  int64_t res = syscall(__NR_membarrier, kMEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ,
                        kMEMBARRIER_CMD_FLAG_CPU, cpu);

  if (ABSL_PREDICT_FALSE(res != 0 && res != -ENXIO /* missing CPU */)) {
    ABSL_RAW_LOG(FATAL, "Upstream fence failed: %ld", res);
  }
}

}  // namespace percpu_internal

}  // namespace percpu
}  // namespace subtle
}  // namespace base

#endif  // PERCPU_USE_RSEQ

namespace base {
namespace subtle {
namespace percpu {

int NumCPUs() {
#if __linux__ && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && \
    defined(__has_include) && __has_include("tcmalloc/internal/sysinfo.h")
  return ::tcmalloc::tcmalloc_internal::NumCPUs();
#else
  return ::absl::base_internal::NumCPUs();
#endif
}

}  // namespace percpu
}  // namespace subtle
}  // namespace base
