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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_PROBABILISTIC_TEST_UTIL_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_PROBABILISTIC_TEST_UTIL_H_

#include <functional>
#include <string>

#include "absl/log/log.h"

namespace thread {
namespace probabilistic_test {

// Runs test_action at most max_runs times. If test_action returns true >=
// passing_threshold times, then we consider the test to pass as a whole. It is
// the responsibility of test_action to provide useful logging for debugging.
// RunTestMultipleTimes will only LOG(WARNING) how many failures if any and
// LOG(ERROR) if there are too many failures. test_action should not contain
// asserts as those would cause it to exit before itis run enough times in some
// cases, rather test_action should LOG(WARNING) in the place of unmet
// expectations.
// IMPORTANT: one must ASSERT_TRUE to result, e.g.
// ASSERT_TRUE(RunTestMultipleTimes)
inline bool RunTestMultipleTimes(int max_runs, int passing_threshold,
                                 std::function<bool()> test_action) {
  int total_runs = 0;
  int passes = 0;
  for (int i = 0; i < max_runs; i++) {
    bool is_deciding_test =
        (passing_threshold - passes ==
         max_runs - i);  // Does the number of runs left equal the number of
                         // passes we have yet to achieve?
    bool passed = test_action();
    total_runs++;
    if (passed) passes++;
    if ((is_deciding_test && !passed) || (passes == passing_threshold)) break;
  }

  if (passes < passing_threshold) {
    LOG(ERROR) << "Too many failures: need " << passing_threshold
               << " passes, got " << passes << " out of " << total_runs
               << " runs.";
  } else if (passes != total_runs) {
    LOG(WARNING) << "Passed with failures: need " << passing_threshold
                 << " passes, got " << passes << " out of " << total_runs
                 << " runs.";
  }
  return passes >= passing_threshold;
}

template <typename T1, typename T2>
static bool LogWarningOnNotEq(T1 val1, T2 val2, std::string literal_val1,
                              std::string literal_val2) {
  if (val1 != val2) {
    LOG(WARNING) << "Expected " << literal_val1 << " = " << literal_val2
                 << ", got " << val1 << " != " << val2 << ".";
    return false;
  }
  return true;
}

template <typename T1, typename T2>
static bool LogWarningOnNotGt(T1 val1, T2 val2, std::string literal_val1,
                              std::string literal_val2) {
  if (val1 <= val2) {
    LOG(WARNING) << "Expected " << literal_val1 << " > " << literal_val2
                 << ", got " << val1 << " <= " << val2 << ".";
    return false;
  }
  return true;
}
}  // namespace probabilistic_test
}  // namespace thread

// There is not a good way to "stringify" an expression, so we have to use a
// macro for the PROB(ABILISTIC)_EXPECT_* macros. If we run across an EXPECT_*
// variant that is not defined here, add it. Though, there probably aren't very
// many left since we do not seem to do an esoteric comparison tests.
// The # operator in a macro stringifies the expression, eg Foo() -> "Foo()"
#define PROB_EXPECT_EQ(val1, val2) \
  thread::probabilistic_test::LogWarningOnNotEq(val1, val2, #val1, #val2)

#define PROB_EXPECT_GT(val1, val2) \
  thread::probabilistic_test::LogWarningOnNotGt(val1, val2, #val1, #val2)
#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_PROBABILISTIC_TEST_UTIL_H_
