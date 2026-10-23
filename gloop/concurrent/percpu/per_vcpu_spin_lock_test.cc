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

#include "gloop/concurrent/percpu/per_vcpu_spin_lock.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gloop/base/percpu.h"
#include "gloop/base/sysinfo.h"
#include "gtest/gtest.h"

namespace concurrent {
namespace percpu {
namespace {

using per_vcpu_internal::LockWord;

struct alignas(64) Slot {
  LockWord lock{0};
};

// Matches Table layout where `fast_capacity` immediately precedes `slots`.
struct TestTable {
  int64_t capacity = 0;
  std::atomic<int64_t> fast_capacity{0};
  std::atomic<Slot*> slots[1];
};

std::unique_ptr<char[]> AllocTestTable(int capacity,
                                       std::vector<Slot>& backing_slots) {
  const size_t bytes =
      sizeof(TestTable) + (capacity - 1) * sizeof(std::atomic<Slot*>);
  auto buf = std::make_unique<char[]>(bytes);
  auto* t = new (buf.get()) TestTable();
  t->capacity = capacity;
  t->fast_capacity.store(capacity, std::memory_order_relaxed);
  for (int i = 0; i < capacity; ++i) {
    t->slots[i].store(&backing_slots[i], std::memory_order_relaxed);
  }
  return buf;
}

TEST(PerVcpuSpinLockTest, AtomicLockAndUnlock) {
  LockWord lock1{0};
  LockWord lock2{0};

  EXPECT_TRUE(per_vcpu_internal::TryLockSlow(lock1));
  EXPECT_EQ(lock1.load(std::memory_order_relaxed), 1u);

  // A second acquire of the same lock must fail while held.
  EXPECT_FALSE(per_vcpu_internal::TryLockSlow(lock1));
  // A different lock is independent.
  EXPECT_TRUE(per_vcpu_internal::TryLockSlow(lock2));

  per_vcpu_internal::UnlockWord(lock1);
  EXPECT_EQ(lock1.load(std::memory_order_relaxed), 0u);
  per_vcpu_internal::UnlockWord(lock2);

  // Re-acquire via LockSlow.
  per_vcpu_internal::LockSlow(lock1);
  EXPECT_EQ(lock1.load(std::memory_order_relaxed), 1u);
  per_vcpu_internal::UnlockWord(lock1);
}

TEST(PerVcpuSpinLockTest, RseqFastPathAcquireAndFallback) {
#if !CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
  GTEST_SKIP() << "RSEQ fast path compiled out (e.g. non-x86/aarch64).";
#else
  if (!::base::subtle::percpu::IsFast()) {
    GTEST_SKIP() << "RSEQ fast path unavailable on this platform.";
  }

  const int capacity = ::NumCPUs() + 64;
  std::vector<Slot> slots(capacity);
  auto table_buf = AllocTestTable(capacity, slots);
  auto* table = reinterpret_cast<TestTable*>(table_buf.get());

  // 1. Successful RSEQ fast-path acquire of current vCPU slot when
  // fast_capacity == capacity.
  Slot* s = nullptr;
  for (int i = 0; i < 100 && s == nullptr; ++i) {
    s = per_vcpu_internal::RseqTryLockVcpuPointerFast(table);
  }
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->lock.load(std::memory_order_relaxed), 1u);

  // While held, another fast-path, slow-path, or TryLockLocalSlot acquire must
  // fail (must not fall back to cross-CPU TryLockSlow when IsFast() is true).
  EXPECT_EQ(per_vcpu_internal::RseqTryLockVcpuPointerFast(table), nullptr);
  EXPECT_EQ(per_vcpu_internal::RseqTryLockVcpuPointerSlow(table), nullptr);
  EXPECT_EQ(per_vcpu_internal::TryLockLocalSlot(table, /*fallback_vcpu=*/0),
            nullptr);

  per_vcpu_internal::UnlockWord(s->lock);
  EXPECT_EQ(s->lock.load(std::memory_order_relaxed), 0u);

  // 2. Setting fast_capacity = 0 (slow mode) forces RseqTryLockVcpuPointerFast
  // to fail, while TryLockLocalSlot succeeds via RseqTryLockVcpuPointerSlow.
  table->fast_capacity.store(0, std::memory_order_relaxed);
  EXPECT_EQ(per_vcpu_internal::RseqTryLockVcpuPointerFast(table), nullptr);

  Slot* slow_s = nullptr;
  for (int i = 0; i < 100 && slow_s == nullptr; ++i) {
    slow_s = per_vcpu_internal::TryLockLocalSlot(table, /*fallback_vcpu=*/0);
  }
  ASSERT_NE(slow_s, nullptr);
  EXPECT_EQ(slow_s->lock.load(std::memory_order_relaxed), 1u);

  // While held, another acquire must fail.
  EXPECT_EQ(per_vcpu_internal::TryLockLocalSlot(table, /*fallback_vcpu=*/0),
            nullptr);
  EXPECT_FALSE(per_vcpu_internal::TryLockSlow(slow_s->lock));

  per_vcpu_internal::UnlockWord(slow_s->lock);
  EXPECT_EQ(slow_s->lock.load(std::memory_order_relaxed), 0u);

  // Restore fast mode (fast_capacity = capacity).
  table->fast_capacity.store(capacity, std::memory_order_release);

  // 3. Out of range capacity falls to nullptr.
  table->capacity = 0;
  table->fast_capacity.store(0, std::memory_order_relaxed);
  EXPECT_EQ(per_vcpu_internal::RseqTryLockVcpuPointerFast(table), nullptr);
  EXPECT_EQ(per_vcpu_internal::RseqTryLockVcpuPointerSlow(table), nullptr);
#endif  // CONCURRENT_PERCPU_HAS_RSEQ_FAST_PATH
}

}  // namespace
}  // namespace percpu
}  // namespace concurrent
