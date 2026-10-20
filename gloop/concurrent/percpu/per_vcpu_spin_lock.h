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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_PER_VCPU_SPIN_LOCK_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_PER_VCPU_SPIN_LOCK_H_

// This file defines low-level per-virtual-CPU spin lock primitives used to
// build flat-vCPU-sharded data structures such as
// concurrent::VcpuStripedObject.
//
// Motivation:
// -----------
// The legacy base::subtle::percpu::PerCpuSpinLock shards lock words by
// *physical* CPU id (up to NumCPUs(), which is 128-256+ on modern servers) and
// backs them with a global mmap handle pool (PERCPU_BYTES_PER_REGION == 4096
// bytes/CPU). While tolerable for a handful of process-global structures,
// physical CPU sharding wastes memory when a container's CPU quota is smaller
// than the host core count, and wastes an enormous amount of memory when
// applied to many fine-grained objects (per-table counters, streamz
// distributions, localized caches).
//
// Under the flat virtual CPU scheme, a container is assigned a *dense* range
// of virtual CPU ids [0, quota). A 4-vCPU container only ever observes flat
// vCPUs 0..3. This lets us shard by flat vCPU using a compact array indexed by
// a small, dense id, and lazily grow that array only as high-numbered vCPUs
// actually touch the structure ("tiered promotion").
//
// Design:
// -------
// This header provides low-level RSEQ assembly and atomic CAS helpers for
// per-vCPU slots. Callers embed a 4-byte `LockWord` (`uint32_t lock`: 0 =
// free, 1 = held) at offset 0 of each Slot alongside the sharded value, so
// acquiring the lock warms the value's cache line.
//
// Callers maintain an array of `Slot*` pointers indexed by flat vCPU ID up to
// `capacity`, paired with a table-level `fast_capacity` atomic word:
//   - In fast mode (no active sweep), `fast_capacity == capacity`.
//   - In slow mode (one or more active sweeps), `fast_capacity == 0`.
// All entries `slots[0 .. capacity - 1]` must be non-null initialized slots,
// as the RSEQ fast path avoids null checks for speed.
//
// Protocol:
// ---------
// There are two ways to acquire a slot:
//
//   1. The RSEQ fast path (RseqTryLockVcpuPointerFast), available only to the
//      thread running on the slot's own vCPU, and only while
//      `vcpu < fast_capacity`. Inside the RSEQ critical section, the thread
//      loads `fast_capacity`, checks `vcpu < fast_capacity`, loads
//      `slot = slots[vcpu]`, checks `slot->lock == 0`, and commits a plain
//      store `slot->lock = 1`. When `fast_capacity == 0` (slow mode), the
//      unsigned bounds check (`vcpu < fast_capacity`) fails in a single
//      comparison.
//   2. An atomic 32-bit CAS on `slot->lock` (0 -> 1):
//        - A local mutator whose fast path fails (because `fast_capacity == 0`
//          or `lock != 0`) executes this 32-bit CAS *as the commit
//          instruction of an RSEQ critical section on the slot's own vCPU*
//          (RseqTryLockVcpuPointerSlow).
//        - A sweep (ForEach) sets `fast_capacity = 0`, executes a global
//          Fence() (sys_membarrier), and then acquires each slot via 32-bit
//          atomic CAS (LockSlow).
//
// The protocol prevents (2) from racing (1):
//   - For a remote sweep (ForEach): a fast-path section on another CPU that has
//     already passed its `vcpu < fast_capacity` check and is about to commit
//     `lock = 1` cannot be aborted by modifying `fast_capacity` or `lock`
//     alone. Thus a sweep first sets `fast_capacity = 0` to prevent *new*
//     fast-path commits, and then executes a global Fence() (sys_membarrier)
//     to preempt/abort any fast-path RSEQ section already in flight before
//     calling LockSlow.
//   - For a local mutator (RseqTryLockVcpuPointerSlow): because the mutator's
//     32-bit CAS is the commit instruction of an RSEQ critical section on the
//     slot's own vCPU, any preemption or CPU migration before the CAS retires
//     aborts the critical section. At the instant the CAS executes, the thread
//     is guaranteed by the kernel to be running on the slot's vCPU. Since at
//     most one thread can run on a vCPU at any instant, no concurrent fast-path
//     section can be executing on that vCPU, and any prior thread on that vCPU
//     was either aborted by the kernel RSEQ handler upon preemption or already
//     retired its commit store. Consequently, local mutators do NOT need to
//     wait for a sweep's Fence() to complete before acquiring via
//     RseqTryLockVcpuPointerSlow -- avoiding any mutator stall during
//     sys_membarrier IPIs.
//
// The protocol is model-checked in TLA+ under concurrent/percpu/tla/.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/percpu.h"
#include "gloop/base/percpu_macros.h"

