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

// This library provides support for optimized per-cpu operations.
//
// It provides a per-cpu specific memory allocator, an in-memory cpu-id,  as
// well as generic operation primitives.  On supported platforms (64-bit Linux)
// these operations will execute extremely quickly, without the use of atomic
// instructions.
//
// The availability of fast-mode can be checked at run-time using
//     base::subtle::percpu::IsFast()
// In general fast-mode should *always* be available for 64-bit binaries within
// the Google production environment.

#ifndef THIRD_PARTY_GLOOP_BASE_PERCPU_H_
#define THIRD_PARTY_GLOOP_BASE_PERCPU_H_

#ifdef __linux__
#include <sched.h>
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "gloop/base/config.h"
#include "gloop/base/internal/percpu.inc"
#include "gloop/base/percpu_macros.h"
#include "gloop/util/atomic_danger/atomic_danger.h"
#include "tcmalloc/internal/linux_syscall_support.h"

namespace base {
namespace subtle {
namespace percpu {

// -----------------------------------------------------------------------
// Handle functions
//
// A Handle references a set of int64_t objects, one per active cpu, that
// has been allocated for per-cpu usage.  Each word in a per-cpu object is
// guaranteed to reside on an unshared cache-line.
//
// IMPORTANT: `AllocHandle()` is currently only supported on Linux (#ifdef
// __linux__) platforms. On all other platforms `AllocHandle()` returns a null
// handle.
// -----------------------------------------------------------------------
struct Handle {
  int64_t* rep = nullptr;
};

// Returns a reference equivalent to &Handle[cpu]
// Clients should never write to this pointer except immediately after
// allocating it during initialization.
// NOTE: Your current CPU may change at any time (including while reading this
// value).  While it is always safe to query this data, control-flow decisions
// should not be dependent on it.
std::atomic<int64_t>* GetPointerAtomic(Handle percpu_data, int cpu);

// Returns a handle to a newly allocated family of per-cpu words.  Each per-cpu
// word (addressable using *GetPointerAtomic(handle, cpu)) will be initially
// zero.
// NOTE:  Most users will prefer to instead use the types provided in
// percpu_types.h
Handle AllocHandle();

// Returns an invalid handle that cannot be read or written through, but is
// safe to free. (This is most useful for move constructors.)
Handle NullHandle();

// Releases a handle previously returned by AllocHandle() (or NullHandle).
void FreeHandle(Handle percpu_data);

// Get the id of the current thread's executing logical core id on supported
// environments (currently only Linux/glibc).  Returns a value in the range
// [0, NumCPUs()) on success and -1 when this operation is unsupported.
int GetCurrentCpu();

// Returns the number of possible CPUs on the machine, including currently
// offline CPUs.
//
// Unlike base::NumCPUs(), this function properly handles offlined CPUs.
//
// TODO: Remove this function once absl::base_internal::NumCPUs() is
// correct, and base::NumCPUs() is accurate again.
int NumCPUs();

// Returns whether or not __rseq_abi.vcpu_id is populated using the flat CPU ID
// model, without providing the NUMA node.
bool UsingRseqVirtualCpus();

// Returns the id of the current thread's executing virtual cpu id on supported
// environments that have flat virtual cpu support. If the current platform does
// not support flat virtual CPUs, then this function will return the same value
// as GetCurrentCpu(). I.e.: this function is always safe to call.
inline int GetCurrentVirtualFlatCpu();

// Returns whether the PerCPU mechanisms are using "fast" mode.
// Here and above fast mode denotes the use of Restartable Sequences (RSEQ) to
// avoid the use of atomic intrinsics.
bool IsFast();

// As IsFast(), but if this thread isn't already initialized, will not
// attempt to do so.
bool IsFastNoInit();

struct FetchAddResult {
  int cpu;
  int64_t previous;
};

// Atomically
//   int64_t& v = *GetPointerAtomic(percpu_data, this_cpu);
//   int64_t previous = v;
//   v += delta;
//   return {this_cpu, previous}
//
// With acquire/release semantics relative to other per-CPU operations.  delta
// may be negative.
FetchAddResult AtomicFetchAdd(Handle percpu_data, int64_t delta);

// Atomically (as observed by any thread executing on target_cpu):
//
//   if (current_cpu != target_cpu) {
//    return current_cpu;
//   } else if (*p != old_val) {
//    return -1;
//   } else { /* current_cpu == target_cpu && *p == old_val */
//    *p = new_val
//    return target_cpu
//   }
//
// with acquire semantics on the read from *p and release semantics on the write
// to *p relative to other percpu operations.
//
// NOTE: *p must not be concurrently modified by another CPU. In the case of
// CompareAndSwap failing due to a cpu mis-match; both p and target_cpu must be
// updated before retrying.
//
// Examples of a suitable 'p':
//   Handle percpu_data; ...; p = GetPointerAtomic(percpu_data, target_cpu);
// Or:
//   int v[NumCPUs()];  ...; p = &v[target_cpu];
//
// Example usage, backing p with a Handle:
//   for (target_cpu = GetCurrentCpu();;) {
//     p = GetPointerAtomic(percpu_data, target_cpu);
//     old_val = *p;
//     result = CompareAndSwap(target_cpu, p, old_val, new_val);
//     if (result == target_cpu) {
//       break;
//     } else if (result != -1)  {
//       target_cpu = result;
//     }
//   }
int CompareAndSwap(int target_cpu, std::atomic<int64_t>* p, int64_t old_val,
                   int64_t new_val);

// Atomically (as observed by any thread executing on target_cpu):
//
//   if (current_cpu != target_cpu) {
//    return current_cpu;
//   } else if (*p != old_val || *check != check_val) {
//    return -1;
//   } else { /* current_cpu == target_cpu && *p == old_val */
//    *p = new_val
//    return target_cpu
//   }
//
// with acquire semantics on the reads from *p and *check, and release semantics
// on the write to *p relative to other percpu operations.
//
// See the notes on CompareAndSwap above regarding atomicity and visibility of
// writes. As with CompareAndSwap, *p must not be concurrently modified by
// another CPU.
//
// If another CPU may be concurrently writing to *check_ptr, it should follow
// the call with a Fence(). The visibility of a remote update to *check_ptr is
// only guaranteed after a matching call to Fence() has completed.
int CompareAndSwapCheck(int target_cpu, std::atomic<int64_t>* p,
                        int64_t old_val, int64_t new_val,
                        std::atomic<int64_t>* check_ptr, int64_t check_val);

// Everything above is subtle.  This is more so.  Please be extremely
// sure you know what you are doing before calling Fence.

// Fence() is a synchronization point.  It guarantees, at moment of return:
// (a) Any "percpu" operations currently executing on another CPU began
//     after the call to Fence().
// (b) Fence() and any (a) form a Release/Acquire pair respectively.
//
// An example: object replacement.  For shared objects dereferenced through
// a global pointer we can update that pointer and Fence().  This guarantees
// no outstanding operations on the previous object may exist.  Requires that
// the dereference occurs within the percpu operation.
void Fence();

// As Fence, but the guarantees only hold against remote operations on "cpu".
// Likely to be more efficient than Fence if only a single cpu is required.
//
// cpu must be a physical CPU ID, not a "virtual" CPU ID (<link>).  If
// given a virtual CPU ID, fencing WILL NOT WORK AS EXPECTED and CAN LEAD TO
// DATA CORRUPTION.
//
// TODO: Enforce this with the type system.
void FenceCpu(int cpu);

// Unregister the current thread from rseq if it is currently registered.
//
// Do not use this unless you know what you're doing! It is typically useful
// only when testing the rseq implementation and working around rseq bugs.
void UnregisterThread();

class ScopedUnregisterRseq {
 public:
  ScopedUnregisterRseq() : was_registered_(IsFastNoInit()) {
    UnregisterThread();
  }

