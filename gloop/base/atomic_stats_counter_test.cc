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

// A test for the atomic counter operations in atomic_stats_counter.h

#include "gloop/base/atomic_stats_counter.h"

#include <cstdint>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

static const int kMaxRefCountThreads = 10;

struct TestContext {  // state used by the tests
  AbstractThreadPool* tp;

  base::StatsCounter cnt;  // counter we're testing

  absl::Mutex mu;
  int outstanding;         // number of threads outstanding; under mu
  int64_t expected_final;  // expected final value of cnt
};

// Initialize the values in a TestContext.
// L < c->mu
static void InitTestContext(TestContext* c) {
  c->mu.lock();
  c->expected_final = 0;
  c->outstanding = 0;
  c->mu.unlock();
  c->cnt.Clear();
}

// Record that a thread has been started that will increment
// the counters by n.
static void AddThread(TestContext* c, int n) {
  c->expected_final += n;
  c->mu.lock();
  c->outstanding++;
  c->mu.unlock();
}

// Increment c->cnt n times
static void IncCounters(TestContext* c, int n) {
  for (int i = 0; i != n; i++) {
    c->cnt.Add(1);
  }
  c->mu.lock();
  c->outstanding--;  // this thread is finished
  c->mu.unlock();
}

// Return whether c->outstanding is 0, which indicates whether all test threads
// have finished their tasks.
// L >= c->mu
static bool ThreadsFinished(TestContext* c) { return c->outstanding == 0; }

// Test that the accumulated sum of all the atomic increments reaches
// the right value, despite concurrency.
// L < c->mu
static void TestStatsCounterAdd(TestContext* c) {
  InitTestContext(c);
  for (int i = 0; i != kMaxRefCountThreads; i++) {
    int n = 10000000;
    AddThread(c, n);
    c->tp->Schedule([c, n] { IncCounters(c, n); });
  }
  c->mu.LockWhen(
      absl::Condition(&ThreadsFinished, c));  // wait for threads to finish
  c->mu.unlock();

  CHECK_EQ(c->cnt.value(), c->expected_final);

  // check that increment need not be 1
  c->cnt.Add(7);
  CHECK_EQ(c->cnt.value(), c->expected_final + 7);
}

// Increment c->cnt n times lossily.
static void LossyIncCounters(TestContext* c, int n) {
  for (int i = 0; i != n; i++) {
    c->cnt.LossyAdd(1);
  }
  c->mu.lock();
  c->outstanding--;  // this thread is finished
  c->mu.unlock();
}

// Test that the accumulated sum of all the atomic increments reaches
// close to the right value, despite concurrency.
// L < c->mu
static void TestLossyStatsCounterAdd(TestContext* c) {
  InitTestContext(c);
  for (int i = 0; i != kMaxRefCountThreads / 2; i++) {
    int n = 10000000;
    AddThread(c, n);
    c->tp->Schedule([c, n] { LossyIncCounters(c, n); });
  }
  c->mu.LockWhen(
      absl::Condition(&ThreadsFinished, c));  // wait for threads to finish
  c->mu.unlock();

  // Guess that we won't lose more than 7/8th of the counts.
  CHECK_LE(c->expected_final / 8, c->cnt.value());
  CHECK_LE(c->cnt.value(), c->expected_final);

  // check that increment need not be 1
  int64_t end_value = c->cnt.value();
  c->cnt.LossyAdd(7);
  CHECK_EQ(c->cnt.value(), end_value + 7);
}

TEST(AtomicStatsCounter, StatsCounter) {
  TestContext context;
  context.tp = new ThreadPool(kMaxRefCountThreads);

  TestStatsCounterAdd(&context);
  TestLossyStatsCounterAdd(&context);

  delete context.tp;
}