struct kernel_rseq;

namespace concurrent {
namespace percpu {
namespace per_vcpu_internal {

// 4-byte lock word embedded at byte offset 0 of each per-vCPU Slot (0 = free,
// 1 = held).
using LockWord = std::atomic<uint32_t>;
static_assert(sizeof(LockWord) == sizeof(uint32_t));
static_assert(LockWord::is_always_lock_free,
              "the slow-path acquire needs a lock-free 32-bit atomic so that "
              "it composes with the 32-bit RSEQ/UnlockWord accesses");

// True if the platform supports the RSEQ assembly fast path.
#if PERCPU_USE_RSEQ_GOTO && (defined(__x86_64__) || defined(__aarch64__))
#define CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH 1
#else
#define CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH 0
#endif

// Attempts a non-atomic RSEQ acquire of the 32-bit lock embedded at offset 0
// of the Slot pointer at `table->slots[vcpu]`. Returns the acquired `Slot*`
// on success, or nullptr on failure (contended, slow mode, out-of-range, or
// RSEQ unavailable).
//
// `Table` must be a standard-layout struct containing:
//   - `fast_capacity`: 64-bit integer/atomic immediately preceding `slots`
//     (at byte offset -8 from `slots[0]`). When no sweep is active,
//     `fast_capacity == table->capacity`; when a sweep is active (slow mode),
//     `fast_capacity == 0`, causing the unsigned bounds check
//     (`vcpu >= fast_capacity`) to fail in a single instruction.
//   - `slots`: array of `std::atomic<Slot*>` pointers, where each `Slot`
//     embeds `LockWord` at byte offset 0.
template <typename Table, typename Slot = std::remove_pointer_t<
                              decltype(std::declval<Table>().slots[0].load())>>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline Slot* RseqTryLockVcpuPointerFast(
    Table* table) {
  static_assert(offsetof(Table, slots) - offsetof(Table, fast_capacity) == 8,
                "Table::fast_capacity must be at byte offset -8 from slots[0]");
  static_assert(offsetof(Slot, lock) == 0,
                "Slot::lock must be at byte offset 0 of Slot");

#if CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  if (ABSL_PREDICT_FALSE(!::base::subtle::percpu::IsFastNoInit())) {
    return nullptr;
  }
  volatile ::kernel_rseq* rseq_abi = ::base::subtle::percpu::RseqAbi();
  void* slots_base = &table->slots[0];
#if defined(__x86_64__)
  uint64_t scratch;
  int64_t vcpu;
  uint64_t slot_ptr;
  asm goto(
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqTryLockVcpuPointerFast, scratch)

