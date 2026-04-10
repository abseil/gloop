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

#include "gloop/thread/fiber/probabilistic_test_util.h"

#include "absl/log/log.h"
#include "gtest/gtest.h"

namespace {

TEST(TestProbabilisticTest, PassesWhenExactlyAtThreshold) {
  int runs = 0;
  auto test_runner = [&runs]() {
    runs++;
    if (runs == 1) {
      return true;
    } else {
      return false;
    }
  };

  EXPECT_TRUE(
      thread::probabilistic_test::RunTestMultipleTimes(2, 1, test_runner));
}

TEST(TestProbabilisticTest, PassesWhenGreaterThanThreshold) {
  auto test_runner = []() { return true; };

  EXPECT_TRUE(
      thread::probabilistic_test::RunTestMultipleTimes(5, 3, test_runner));
}

TEST(TestProbabilisticTest, FailsWhenLessThanThreshold) {
  auto test_runner = []() { return false; };

  EXPECT_FALSE(
      thread::probabilistic_test::RunTestMultipleTimes(5, 3, test_runner));
}

TEST(TestProbabilisticTest, FailsEarly) {
  int runs = 0;
  auto test_runner = [&runs]() {
    runs++;
    return false;
  };

  EXPECT_FALSE(
      thread::probabilistic_test::RunTestMultipleTimes(5, 4, test_runner));
  EXPECT_EQ(runs, 2);
}

TEST(TestProbabilisticTest, PassesEarly) {
  int runs = 0;
  auto test_runner = [&runs]() {
    runs++;
    return true;
  };

  EXPECT_TRUE(
      thread::probabilistic_test::RunTestMultipleTimes(5, 2, test_runner));
  EXPECT_EQ(runs, 2);
}

TEST(TestProbabilisticTest, PROB_EXPECT_EQ_TRUE) {
  EXPECT_TRUE(PROB_EXPECT_EQ(1, 1));
}

TEST(TestProbabilisticTest, PROB_EXPECT_EQ_FALSE) {
  EXPECT_FALSE(PROB_EXPECT_EQ(1, 0));
}

TEST(TestProbabilisticTest, PROB_EXPECT_GT_TRUE) {
  EXPECT_TRUE(PROB_EXPECT_GT(1, 0));
}

TEST(TestProbabilisticTest, PROB_EXPECT_GT_FALSE) {
  EXPECT_FALSE(PROB_EXPECT_GT(0, 1));
}

// We currently assume that LOG(ERROR) won't fail the test, but it would be nice
// to know if it ever starts to.
TEST(TestProbabilisticTest, LogErrorDoesNotFailTest) {
  LOG(ERROR) << "This should not cause the test to fail.";
}
}  // namespace
