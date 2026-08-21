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

#include "gloop/perftools/tracing/perftools_verify.h"

#include "gtest/gtest.h"

namespace {

TEST(PerftoolsVerify, Verify) { PERFTOOLS_VERIFY(1 == 1); }

TEST(PerftoolsVerify, EQ) { PERFTOOLS_VERIFY_EQ(1, 1); }

TEST(PerftoolsVerify, NE) { PERFTOOLS_VERIFY_NE(1, 2); }

TEST(PerftoolsVerify, LT) { PERFTOOLS_VERIFY_LT(1, 2); }

TEST(PerftoolsVerify, LE) {
  PERFTOOLS_VERIFY_LE(1, 1);
  PERFTOOLS_VERIFY_LE(1, 2);
}

TEST(PerftoolsVerify, GT) { PERFTOOLS_VERIFY_GT(2, 1); }

TEST(PerftoolsVerify, GE) {
  PERFTOOLS_VERIFY_GE(1, 1);
  PERFTOOLS_VERIFY_GE(2, 1);
}

TEST(PerftoolsVerify, VerifyInvokesConditionOnce) {
  int true_count = 0;
  auto true_cond = [&] { return ++true_count, true; };

  PERFTOOLS_VERIFY(true_cond());
  EXPECT_EQ(true_count, 1);
}

TEST(PerftoolsVerify, VerifyReturnsCondition) {
  EXPECT_TRUE(PERFTOOLS_VERIFY(1 == 1));
}

TEST(PerftoolsVerify, VerifyOpInvokesConditionOnce) {
  int lhs_count = 0;
  int rhs_count = 0;
  auto lhs = [&] { return ++lhs_count, 1; };
  auto rhs = [&] { return ++rhs_count, 2; };

  PERFTOOLS_VERIFY_NE(lhs(), rhs());
  EXPECT_EQ(lhs_count, 1);
  EXPECT_EQ(rhs_count, 1);
}

TEST(PerftoolsVerify, VerifyOpReturnsCondition) {
  EXPECT_TRUE(PERFTOOLS_VERIFY_EQ(1, 1));
}

}  // namespace
