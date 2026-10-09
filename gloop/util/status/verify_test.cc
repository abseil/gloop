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

#include "gloop/util/status/verify.h"

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace not_util {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::util::Fail;
using ::util::Verify;
using ::util::VerifyEq;
using ::util::VerifyGe;
using ::util::VerifyGt;
using ::util::VerifyLe;
using ::util::VerifyLt;
using ::util::VerifyNe;

TEST(VerifyTest, Fail) {
  EXPECT_THAT(Fail("I'm an error."), StatusIs(_, HasSubstr("I'm an error.")));
}

TEST(VerifyTest, Verify) {
  EXPECT_THAT(Verify(1 + 1 == 2, ""), IsOk());
  EXPECT_THAT(Verify(1 + 1 == 3, "I'm an error."),
              StatusIs(_, AllOf(HasSubstr("Fail("),
                                HasSubstr("util/status/verify_test.cc:"),
                                HasSubstr("I'm an error."))));
  EXPECT_THAT(Verify(1 + 1 == 2), IsOk());
  EXPECT_THAT(Verify(1 + 1 == 3),
              StatusIs(_, AllOf(HasSubstr("Fail("),
                                HasSubstr("util/status/verify_test.cc:"))));
}

TEST(VerifyTest, VerifyEq) {
  EXPECT_THAT(VerifyEq(1 + 1, 2, ""), IsOk());
  EXPECT_THAT(VerifyEq(1 + 1, 3), StatusIs(_, HasSubstr("2 == 3")));
}

TEST(VerifyTest, VerifyNe) {
  EXPECT_THAT(VerifyNe(1 + 1, 3, ""), IsOk());
  EXPECT_THAT(VerifyNe(1 + 1, 2), StatusIs(_, HasSubstr("2 != 2")));
}

TEST(VerifyTest, VerifyLe) {
  EXPECT_THAT(VerifyLe(1, 2, ""), IsOk());
  EXPECT_THAT(VerifyLe(1, 1, ""), IsOk());
  EXPECT_THAT(VerifyLe(2, 1), StatusIs(_, HasSubstr("2 <= 1")));
}

TEST(VerifyTest, VerifyLt) {
  EXPECT_THAT(VerifyLt(1, 2, ""), IsOk());
  EXPECT_THAT(VerifyLt(1, 1), StatusIs(_, HasSubstr("1 < 1")));
  EXPECT_THAT(VerifyLt(2, 1), StatusIs(_, HasSubstr("2 < 1")));
}

TEST(VerifyTest, VerifyGe) {
  EXPECT_THAT(VerifyGe(2, 1, ""), IsOk());
  EXPECT_THAT(VerifyGe(2, 2, ""), IsOk());
  EXPECT_THAT(VerifyGe(1, 2), StatusIs(_, HasSubstr("1 >= 2")));
}

TEST(VerifyTest, VerifyGt) {
  EXPECT_THAT(VerifyGt(2, 1, ""), IsOk());
  EXPECT_THAT(VerifyGt(2, 2), StatusIs(_, HasSubstr("2 > 2")));
  EXPECT_THAT(VerifyGt(1, 2), StatusIs(_, HasSubstr("1 > 2")));
}

template <bool cause_failure>
void BM_VerifyEq(benchmark::State& state) {
  const int x = 100;
  int y = x;
  for (auto s : state) {
    // Prevent the compiler from knowing what `y` is statically, just in case.
    if (cause_failure) ++y;
    auto status = VerifyEq(x, y);
    if (cause_failure) {
      CHECK(!status.ok());
    } else {
      CHECK_OK(status);
    }
  }
}
BENCHMARK(BM_VerifyEq<true>);
BENCHMARK(BM_VerifyEq<false>);

}  // namespace
}  // namespace not_util