      "4:\n"
      // Load fast_capacity before the 3-instruction vcpu ID load chain so
      // their L1D load latencies overlap completely on out-of-order cores.
      "movq -8(%[base]), %[scratch]\n"
      PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(vcpu)
      "cmpq %[scratch], %[vcpu]\n"
      "jae %l[fail_contended]\n"
      // slot_ptr = slots_base[vcpu]
      "movq (%[base], %[vcpu], 8), %[slot_ptr]\n"
      // Dereference slot_ptr to check slot_ptr->lock == 0 (at byte offset 0).
      "cmpl $0, (%[slot_ptr])\n"
      "jne %l[fail_contended]\n"
      // Commit instruction: slot_ptr->lock = 1.
      "movl $1, (%[slot_ptr])\n"
      "5:\n"
      // clang-format on
      : [scratch] "=&r"(scratch), [vcpu] "=&r"(vcpu),
        [slot_ptr] "=&r"(slot_ptr)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi), [base] "r"(slots_base)
      : PERCPU_RSEQ_CLOBBERS, "memory"
      : fail_contended);
  // TSan cannot instrument the non-atomic RSEQ asm store, so manually annotate
  // the acquire to pair with UnlockWord's release store.
  ::base::subtle::percpu::TSANAcquire(reinterpret_cast<void*>(slot_ptr));
  ABSL_ASSUME(slot_ptr != 0);
  return reinterpret_cast<Slot*>(slot_ptr);
#elif defined(__aarch64__)
  uint64_t scratch;
  int64_t vcpu;
  uint64_t slot_ptr;
  uint32_t lock_word;
  asm goto(
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqTryLockVcpuPointerFast, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(vcpu)
      // Load fast_capacity from offset -8 before slots_base with acquire
      // semantics so ARM cannot reorder the fast_capacity check after subsequent
      // slot loads.
      "sub %[scratch], %[base], #8\n"
#ifdef __ARM_FEATURE_RCPC
      "ldapr %[scratch], [%[scratch]]\n"
#else
      "ldar %[scratch], [%[scratch]]\n"
#endif
      "cmp %[vcpu], %[scratch]\n"
      "b.hs %l[fail_contended]\n"
      // slot_ptr = slots_base[vcpu] (64-bit pointer array, ldr with lsl #3)
      "ldr %[slot_ptr], [%[base], %[vcpu], lsl #3]\n"
      // Dereference slot_ptr to load slot_ptr->lock (at byte offset 0).
#ifdef __ARM_FEATURE_RCPC
      "ldapr %w[lock_word], [%[slot_ptr]]\n"
#else
      "ldar %w[lock_word], [%[slot_ptr]]\n"
#endif
      "cbnz %w[lock_word], %l[fail_contended]\n"
      // Commit instruction: slot_ptr->lock = 1.
      "str %w[one], [%[slot_ptr]]\n"
      "5:\n"
      // clang-format on
      : [scratch] "=&r"(scratch), [vcpu] "=&r"(vcpu),
        [slot_ptr] "=&r"(slot_ptr), [lock_word] "=&r"(lock_word)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi), [base] "r"(slots_base), [one] "r"(1)
      : PERCPU_RSEQ_CLOBBERS, "memory"
      : fail_contended);
  // TSan cannot instrument the non-atomic RSEQ asm store, so manually annotate
  // the acquire to pair with UnlockWord's release store.
  ::base::subtle::percpu::TSANAcquire(reinterpret_cast<void*>(slot_ptr));
  ABSL_ASSUME(slot_ptr != 0);
  return reinterpret_cast<Slot*>(slot_ptr);
#endif
fail_contended:
#endif  // CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  return nullptr;
}

// Attempts an atomic 32-bit CAS acquire of the LockWord embedded at offset 0 of
// the Slot pointer at `table->slots[vcpu]` *as the commit instruction of an
// RSEQ critical section on `vcpu`*. Returns the acquired `Slot*` on success, or
// nullptr on failure (contended, out-of-range, RSEQ abort, or RSEQ
// unavailable).
template <typename Table, typename Slot = std::remove_pointer_t<
                              decltype(std::declval<Table>().slots[0].load())>>
ABSL_ATTRIBUTE_NOINLINE inline Slot* RseqTryLockVcpuPointerSlow(Table* table) {
  static_assert(offsetof(Slot, lock) == 0,
                "Slot::lock must be at byte offset 0 of Slot");

#if CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  if (ABSL_PREDICT_FALSE(!::base::subtle::percpu::IsFast())) {
    return nullptr;
  }
  volatile ::kernel_rseq* rseq_abi = ::base::subtle::percpu::RseqAbi();
  void* slots_base = &table->slots[0];
  const int64_t capacity = table->capacity;