  ~ScopedUnregisterRseq() {
    if (was_registered_) {
      ABSL_RAW_CHECK(IsFast(), "unable to reinitialize rseq");
    }
  }

 private:
  const bool was_registered_;
};

////////////////////////////////////////////////////////////////////////
// Implementation details
////////////////////////////////////////////////////////////////////////

inline constexpr size_t kObjectSize = sizeof(int64_t);
inline constexpr size_t kObjectsPerRegion =
    PERCPU_BYTES_PER_REGION / kObjectSize;

// Internal state used for tracking initialization of RseqCpuId()
inline constexpr int kCpuIdUnsupported = -2;
inline constexpr int kCpuIdUninitialized = -1;
inline constexpr int kCpuIdInitialized = 0;

// `__rseq_virtual_flat_cpu_id_offset` contains the offset into `kernel_rseq` of
// the `vcpu_id` field if flat virtual cpu mode is available and active, and
// the offset of the 16 bit portion of the `cpu_id` field if not.
extern "C" ABSL_CONST_INIT size_t __rseq_virtual_flat_cpu_id_offset;

#if PERCPU_USE_RSEQ
extern "C" ABSL_CONST_INIT thread_local volatile kernel_rseq __rseq_abi;

inline int RseqCpuId() { return __rseq_abi.cpu_id; }
ABSL_DEPRECATED("Prefer RseqVirtualFlatCpuId")
inline int16_t RseqVcpuId() { return __rseq_abi.vcpu_id; }
ABSL_DEPRECATED("Prefer RseqVirtualFlatCpuId")
inline int RseqVcpuFlat() { return __rseq_abi.vcpu_flat; }
inline int16_t RseqVirtualFlatCpuId() {
  return *reinterpret_cast<volatile int16_t*>(
      reinterpret_cast<volatile char*>(&__rseq_abi) +
      __rseq_virtual_flat_cpu_id_offset);
}
inline volatile kernel_rseq* RseqAbi() { return &__rseq_abi; }
#else  // !PERCPU_USE_RSEQ
inline int RseqCpuId() { return kCpuIdUnsupported; }
inline int16_t RseqVcpuId() { return kCpuIdUnsupported; }
inline int16_t RseqVirtualFlatCpuId() { return kCpuIdUnsupported; }
inline int RseqVcpuFlat() { return kCpuIdUnsupported; }
inline volatile kernel_rseq* RseqAbi() { return nullptr; }
#endif

enum class RseqVcpuMode {
  kNone,       // No RSEQ VCPUs
  kFlat,       // RSEQ VCPUs are assigned ignoring NUMA
  kFlatPerL3,  // RSEQ VCPUs are clustered by L3, see <link>.
  kMM,         // Upstream (v6.6) provided flat
};

RseqVcpuMode GetRseqVcpuMode();

inline Handle NullHandle() { return Handle(); }

// As IsFast(), but if this thread isn't already initialized, will not
// attempt to do so.
inline bool IsFastNoInit() {
  if (!PERCPU_USE_RSEQ) {
    return false;
  }
  int cpu = RseqCpuId();
  return ABSL_PREDICT_TRUE(cpu >= kCpuIdInitialized);
}

#if !PERCPU_USE_RSEQ

inline void UnregisterThread() {}

inline RseqVcpuMode GetRseqVcpuMode() { return RseqVcpuMode::kNone; }

inline bool UsingRseqVirtualCpus() { return false; }

#endif  // !PERCPU_USE_RSEQ

// Functions below are implemented in the architecture-specific percpu_rseq_*.S
// files.
extern "C" {
// Operations

#if PERCPU_USE_RSEQ && defined(__x86_64__)

static inline FetchAddResult RseqFunction_PerCpuAtomicFetchAdd(
    volatile kernel_rseq* rseq_abi, Handle percpu_data, int64_t delta) {
  uint64_t scratch;
  int64_t new_value;
  int64_t cpu;
  asm (
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqFunction_PerCpuAtomicFetchAdd, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(cpu)
      "mov %[cpu], %[scratch]\n"
      "shl %[shift], %[scratch]\n"
      "mov (%[base], %[scratch]), %[new_value]\n"
      "add %[delta], %[new_value]\n"
      "mov %[new_value], (%[base], %[scratch])\n"
      "5:\n"

      // clang-format on
      : [scratch] "=&r"(scratch), [cpu] "=&r"(cpu), [new_value] "=&r"(new_value)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi),
        [base] "r"(percpu_data),
        [delta] "r"(delta),
        [shift] "n"(PERCPU_BYTES_PER_REGION_SHIFT)
      : PERCPU_RSEQ_CLOBBERS, "memory");

  return FetchAddResult{static_cast<int>(cpu), new_value - delta};
}

#else  // !if PERCPU_USE_RSEQ || !defined(__x86_64__)
FetchAddResult RseqFunction_PerCpuAtomicFetchAdd(volatile kernel_rseq* rseq_abi,
                                                 Handle percpu_data,
                                                 int64_t delta);
#endif

int RseqFunction_PerCpuCmpxchg64(volatile kernel_rseq* rseq_abi, int target_cpu,
                                 int64_t* p, int64_t old_val, int64_t new_val);

#if PERCPU_USE_RSEQ_GOTO && defined(__x86_64__)
static inline int RseqFunction_PerCpuCmpxchgCheck64(
    volatile kernel_rseq* rseq_abi, int target_cpu, int64_t* p, int64_t old_val,
    int64_t new_val, int64_t* check_p, int64_t check_val) {
  ABSL_RAW_DCHECK(IsFastNoInit(), "Fast per-CPU not enabled");

  uint64_t scratch;
  asm goto (
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqFunction_PerCpuCmpxchgCheck64, scratch)

      // Start
      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(scratch)
      "cmp %k[scratch], %[target_cpu]\n"
      // If there is a mismatch in CPU, we return the new CPU ID.
      "jne 5f\n"
      "cmp %[old_val], (%[p])\n"
      "jne %l[fail_contended]\n"
      "cmp %[check_val], (%[check_ptr])\n"
      "jne %l[fail_contended]\n"
      "mov %[new_val], (%[p])\n"
      // Commit
      "5:\n"

      // clang-format on
      : [scratch] "=&r"(scratch)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi),
        [target_cpu] "r"(target_cpu),
        [p] "r"(p), [old_val] "r"(old_val), [new_val] "r"(new_val),
        [check_ptr] "r"(check_p), [check_val] "r"(check_val)
      : PERCPU_RSEQ_CLOBBERS, "memory"
      : fail_contended);
  ABSL_ASSUME(static_cast<int>(scratch) >= kCpuIdInitialized);
  return static_cast<int>(scratch);
fail_contended:
  return -1;
}

#else  // !if PERCPU_USE_RSEQ_GOTO || !defined(__x86_64__)
int RseqFunction_PerCpuCmpxchgCheck64(volatile kernel_rseq* rseq_abi,
                                      int target_cpu, int64_t* p,
                                      int64_t old_val, int64_t new_val,
                                      int64_t* check_p, int64_t check_val);
#endif

struct RseqCycleCounterResult {
  int cpu;
  uint64_t cycles;
};

RseqCycleCounterResult RseqFunction_PerCpuReadCycleCounter(
    volatile kernel_rseq* rseq_abi);

// Equivalent to:
//
// rseq {
//   const auto current_cpu = GetCurrentCpu();
//   auto* const pointer = GetPointerAtomic(percpu_data, current_cpu);
//   if (*pointer == 0) {
//     *pointer = lock_value;
//     return current_cpu;
//   }
// }
// return -1;
#if PERCPU_USE_RSEQ_GOTO && defined(__x86_64__)
static inline int RseqFunction_PerCpuTryLock(volatile kernel_rseq* rseq_abi,
                                             Handle percpu_data,
                                             int64_t lock_value) {
  // PerCpuSpinLock::TryLockImpl ensures that fast per-CPU has been initialized
  // before calling this method.
  ABSL_RAW_DCHECK(IsFastNoInit(), "Must be using fast per-CPU");
  uint64_t scratch;
  int64_t cpu;
  asm goto (
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqFunction_PerCpuTryLock, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(cpu)
      "mov %[cpu], %[scratch]\n"
      "shl %[shift], %[scratch]\n"
      "cmpq $0, (%[scratch], %[base])\n"
      "jne %l[fail_contended]\n"
      "mov %[lock_value], (%[scratch], %[base])\n"
      "5:\n"

      // clang-format on
      : [scratch] "=&r"(scratch), [cpu] "=&r"(cpu)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi),
        [base] "r"(percpu_data),
        [lock_value] "r"(lock_value),
        [shift] "n"(PERCPU_BYTES_PER_REGION_SHIFT)
      : PERCPU_RSEQ_CLOBBERS, "memory"
      : fail_contended);
  ABSL_ASSUME(cpu >= kCpuIdInitialized);
  return static_cast<int>(cpu);
fail_contended:
  return -1;
}

