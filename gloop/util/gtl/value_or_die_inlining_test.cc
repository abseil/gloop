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

// This file tests that the inliner wouldn't silently break, since we have
// inliner-only migration code guarded with ABSL_REFACTOR_INLINER_IS_RUNNING,
// which the compiler wouldn't otherwise see.
//
// Without these tests, breakage of the hidden code would cause the inliner to
// suddenly stop processing any translation units that directly or indirectly
// include the headers.

#include <utility>

#include "absl/status/statusor.h"

#ifndef ABSL_REFACTOR_INLINER_IS_RUNNING
#error ABSL_REFACTOR_INLINER_IS_RUNNING must be defined for this test.
#endif

#ifdef THIRD_PARTY_GLOOP_UTIL_GTL_VALUE_OR_DIE_H_
#error Must not include gtl/value_or_die.h before this line
#endif

// These functions are normally declared but left undefined for the inliner.
// We specialize them here to ensure they are in fact previously declared, and
// so they build correctly (otherwise these tests wouldn't link).

template <>
int&& gtl::ValueOrDie(absl::StatusOr<int>&& value) {
  return *std::move(value);
}

template <>
const int& gtl::ValueOrDie(absl::StatusOr<const int&>&& value) {
  return *std::move(value);
}

#include "gloop/util/gtl/value_or_die.h"
#include "gtest/gtest.h"

namespace {

TEST(ValueOrDieInliningTest, ValueOrDieMethod) {
  EXPECT_EQ(absl::StatusOr<int>(42).ValueOrDie(), 42);
  EXPECT_EQ(absl::StatusOr<const int&>(42).ValueOrDie(), 42);
}

TEST(ValueOrDieInliningTest, GtlValueOrDie) {
  EXPECT_EQ(gtl::ValueOrDie(absl::StatusOr<int>(42)), 42);
  EXPECT_EQ(gtl::ValueOrDie(absl::StatusOr<const int&>(42)), 42);
}

}  // namespace

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_VALUE_OR_DIE_H_
#error Must include gtl/value_or_die.h before this line
#endif
