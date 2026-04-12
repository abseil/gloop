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

// A test for the atomic counter operations in atomic_sequence_num.h

#include "gloop/base/atomic_sequence_num.h"

#include <cstdint>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/threadpool.h"

// ----------------------------------------------------
static const int kMaxRefCountThreads = 10;

struct TestContext {  // state used by the tests
  ThreadPool* tp;

  base::SequenceNumber cnt;

  absl::Mutex mu;
  int outstanding;  // number of threads outstanding; under mu

  int64_t expected_final;  // expected final value of cnt

  // We sum (via + and ^) the sequence of numbers returned by
  // GetNext() to ensure that all values are returned.
  base::SequenceNumber::Value expected_xor;
  base::SequenceNumber::Value expected_sum;
};

// Initialize the values in a TestContext.
// L < c->mu
static void InitTestContext(TestContext* c) {
  c->mu.lock();
  c->expected_final = 0;
  c->outstanding = 0;
  c->expected_sum = 0;
  c->expected_xor = 0;
  c->mu.unlock();
}

// Record that a thread has been started that will increment
// the counters by n.
static void AddThread(TestContext* c, int n) {
  c->mu.lock();
  c->expected_final += n;
  c->outstanding++;
  c->mu.unlock();
}

// Get sequence numbers
static void GetSequenceNumbers(TestContext* c, int n) {
  base::SequenceNumber::Value expected_sum = 0;
  base::SequenceNumber::Value expected_xor = 0;
  base::SequenceNumber::Value previous = -1;
  for (int i = 0; i != n; i++) {
    base::SequenceNumber::Value x = c->cnt.GetNext();
    CHECK_LT(previous, x);  // sequence numbers are in order
    expected_sum += x;      // accumulate local sums
    expected_xor ^= x;
    previous = x;
  }
  c->mu.lock();
  c->expected_sum += expected_sum;  // accumulate global sums
  c->expected_xor ^= expected_xor;
  c->outstanding--;  // this thread is finished
  c->mu.unlock();
}

// Return whether c->outstanding is 0, which indicates whether all test threads
// have finished their tasks.
// L >= c->mu
static bool ThreadsFinished(TestContext* c) { return c->outstanding == 0; }

// Test that every time we get a sequence number, we get a higher value than
// the previous one, and that al sequence are given out exactly once.
// L < c->mu
static void TestSequenceNumber(TestContext* c) {
  InitTestContext(c);

  for (int i = 0; i != kMaxRefCountThreads; i++) {
    int n = 10000000;
    AddThread(c, n);
    c->tp->Schedule([c, n] { GetSequenceNumbers(c, n); });
  }
  c->mu.LockWhen(
      absl::Condition(&ThreadsFinished, c));  // wait for threads to finish
  c->mu.unlock();

  CHECK_EQ(c->cnt.GetNext(), c->expected_final);

  // Compute the expected value of the cumulative xor.
  int64_t expected_64_xor = 0;
  for (int64_t i = 0; i != c->expected_final; i++) {
    expected_64_xor ^= i;
  }
  base::SequenceNumber::Value expected_word_xor = expected_64_xor;
  // Compute the expected value of the cumulative sum.
  int64_t expected_64_sum = (c->expected_final * (c->expected_final - 1)) / 2;
  base::SequenceNumber::Value expected_word_sum = expected_64_sum;
  CHECK_EQ(expected_word_sum, c->expected_sum);
  CHECK_EQ(expected_word_xor, c->expected_xor);
}

TEST(AtomicSequenceNumber, SequenceNumber) {
  TestContext context;
  context.tp = new ThreadPool(kMaxRefCountThreads);

  TestSequenceNumber(&context);

  delete context.tp;
}

TEST(AtomicSequenceNumber, SingleThreaded) {
  base::SequenceNumber cnt;
  EXPECT_EQ(cnt.GetNext(), 0);
  EXPECT_EQ(cnt.GetNext(), 1);
  EXPECT_EQ(cnt.GetNext(), 2);
}

TEST(AtomicSequenceNumber, StdThreaded) {
  base::SequenceNumber cnt;
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&cnt] {
      for (int j = 0; j < 10000; ++j) {
        cnt.GetNext();
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(cnt.GetNext(), 100000);
}