#else
// Defined in assembly.
int RseqFunction_PerCpuTryLock(volatile kernel_rseq* rseq_abi,
                               Handle percpu_data, int64_t lock_value);
#endif

// Primitives
}

// Ensure that objects necessary for slow mode have been initialized.
void EnsureSlowModeInitialized();

// Performs both process-wide and per-thread RSEQ initialization.
// Returns true if RSEQ is available.
bool InitThreadCpuId();

// NOTE:  We skirt the usual naming convention slightly above using "_" to
// increase the visibility of functions embedded into the root-namespace (by
// virtue of C linkage) in the supported case.

// Returns current CPU id or, in case of failure, kCpuIdInitialized or less.
// Signal handlers can call this function.
inline int GetCurrentCpuUnsafe() { return RseqCpuId(); }

inline int GetCurrentCpu() {
  int cpu = GetCurrentCpuUnsafe();

  // We open-code the check for fast-cpu availability since we do not want to
  // force initialization in the first-call case.  This so done so that we can
  // use this in places where it may not always be safe to initialize (e.g.
  // pre-InitGoogle) and so that it may serve in the future as a proxy for
  // callers such as CPULogicalId() without introducing an implicit dependence
  // on the fast-path extensions. Initialization is also simply unneeded on some
  // platforms.
  if (ABSL_PREDICT_TRUE(cpu >= kCpuIdInitialized)) {
    return cpu;
  }

#ifdef GOOGLE_HAVE_SCHED_GETCPU
  cpu = sched_getcpu();
  ABSL_RAW_DCHECK(cpu >= 0, "percpu: sched_getcpu() failure");
  return cpu;
#endif  // GOOGLE_HAVE_SCHED_GETCPU

  return -1;
}

