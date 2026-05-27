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

#include "gloop/base/percpu_macros.h"

#include <stddef.h>
#include <stdint.h>

#include "gloop/base/percpu.h"

#if PERCPU_USE_RSEQ

#include <iostream>
#include <thread>  // NOLINT
#include <vector>

#include "absl/base/internal/sysinfo.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using base::subtle::percpu::GetRseqVcpuMode;
using base::subtle::percpu::RseqVcpuMode;
using testing::Eq;
using testing::Ge;
using testing::Lt;

TEST(PercpuMacrosTest, CpuIds) {
  bool is_fast = base::subtle::percpu::IsFast();
  int64_t cpu_id;
  int64_t vcpu_id;
  int64_t flat_cpu_id;

  // clang-format off
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(PercpuTestBasics, cpu_id)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(cpu_id)
      PERCPU_RSEQ_LOAD_VCPU_ID(vcpu_id)
      PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(flat_cpu_id)

      "5:\n"

      : [cpu_id] "=&r"(cpu_id),
        [vcpu_id] "=&r"(vcpu_id),
        [flat_cpu_id] "=&r"(flat_cpu_id)
      : PERCPU_RSEQ_INPUTS
      : PERCPU_RSEQ_CLOBBERS);
  // clang-format on

  std::cout << "cpu id = " << cpu_id << ", flat cpu id = " << flat_cpu_id
            << ", vcpu mode / vcpu id = "
            << static_cast<int>(base::subtle::percpu::GetRseqVcpuMode())
            << " / " << vcpu_id << "\n";

  if (is_fast) {
    EXPECT_THAT(cpu_id, Ge(0));
    EXPECT_THAT(cpu_id, Lt(absl::base_internal::NumCPUs()));
    EXPECT_THAT(flat_cpu_id, Ge(0));
    EXPECT_THAT(flat_cpu_id, Lt(absl::base_internal::NumCPUs()));
    switch (GetRseqVcpuMode()) {
      case RseqVcpuMode::kFlat:
      case RseqVcpuMode::kFlatPerL3:
        EXPECT_THAT(vcpu_id, Ge(0));
        EXPECT_THAT(vcpu_id, Lt(absl::base_internal::NumCPUs()));
        EXPECT_THAT(flat_cpu_id, Eq(vcpu_id));
        break;
      case RseqVcpuMode::kMM:
        EXPECT_EQ(vcpu_id, -1);
        EXPECT_THAT(flat_cpu_id, Ge(0));
        EXPECT_THAT(flat_cpu_id, Lt(absl::base_internal::NumCPUs()));
        break;
      case RseqVcpuMode::kNone:
        EXPECT_THAT(flat_cpu_id, Eq(cpu_id));
        break;
    }
  } else {
    EXPECT_THAT(cpu_id, Eq(base::subtle::percpu::kCpuIdUnsupported));
    EXPECT_THAT(flat_cpu_id, Eq(base::subtle::percpu::kCpuIdUnsupported));
  }
}

void CounterAdd(base::subtle::percpu::Handle handle, int64_t delta) {
  int64_t value;
  int64_t scratch;

  // clang-format off
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(PercpuTestCounterAdd, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_CPU_ID(scratch)

      // *GetPointerAtomic(handle, scratch) += delta
#if defined(__aarch64__)
      "lsl %[scratch], %[scratch], %c[shift]\n"
      "ldr %[value], [%[handle], %[scratch]]\n"
      "add %[value], %[value], %[delta]\n"
      "str %[value], [%[handle], %[scratch]]\n"
#else
      "shl  %[shift], %[scratch]\n"
      "mov  (%[handle], %[scratch]), %[value]\n"
      "add  %[delta], %[value]\n"
      "mov  %[value], (%[handle], %[scratch])\n"
#endif
      "5:\n"

      : [scratch] "=&r"(scratch), [value] "=&r"(value)
      : PERCPU_RSEQ_INPUTS_P(base::subtle::percpu::RseqAbi()),
        [handle] "r"(handle),
        [delta] "r"(delta),
        [shift] "n"(PERCPU_BYTES_PER_REGION_SHIFT)
      : PERCPU_RSEQ_CLOBBERS);
  // clang-format on
}

void CounterAddMaybeFlat(base::subtle::percpu::Handle handle, int64_t delta) {
  int64_t value;
  int64_t scratch;

  // clang-format off
  asm volatile(
      PERCPU_RSEQ_PROLOGUE(PercpuTestCounterAddMaybeFlat, scratch)

      "4:\n"
      PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(scratch)

      // *GetPointerAtomic(handle, scratch) += delta
#if defined(__aarch64__)
      "lsl %[scratch], %[scratch], %c[shift]\n"
      "ldr %[value], [%[handle], %[scratch]]\n"
      "add %[value], %[value], %[delta]\n"
      "str %[value], [%[handle], %[scratch]]\n"
#else
      "shl  %[shift], %[scratch]\n"
      "mov  (%[handle], %[scratch]), %[value]\n"
      "add  %[delta], %[value]\n"
      "mov  %[value], (%[handle], %[scratch])\n"
#endif
      "5:\n"

      : [scratch] "=&r"(scratch), [value] "=&r"(value)
      : PERCPU_RSEQ_INPUTS,
        [handle] "r"(handle),
        [delta] "r"(delta),
        [shift] "n"(PERCPU_BYTES_PER_REGION_SHIFT)
      : PERCPU_RSEQ_CLOBBERS);
  // clang-format on
}

TEST(PercpuMacrosTest, CounterAdd) {
  if (!base::subtle::percpu::IsFast()) GTEST_SKIP() << "No rseq";

  base::subtle::percpu::Handle handle = base::subtle::percpu::AllocHandle();
  std::vector<std::thread> threads;
  for (int delta : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
    threads.emplace_back([&, delta] {
      base::subtle::percpu::IsFast();
      for (int i = 0; i < 1000; ++i) {
        CounterAdd(handle, delta);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  int64_t total = 0;
  for (int i = 0; i < absl::base_internal::NumCPUs(); ++i) {
    int64_t value = *base::subtle::percpu::GetPointerAtomic(handle, i);
    if (value) std::cout << "cpu " << i << " = " << value << "\n";
    total += value;
  }
  EXPECT_THAT(total, Eq(511 * 1000));
}

TEST(PercpuMacrosTest, CounterAddMaybeFlat) {
  if (!base::subtle::percpu::IsFast()) GTEST_SKIP() << "No rseq";

  base::subtle::percpu::Handle handle = base::subtle::percpu::AllocHandle();
  std::vector<std::thread> threads;
  for (int delta : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
    threads.emplace_back([&, delta] {
      base::subtle::percpu::IsFast();
      for (int i = 0; i < 1000; ++i) {
        CounterAddMaybeFlat(handle, delta);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  int64_t total = 0;
  for (int i = 0; i < absl::base_internal::NumCPUs(); ++i) {
    int64_t value = *base::subtle::percpu::GetPointerAtomic(handle, i);
    if (value) std::cout << "cpu " << i << " = " << value << "\n";
    total += value;
  }
  EXPECT_THAT(total, Eq(511 * 1000));
  base::subtle::percpu::FreeHandle(handle);
}

}  // namespace
#endif  // PERCPU_USE_RSEQ