#if defined(__x86_64__)
  uint64_t scratch;
  int64_t vcpu;
  uint64_t slot_ptr;
  uint32_t expected;
  asm goto(
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqTryLockVcpuPointerSlow, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(vcpu)
      "cmp %[capacity], %[vcpu]\n"
      "jae %l[fail_contended]\n"
      // slot_ptr = slots_base[vcpu]
      "mov (%[base], %[vcpu], 8), %[slot_ptr]\n"
      "xorl %[expected], %[expected]\n"
      "movl $1, %k[scratch]\n"
      // Commit instruction (label `5:`): because the 32-bit CAS (0 -> 1) is
      // semantically a memory write, it is placed immediately before `5:`.
      // Retiring advances IP to `5:` (exiting the RSEQ critical section so
      // preemptions no longer redirect to `abort_ip`) with the outcome in
      // EFLAGS.ZF, which is preserved across context switches and tested by
      // `jnz` below.
      "lock cmpxchgl %k[scratch], (%[slot_ptr])\n"
      "5:\n"
      "jnz %l[fail_contended]\n"
      // clang-format on
      : [scratch] "=&r"(scratch), [vcpu] "=&r"(vcpu),
        [slot_ptr] "=&r"(slot_ptr), [expected] "=&a"(expected)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi), [base] "r"(slots_base),
        [capacity] "r"(capacity)
      : PERCPU_RSEQ_CLOBBERS, "memory"
      : fail_contended);
  // TSan cannot instrument inline asm, so manually annotate acquire.
  ::base::subtle::percpu::TSANAcquire(reinterpret_cast<void*>(slot_ptr));
  ABSL_ASSUME(slot_ptr != 0);
  return reinterpret_cast<Slot*>(slot_ptr);
#elif defined(__aarch64__)
  uint64_t scratch;
  int64_t vcpu;
  uint64_t slot_ptr;
  uint32_t status;
  asm goto(
      // clang-format off
      PERCPU_RSEQ_PROLOGUE(RseqTryLockVcpuPointerSlow, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(vcpu)
      "cmp %[vcpu], %[capacity]\n"
      "b.hs %l[fail_contended]\n"
      // slot_ptr = slots_base[vcpu]
      "ldr %[slot_ptr], [%[base], %[vcpu], lsl #3]\n"
      "ldaxr %w[scratch], [%[slot_ptr]]\n"
      "cbnz %w[scratch], %l[fail_contended]\n"
      "mov %w[scratch], #1\n"
      // Commit instruction (label `5:`): because the store-exclusive is
      // semantically a memory write, it is placed immediately before `5:`.
      // Retiring advances PC to `5:` (exiting the RSEQ critical section) with
      // the outcome in `%w[status]` (0 on success), preserved across context
      // switches and tested by `cbnz` below.
      "stxr %w[status], %w[scratch], [%[slot_ptr]]\n"
      "5:\n"
      "cbnz %w[status], %l[fail_contended]\n"
      // clang-format on
      : [scratch] "=&r"(scratch), [vcpu] "=&r"(vcpu),
        [slot_ptr] "=&r"(slot_ptr), [status] "=&r"(status)
      : PERCPU_RSEQ_INPUTS_P(rseq_abi), [base] "r"(slots_base),
        [capacity] "r"(capacity)
      : PERCPU_RSEQ_CLOBBERS, "memory"
      : fail_contended);
  // TSan cannot instrument inline asm, so manually annotate acquire.
  ::base::subtle::percpu::TSANAcquire(reinterpret_cast<void*>(slot_ptr));
  ABSL_ASSUME(slot_ptr != 0);
  return reinterpret_cast<Slot*>(slot_ptr);
#endif
fail_contended:
#endif  // CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  return nullptr;
}

