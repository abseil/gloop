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



// IWYU pragma: private, include "gloop/concurrent/percpu/vcpu_counter.h"
#ifndef THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_COUNTER_NATIVE_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_COUNTER_NATIVE_H_

#include <atomic>
#include <cstdint>
#include <utility>

#include "absl/base/optimization.h"
#include "gloop/base/percpu.h"
#include "gloop/base/percpu_macros.h"
#include "gloop/base/sysinfo.h"

// percpu::VcpuCounter. Exactly the same interface as percpu::Counter, except
// that updates are indexed by virtual CPU (vCPU) ID rather than physical CPU ID
// when RSEQ virtual CPUs are active, so only pages for active vCPUs are faulted
// in.
namespace concurrent {
namespace percpu {

class VcpuCounter {
 public:
  VcpuCounter() : c_(base::subtle::percpu::AllocHandle()) {}
  ~VcpuCounter() { base::subtle::percpu::FreeHandle(c_); }

  VcpuCounter(VcpuCounter&& rhs) noexcept
      : c_(std::exchange(rhs.c_, base::subtle::percpu::NullHandle())) {}
  VcpuCounter& operator=(VcpuCounter&& rhs) noexcept {
    if (this != &rhs) {
      base::subtle::percpu::FreeHandle(c_);
      c_ = std::exchange(rhs.c_, base::subtle::percpu::NullHandle());
    }
    return *this;
  }

  // This scales linearly in the face of concurrent calls; it is thread-safe
  // (counts are never lost due to concurrent calls to Add.)
  //
  // Like base::StatsCounter: This call is suitable for maintaining statistics.
  // It is not suitable for other purposes; in particular, it provides no data
  // synchronization (including visibility of the Add to future calls of value()
  // on other CPUs).
  void Add(int64_t increment) {
    if (ABSL_PREDICT_TRUE(base::subtle::percpu::IsFast())) {
      int64_t scratch;
      int64_t value;
      // clang-format off
      asm volatile(
          PERCPU_RSEQ_PROLOGUE(VcpuCounter_Add, scratch)
          "4:\n"
          PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(scratch)
#if defined(__aarch64__)
          "lsl %[scratch], %[scratch], %c[shift]\n"
          "ldr %[value], [%[handle], %[scratch]]\n"
          "add %[value], %[value], %[delta]\n"
          "str %[value], [%[handle], %[scratch]]\n"
#else
          "shl %[shift], %[scratch]\n"
          "mov (%[handle], %[scratch]), %[value]\n"
          "add %[delta], %[value]\n"
          "mov %[value], (%[handle], %[scratch])\n"
#endif
          "5:\n"
          : [scratch] "=&r"(scratch), [value] "=&r"(value)
          : PERCPU_RSEQ_INPUTS,
            [handle] "r"(c_.rep),
            [delta] "r"(increment),
            [shift] "n"(PERCPU_BYTES_PER_REGION_SHIFT)
          : PERCPU_RSEQ_CLOBBERS, "memory");
      // clang-format on
      return;
    }
    base::subtle::percpu::AtomicFetchAddSlow(c_, increment);
  }

  // Clear must not be called concurrently with any calls to Add().
  // Specifically, concurrent calls to Clear() and Add() can cause corruption of
  // this counter's value.
  //
  // Consider using external locking and ctr.Add(-ctr.value()) as an alternative
  // to calling ctr.Clear().
  void Clear() {
    const int cpus = NumCPUs();
    for (int i = 0; i < cpus; ++i) {
      std::atomic<int64_t>* p = base::subtle::percpu::GetPointerAtomic(c_, i);
      if (p->load(std::memory_order_relaxed) != 0) {
        p->store(0, std::memory_order_relaxed);
      }
    }
  }

  // Returns the current value: sum of previous calls to Add(). Note that in the
  // face of concurrent calls to Add(), value() may see the sum of any *subset*
  // of those concurrent calls.
  int64_t value() const {
    const int cpus = NumCPUs();
    uint64_t total = 0;  // Use unsigned for defined-overflow behavior
    for (int i = 0; i < cpus; ++i) {
      total += subtle_value_for_cpu(i);
    }
    return static_cast<int64_t>(total);
  }

  // Returns the sum of all previous calls to Add() for the supplied `cpu`.
  // This can be used as part of a loop to iterate over the per-CPU sums along
  // with other counters.
  //
  // REQUIRES: cpu >= 0 && < NumCPUs().
  int64_t subtle_value_for_cpu(int cpu) const {
    return base::subtle::percpu::GetPointerAtomic(c_, cpu)->load(
        std::memory_order_relaxed);
  }

  VcpuCounter(const VcpuCounter& rhs) = delete;
  VcpuCounter& operator=(const VcpuCounter& rhs) = delete;

 private:
  base::subtle::percpu::Handle c_;
};

}  // namespace percpu
}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_COUNTER_NATIVE_H_
