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

#include "gloop/util/refcount/blocking_refcount.h"

#include <cstdint>
#include <memory>
#include <thread>  // NOLINT (for std::hardware_concurrency())
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/bind_front.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/atomic_stats_counter.h"
#include "gloop/base/walltime.h"
#include "gtest/gtest.h"

namespace util {
namespace {

using base::StatsCounter;

// Test that BlockingRefcount can be constexpr-initialized.
ABSL_CONST_INIT BlockingRefcount kStaticRefcount(absl::kConstInit);

class BlockingRefcountTest : public testing::Test {
 protected:
  BlockingRefcount counter_;
};

TEST_F(BlockingRefcountTest, BlockingRefcountReferenceMoveConstructible) {
  EXPECT_EQ(0, counter_.count());

  {
    BlockingRefcountReference ref1(&counter_);
    EXPECT_EQ(1, counter_.count());

    BlockingRefcountReference ref2(std::move(ref1));
    EXPECT_EQ(1, counter_.count());
  }

  EXPECT_EQ(0, counter_.count());
}

TEST_F(BlockingRefcountTest, BlockingRefcountReferenceMoveAssignable) {
  EXPECT_EQ(0, counter_.count());

  {
    BlockingRefcountReference ref1(&counter_);
    EXPECT_EQ(1, counter_.count());

    BlockingRefcountReference ref2(&counter_);
    EXPECT_EQ(2, counter_.count());

    ref2 = std::move(ref1);
    EXPECT_EQ(1, counter_.count());
  }

  EXPECT_EQ(0, counter_.count());
}

TEST_F(BlockingRefcountTest, TestBRReferenceAssignment) {
  BlockingRefcount counter, counter2;
  BlockingRefcountReference r1(&counter);
  BlockingRefcountReference r2(&counter2);
  EXPECT_EQ(1, counter.count());
  EXPECT_EQ(1, counter2.count());
  r1 = r2;
  EXPECT_EQ(0, counter.count());
  EXPECT_EQ(2, counter2.count());
}

TEST_F(BlockingRefcountTest, TestSwap) {
  BlockingRefcount counter, counter2;
  counter.IncN(5);
  counter2.IncN(10);
  std::unique_ptr<BlockingRefcountReference> r1 =
      std::make_unique<BlockingRefcountReference>(&counter);
  std::unique_ptr<BlockingRefcountReference> r2 =
      std::make_unique<BlockingRefcountReference>(&counter2);
  EXPECT_EQ(6, counter.count());
  EXPECT_EQ(11, counter2.count());
  r1->swap(*r2);
  // Underlying counters didn't change, just the reference objects.
  EXPECT_EQ(6, counter.count());
  EXPECT_EQ(11, counter2.count());
  r1.reset();
  EXPECT_EQ(6, counter.count());
  EXPECT_EQ(10, counter2.count());
  r2.reset();
  EXPECT_EQ(5, counter.count());
  EXPECT_EQ(10, counter2.count());
}

// This tests that WaitForZero can be re-entered after a previous call failed
// with a timeout.
TEST_F(BlockingRefcountTest, WaitReEntrancy) {
  counter_.Inc();
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
  counter_.Dec();
  EXPECT_TRUE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
}

TEST_F(BlockingRefcountTest, WaitWithTimeoutWorksWhenTimeout) {
  counter_.Inc();
  WallTime start_time = base::ToWallTime(absl::Now());
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(absl::Milliseconds(10)));
  // Only 9ms to handle clock jitter.
  EXPECT_LT(start_time + 0.009, base::ToWallTime(absl::Now()));
}

TEST_F(BlockingRefcountTest, WaitWithTimeoutInDurationWorksWhenTimeout) {
  counter_.Inc();
  const absl::Duration timeout = absl::Milliseconds(10);
  const absl::Time start = absl::Now();
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(timeout));
  const absl::Duration jitter = absl::Milliseconds(1);
  EXPECT_LT(timeout - jitter, absl::Now() - start);
}

TEST_F(BlockingRefcountTest, WaitWithDeadlineWorksWhenTimeout) {
  counter_.Inc();
  const absl::Duration timeout = absl::Milliseconds(10);
  const absl::Time start = absl::Now();
  EXPECT_FALSE(counter_.WaitForZeroWithDeadline(start + timeout));
  const absl::Duration jitter = absl::Milliseconds(1);
  EXPECT_LT(timeout - jitter, absl::Now() - start);
}

// Define a simple blocking reference counter using mutexes to provide a base
// implementation against which to compare other implementations.
//
// For benchmarking we need minimal functionality.
//
// TODO: Unittest this base implementation.

namespace simple {

class BlockingRefcount {
 public:
  BlockingRefcount() : count_(0) {}
  ~BlockingRefcount() = default;

  void Inc() {
    absl::MutexLock ml(mu_);
    count_ += 1;
  }

  void Dec() {
    absl::MutexLock ml(mu_);
    count_ -= 1;
  }

  void WaitForZero() const {
    absl::MutexLock ml(mu_);
    mu_.Await(absl::Condition(this, &BlockingRefcount::is_zero));
  }

 private:
  bool is_zero() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return count_ == 0;
  }

  mutable absl::Mutex mu_;
  int64_t count_ ABSL_GUARDED_BY(mu_);
};

}  // namespace simple

// Measure the raw performance of the underlying primitives.

// Measure increment time.
template <class Q>
void BM_Inc(benchmark::State& state) {
  Q counter;
  for (auto s : state) {
    counter.Inc();
  }
}
BENCHMARK_TEMPLATE(BM_Inc, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_Inc, simple::BlockingRefcount);

// Measure decrement time, when not decrementing to zero.
template <class Q>
void BM_DecToNonZero(benchmark::State& state) {
  Q counter;
  for (int i = 0; i < state.max_iterations; i++) {
    counter.Inc();
  }
  // An extra inc so we don't measure time to dec to zero.
  counter.Inc();
  for (auto s : state) {
    counter.Dec();
  }
}
BENCHMARK_TEMPLATE(BM_DecToNonZero, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_DecToNonZero, simple::BlockingRefcount);

// Benchmark a simple loop of Inc and Dec to zero.
// Subtract off the Inc time to get DecToZero time.
template <class Q>
void BM_IncDecToZero(benchmark::State& state) {
  Q counter;
  for (auto s : state) {
    counter.Inc();
    counter.Dec();
  }
}
BENCHMARK_TEMPLATE(BM_IncDecToZero, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_IncDecToZero, simple::BlockingRefcount);

// Benchmark a simple loop of Wait.
template <class Q>
void BM_Wait(benchmark::State& state) {
  Q counter;
  for (auto s : state) {
    counter.WaitForZero();
  }
}
BENCHMARK_TEMPLATE(BM_Wait, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_Wait, simple::BlockingRefcount);

}  // namespace
}  // namespace util