inline int GetCurrentVirtualFlatCpu() {
  int cpu = RseqVirtualFlatCpuId();
  if (ABSL_PREDICT_TRUE(cpu >= kCpuIdInitialized)) {
    return cpu;
  }

#if PERCPU_USE_RSEQ
  if (ABSL_PREDICT_FALSE(cpu == kCpuIdUninitialized)) {
    if (ABSL_PREDICT_TRUE(InitThreadCpuId())) {
      return RseqVirtualFlatCpuId();
    }
  }
#endif  // PERCPU_USE_RSEQ

#ifdef GOOGLE_HAVE_SCHED_GETCPU
  return sched_getcpu();
#endif  // GOOGLE_HAVE_SCHED_GETCPU
  return -1;
}

inline bool IsFast() {
#if PERCPU_USE_RSEQ
  // Check if we have already initialized RSEQ for this thread by
  // inspecting the cpuid value, and initialize RSEQ as needed.
  int cpu = RseqCpuId();
  if (ABSL_PREDICT_TRUE(cpu >= kCpuIdInitialized)) {
    return true;
  } else if (ABSL_PREDICT_FALSE(cpu == kCpuIdUnsupported)) {
    return false;
  } else {
    return InitThreadCpuId();
  }
#else   // !PERCPU_USE_RSEQ
  // If we're not supposed to use fast mode, make sure that the
  // slow mode is initialized and bail out here.
  EnsureSlowModeInitialized();
  return false;
#endif  // PERCPU_USE_RSEQ
}

