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
#ifndef THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_COUNTER_EMULATED_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_COUNTER_EMULATED_H_

#include <atomic>
#include <cstdint>

namespace concurrent {
namespace percpu {

// This has an API equivalent to the implementation in vcpu_counter_native.inc,
// but it is for platforms that don't support per-CPU operations that the
// native implementation needs.
class VcpuCounter {
 public:
  VcpuCounter() = default;
  VcpuCounter(const VcpuCounter&) = delete;
  VcpuCounter& operator=(const VcpuCounter&) = delete;

  // Note that we don't define the copy-assignment operator or copy constructor
  // because then we'd have to define it in vcpu_counter_native.inc.
  VcpuCounter(VcpuCounter&& rhs) noexcept
      : value_(rhs.value_.exchange(0, std::memory_order_relaxed)) {}
  VcpuCounter& operator=(VcpuCounter&& rhs) noexcept {
    if (this != &rhs) {
      value_.store(rhs.value_.exchange(0, std::memory_order_relaxed),
                   std::memory_order_relaxed);
    }
    return *this;
  }

  void Add(int64_t increment) {
    value_.fetch_add(increment, std::memory_order_relaxed);
  }

  void Clear() { value_.store(0, std::memory_order_relaxed); }

  int64_t value() const { return value_.load(std::memory_order_relaxed); }

  // Returns the sum of all previous calls to Add() for the given CPU.
  // This can be used as part of a loop to iterate over the per-CPU sums along
  // with other counters.
  //
  // On platforms that do not support per-CPU operations, only CPU 0
  // has the sum of previous calls to Add().
  //
  // REQUIRES: cpu >= 0 && < NumCPUs().
  int64_t subtle_value_for_cpu(int cpu) const {
    if (cpu == 0) return value_.load(std::memory_order_relaxed);
    return 0;
  }

 private:
  std::atomic<int64_t> value_{0};
};

}  // namespace percpu
}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_COUNTER_EMULATED_H_