// Issues a global RSEQ memory barrier (sys_membarrier) if RSEQ fast-path is
// active on this platform. Used by sweeps after setting `fast_capacity = 0` to
// abort/drain any fast-path RSEQ critical section already in flight.
inline void FenceFastPath() {
#if CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  if (::base::subtle::percpu::IsFast()) {
    ::base::subtle::percpu::Fence();
  }
#endif
}

// Exponential backoff for the (rare) contended acquire spin loop. Contention
// here is a slot held across a preemption or being visited by a sweep, so an
// approximate, dependency-free backoff is sufficient. It goes straight to
// nanosleep: a spin-with-pause prelude measures worse under tight-loop sweeps
// (mutations over 1us rise from ~0.5% to ~4.5%) and neutral under paced ones.
inline void LockDelay(int iteration) {
  const int shift = std::min(iteration, 20);
  absl::SleepFor(absl::Nanoseconds(int64_t{1} << shift));
}

// Non-blocking acquire of `lock` by atomic CAS, transitioning `0 -> 1`.
[[nodiscard]] inline bool TryLockSlow(LockWord& lock) {
  uint32_t expected = 0;
  if (lock.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                   std::memory_order_relaxed)) {
    ::base::subtle::percpu::TSANAcquire(&lock);
    return true;
  }
  return false;
}

// Returns the current virtual flat CPU ID (initializing RSEQ for the process
// and thread if needed), or the physical CPU ID (clamped to >= 0) if RSEQ is
// unavailable.
//
// Registration is checked via `IsFast()`, which reads `cpu_id`: the kernel only
// guarantees `cpu_id == -1` for an unregistered thread, and leaves `mm_cid` at
// 0 after unregistration, so the vCPU field alone cannot be trusted to detect
// registration.
inline int CurrentVcpu() {
  if (ABSL_PREDICT_TRUE(::base::subtle::percpu::IsFast())) {
    return ::base::subtle::percpu::RseqVirtualFlatCpuId();
  }
  const int v = ::base::subtle::percpu::GetCurrentCpu();
  return v < 0 ? 0 : v;
}

// Non-blocking acquire of the slot for the current vCPU on `table`:
//   - When RSEQ fast path is active (`IsFast()`), attempts
//     `RseqTryLockVcpuPointerFast` followed by `RseqTryLockVcpuPointerSlow`. If
//     both fail (e.g. contended or RSEQ abort), returns nullptr so the caller
//     spins/retries without racing cross-CPU atomic CAS against a fast-path
//     RSEQ store.
//   - When RSEQ is unavailable process-wide (`!IsFast()`), falls back to a
//     plain atomic CAS (`TryLockSlow`) on `fallback_vcpu`.
template <typename Table, typename Slot = std::remove_pointer_t<
                              decltype(std::declval<Table>().slots[0].load())>>
inline Slot* TryLockLocalSlot(Table* table, int fallback_vcpu) {
#if CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  if (ABSL_PREDICT_TRUE(::base::subtle::percpu::IsFast())) {
    if (Slot* s = RseqTryLockVcpuPointerFast(table);
        ABSL_PREDICT_TRUE(s != nullptr)) {
      return s;
    }
    return RseqTryLockVcpuPointerSlow(table);
  }
#endif
  Slot* s = table->slots[fallback_vcpu].load(std::memory_order_acquire);
  return TryLockSlow(s->lock) ? s : nullptr;
}

// Blocking atomic CAS acquire of `lock`, with exponential backoff.
inline void LockSlow(LockWord& lock) {
  if (ABSL_PREDICT_TRUE(TryLockSlow(lock))) {
    return;
  }
  int iteration = 1;
  do {
    LockDelay(iteration++);
  } while (!TryLockSlow(lock));
}

// Releases `lock`. Safe regardless of how the lock was acquired: the holder has
// exclusive ownership, so a single release store suffices and no RSEQ critical
// section is required.
inline void UnlockWord(LockWord& lock) {
  ::base::subtle::percpu::TSANRelease(&lock);
  lock.store(0, std::memory_order_release);
}

}  // namespace per_vcpu_internal
}  // namespace percpu
}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_PER_VCPU_SPIN_LOCK_H_