// Slow-path implementations for when RSEQ is not available.  It is *never* safe
// to mix these with the accelerated variants.
FetchAddResult AtomicFetchAddSlow(Handle percpu_data, int64_t delta);
int CompareAndSwapCheckSlow(int target_cpu, std::atomic<int64_t>* p,
                            int64_t old_val, int64_t new_val,
                            std::atomic<int64_t>* check_ptr, int64_t check_val);

inline std::atomic<int64_t>* GetPointerAtomic(Handle percpu_data, int cpu) {
  return reinterpret_cast<std::atomic<int64_t>*>(percpu_data.rep +
                                                 (cpu * kObjectsPerRegion));
}

// A barrier that prevents compiler reordering.
inline void CompilerBarrier() {
#if defined(__GNUC__)
  // Cf. <link>
  __asm__ __volatile__("" : : : "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

// Internal tsan annotations, do not use externally.
// Required as tsan does not natively understand RSEQ.
#ifdef THREAD_SANITIZER
extern "C" {
void __tsan_acquire(void* addr);
void __tsan_release(void* addr);
}
#endif

// TSAN relies on seeing (and rewriting) memory accesses.  It can't
// get at the memory acccesses we make from RSEQ assembler sequences,
// which means it doesn't know about the semantics our sequences
// enforce.  So if we're under TSAN, add barrier annotations.
inline void TSANAcquire(void* p) {
#ifdef THREAD_SANITIZER
  __tsan_acquire(p);
#endif
}

inline void TSANRelease(void* p) {
#ifdef THREAD_SANITIZER
  __tsan_release(p);
#endif
}

inline void TSANMemoryBarrierOn(void* p) {
  TSANAcquire(p);
  TSANRelease(p);
}

namespace percpu_internal {
void FenceFallback();
void FenceUnsafe();
void FenceCpuFallback(int cpu);
void FenceCpuUnsafe(int cpu);
}  // namespace percpu_internal

inline void Fence() {
#if PERCPU_USE_RSEQ
  if (ABSL_PREDICT_TRUE(IsFast())) return percpu_internal::FenceUnsafe();
#endif
  percpu_internal::FenceFallback();
}

inline void FenceCpu(int cpu) {
  // Prevent compiler re-ordering of code below. In particular, the call to
  // GetCurrentCpu must not appear in assembly program order until after any
  // code that comes before FenceCpu in C++ program order.
  CompilerBarrier();

  if (ABSL_PREDICT_TRUE(IsFast())) {
    // A useful fast path: nothing needs doing at all to order us with respect
    // to our own CPU.
    if (GetCurrentCpu() == cpu) return;

#if PERCPU_USE_RSEQ
    return percpu_internal::FenceCpuUnsafe(cpu);
#endif
  }

  percpu_internal::FenceCpuFallback(cpu);
}

#if PERCPU_USE_RSEQ
// These methods may *only* be called if IsFast() has been called by the current
// thread (and it returned true).
inline int CompareAndSwapUnsafe(int target_cpu, std::atomic<int64_t>* p,
                                int64_t old_val, int64_t new_val) {
  TSANMemoryBarrierOn(p);
  return RseqFunction_PerCpuCmpxchg64(RseqAbi(), target_cpu,
                                      atomic_danger::CastToIntegral(p), old_val,
                                      new_val);
}

inline int CompareAndSwapCheckUnsafe(int target_cpu, std::atomic<int64_t>* p,
                                     int64_t old_val, int64_t new_val,
                                     std::atomic<int64_t>* check_ptr,
                                     int64_t check_val) {
  TSANMemoryBarrierOn(p);
  return RseqFunction_PerCpuCmpxchgCheck64(
      RseqAbi(), target_cpu, atomic_danger::CastToIntegral(p), old_val, new_val,
      atomic_danger::CastToIntegral(check_ptr), check_val);
}

inline FetchAddResult AtomicFetchAddUnsafe(Handle percpu_data, int64_t delta) {
  FetchAddResult r =
      RseqFunction_PerCpuAtomicFetchAdd(RseqAbi(), percpu_data, delta);
  return r;
}
#else

inline int CompareAndSwapUnsafe(int target_cpu, std::atomic<int64_t>* p,
                                int64_t old_val, int64_t new_val) {
  return CompareAndSwapCheckSlow(target_cpu, p, old_val, new_val, nullptr, 0);
}

inline int CompareAndSwapCheckUnsafe(int target_cpu, std::atomic<int64_t>* p,
                                     int64_t old_val, int64_t new_val,
                                     std::atomic<int64_t>* check_ptr,
                                     int64_t check_val) {
  return CompareAndSwapCheckSlow(target_cpu, p, old_val, new_val, check_ptr,
                                 check_val);
}

inline FetchAddResult AtomicFetchAddUnsafe(Handle percpu_data, int64_t delta) {
  return AtomicFetchAddSlow(percpu_data, delta);
}

#endif

// Inline implementations of operations exposed by percpu.h
inline int CompareAndSwap(int target_cpu, std::atomic<int64_t>* p,
                          int64_t old_val, int64_t new_val) {
  if (ABSL_PREDICT_TRUE(IsFast())) {
    return CompareAndSwapUnsafe(target_cpu, p, old_val, new_val);
  }
  return CompareAndSwapCheckSlow(target_cpu, p, old_val, new_val, nullptr, 0);
}

inline int CompareAndSwapCheck(int target_cpu, std::atomic<int64_t>* p,
                               int64_t old_val, int64_t new_val,
                               std::atomic<int64_t>* check_ptr,
                               int64_t check_val) {
  if (ABSL_PREDICT_TRUE(IsFast())) {
    return CompareAndSwapCheckUnsafe(target_cpu, p, old_val, new_val, check_ptr,
                                     check_val);
  }
  return CompareAndSwapCheckSlow(target_cpu, p, old_val, new_val, check_ptr,
                                 check_val);
}

inline FetchAddResult AtomicFetchAdd(Handle percpu_data, int64_t delta) {
  if (ABSL_PREDICT_TRUE(IsFast())) {
    return AtomicFetchAddUnsafe(percpu_data, delta);
  }
  return AtomicFetchAddSlow(percpu_data, delta);
}

// Utilities for storing Handles in AtomicWords and the like.
// Our contract: HandleFromInt(IntFromHandle(h)) == h.
inline int64_t IntFromHandle(Handle h) {
  return reinterpret_cast<int64_t>(h.rep);
}

inline Handle HandleFromInt(int64_t i) {
  Handle h;
  h.rep = reinterpret_cast<int64_t*>(i);
  return h;
}

namespace percpu_internal {
// Internal functions, intended for testing only.
bool UsingUpstreamRseqFenceCpu();
void UpstreamRseqFenceCpu(int cpu);
// weakly defined:
bool disable_rseq();
}  // namespace percpu_internal

}  // namespace percpu
}  // namespace subtle
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_PERCPU_H_
