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

// A test for the atomic counter operations in atomic_stats_counter.h

#include "gloop/base/atomic_stats_counter.h"

#include <cstdint>
#include <memory>

#include "absl/synchronization/mutex.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace {

constexpr int kMaxRefCountThreads = 10;

struct TestContext {  // state used by the tests
  explicit TestContext(int num_threads = kMaxRefCountThreads)
      : tp(std::make_unique<ThreadPool>(num_threads)) {}

  std::unique_ptr<ThreadPool> tp;

  base::StatsCounter cnt;  // counter we're testing

  absl::Mutex mu;
  int outstanding = 0;         // number of threads outstanding; under mu
  int64_t expected_final = 0;  // expected final value of cnt
};

// Record that a thread has been started that will increment
// the counters by n.
void AddThread(TestContext* c, int n) {
  c->expected_final += n;
  absl::MutexLock l(c->mu);
  c->outstanding++;
}

// Increment c->cnt n times
void IncCounters(TestContext* c, int n) {
  for (int i = 0; i != n; i++) {
    c->cnt.Add(1);
  }
  absl::MutexLock l(c->mu);
  c->outstanding--;  // this thread is finished
}

// Increment c->cnt n times lossily.
void LossyIncCounters(TestContext* c, int n) {
  for (int i = 0; i != n; i++) {
    c->cnt.LossyAdd(1);
  }
  absl::MutexLock l(c->mu);
  c->outstanding--;  // this thread is finished
}

// Return whether c->outstanding is 0, which indicates whether all test threads
// have finished their tasks.
// L >= c->mu
bool ThreadsFinished(TestContext* c) { return c->outstanding == 0; }

// Test that the accumulated sum of all the atomic increments reaches
// the right value, despite concurrency.
TEST(AtomicStatsCounter, StatsCounterAdd) {
  TestContext context;
  for (int i = 0; i != kMaxRefCountThreads; i++) {
    int n = 10000000;
    AddThread(&context, n);
    context.tp->Schedule([&context, n] { IncCounters(&context, n); });
  }
  // wait for threads to finish
  context.mu.LockWhen(absl::Condition(&ThreadsFinished, &context));
  context.mu.unlock();

  EXPECT_EQ(context.cnt.value(), context.expected_final);

  // check that increment need not be 1
  context.cnt.Add(7);
  EXPECT_EQ(context.cnt.value(), context.expected_final + 7);
}

// Test that the accumulated sum of all the atomic increments reaches
// close to the right value, despite concurrency.
TEST(AtomicStatsCounter, LossyStatsCounterAdd) {
  TestContext context;
  for (int i = 0; i != kMaxRefCountThreads / 2; i++) {
    int n = 10000000;
    AddThread(&context, n);
    context.tp->Schedule([&context, n] { LossyIncCounters(&context, n); });
  }
  // wait for threads to finish
  context.mu.LockWhen(absl::Condition(&ThreadsFinished, &context));
  context.mu.unlock();

  // Guess that we won't lose more than 7/8th of the counts.
  EXPECT_GE(context.cnt.value(), context.expected_final / 8);
  EXPECT_LE(context.cnt.value(), context.expected_final);

  // check that increment need not be 1
  const int64_t end_value = context.cnt.value();
  context.cnt.LossyAdd(7);
  EXPECT_EQ(context.cnt.value(), end_value + 7);
}

TEST(AtomicStatsCounter, Clear) {
  base::StatsCounter counter;
  EXPECT_EQ(counter.value(), 0);
  counter.Add(42);
  EXPECT_EQ(counter.value(), 42);
  counter.Clear();
  EXPECT_EQ(counter.value(), 0);
}

}  // namespace
