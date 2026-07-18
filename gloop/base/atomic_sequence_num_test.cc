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

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace base {
namespace {

// ----------------------------------------------------
constexpr int kMaxRefCountThreads = 10;

struct TestContext {  // state used by the tests
  ThreadPool* tp = nullptr;

  SequenceNumber cnt;

  absl::Mutex mu;
  // Number of threads outstanding; under mu.
  int outstanding ABSL_GUARDED_BY(mu) = 0;

  // Expected final value of cnt.
  int64_t expected_final ABSL_GUARDED_BY(mu) = 0;

  // We sum (via + and ^) the sequence of numbers returned by
  // GetNext() to ensure that all values are returned.
  SequenceNumber::Value expected_xor ABSL_GUARDED_BY(mu) = 0;
  SequenceNumber::Value expected_sum ABSL_GUARDED_BY(mu) = 0;

  bool is_monotonic ABSL_GUARDED_BY(mu) = true;
};

// Record that a thread has been started that will increment
// the counters by n.
void AddThread(TestContext* c, int n) {
  absl::MutexLock lock(c->mu);
  c->expected_final += n;
  c->outstanding++;
}

// Get sequence numbers
void GetSequenceNumbers(TestContext* c, int n) {
  SequenceNumber::Value expected_sum = 0;
  SequenceNumber::Value expected_xor = 0;
  SequenceNumber::Value previous = -1;
  bool is_monotonic = true;
  for (int i = 0; i != n; i++) {
    SequenceNumber::Value x = c->cnt.GetNext();
    if (x <= previous) {
      is_monotonic = false;
    }
    expected_sum += x;  // accumulate local sums
    expected_xor ^= x;
    previous = x;
  }
  absl::MutexLock lock(c->mu);
  c->expected_sum += expected_sum;  // accumulate global sums
  c->expected_xor ^= expected_xor;
  if (!is_monotonic) {
    c->is_monotonic = false;
  }
  c->outstanding--;  // this thread is finished
}

// Return whether c->outstanding is 0, which indicates whether all test threads
// have finished their tasks.
// L >= c->mu
bool ThreadsFinished(TestContext* c) {
  c->mu.AssertHeld();
  return c->outstanding == 0;
}

// Test that every time we get a sequence number, we get a higher value than
// the previous one, and that all sequence numbers are given out exactly once.
// L < c->mu
TEST(AtomicSequenceNumber, SequenceNumber) {
  ThreadPool tp(kMaxRefCountThreads);
  TestContext context;
  context.tp = &tp;

  for (int i = 0; i != kMaxRefCountThreads; i++) {
    int n = 10000000;
    AddThread(&context, n);
    context.tp->Schedule([&context, n] { GetSequenceNumbers(&context, n); });
  }
  int64_t expected_final = 0;
  SequenceNumber::Value expected_word_sum = 0;
  SequenceNumber::Value expected_word_xor = 0;
  bool is_monotonic = true;
  {
    absl::MutexLock lock(
        context.mu, absl::Condition(&ThreadsFinished,
                                    &context));  // wait for threads to finish
    expected_final = context.expected_final;
    expected_word_sum = context.expected_sum;
    expected_word_xor = context.expected_xor;
    is_monotonic = context.is_monotonic;
  }

  EXPECT_TRUE(is_monotonic);  // sequence numbers are in order
  EXPECT_EQ(context.cnt.GetNext(), expected_final);

  // Compute the expected value of the cumulative xor.
  int64_t expected_64_xor = 0;
  for (int64_t i = 0; i != expected_final; i++) {
    expected_64_xor ^= i;
  }
  SequenceNumber::Value expected_word_xor_calc = expected_64_xor;
  // Compute the expected value of the cumulative sum.
  int64_t expected_64_sum = (expected_final * (expected_final - 1)) / 2;
  SequenceNumber::Value expected_word_sum_calc = expected_64_sum;
  EXPECT_EQ(expected_word_sum, expected_word_sum_calc);
  EXPECT_EQ(expected_word_xor, expected_word_xor_calc);
}

TEST(AtomicSequenceNumber, SingleThreaded) {
  SequenceNumber cnt;
  EXPECT_EQ(cnt.GetNext(), 0);
  EXPECT_EQ(cnt.GetNext(), 1);
  EXPECT_EQ(cnt.GetNext(), 2);
}

TEST(AtomicSequenceNumber, StdThreaded) {
  SequenceNumber cnt;
  std::vector<std::thread> threads;
  threads.reserve(10);
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

}  // namespace
}  // namespace base
