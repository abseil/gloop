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

#include "gloop/util/tuple/components/rank.h"

#include <type_traits>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

namespace overloaded_function {

int ZeroOne(rank<0>) { return 0; }
[[maybe_unused]] int ZeroOne(rank<1>) { return 1; }

int OneTwo(rank<1>) { return 1; }
[[maybe_unused]] int OneTwo(rank<2>) { return 2; }

int TwoThree(rank<1>) { return 2; }
[[maybe_unused]] int TwoThree(rank<2>) { return 3; }

int MaxRank(rank<kMaxRank>) { return kMaxRank; }

TEST(RankTest, OverloadedFunction) {
  EXPECT_EQ(0, ZeroOne(rank_selector));
  EXPECT_EQ(1, OneTwo(rank_selector));
  EXPECT_EQ(2, TwoThree(rank_selector));
  EXPECT_EQ(kMaxRank, MaxRank(rank_selector));
}

}  // namespace overloaded_function

namespace properties {

static_assert(sizeof(rank_selector_t) == 1);
static_assert(std::is_trivial_v<rank_selector_t>);
static_assert(std::is_standard_layout_v<rank_selector_t>);
static_assert(std::is_empty_v<rank_selector_t>);
static_assert(std::is_same_v<rank_selector_t, rank<0>>);
static_assert(std::is_convertible_v<rank<0>, rank<1>>);
static_assert(std::is_convertible_v<rank<0>, rank<kMaxRank>>);
static_assert(std::is_convertible_v<rank<1>, rank<kMaxRank>>);
static_assert(!std::is_convertible_v<rank<1>, rank<0>>);
static_assert(!std::is_convertible_v<rank<kMaxRank>, rank<0>>);

TEST(RankTest, Properties) {
  EXPECT_EQ(sizeof(rank_selector_t), 1);
  EXPECT_TRUE(std::is_trivial_v<rank_selector_t>);
  EXPECT_TRUE(std::is_standard_layout_v<rank_selector_t>);
  EXPECT_TRUE((std::is_convertible_v<rank<0>, rank<kMaxRank>>));
  EXPECT_FALSE((std::is_convertible_v<rank<1>, rank<0>>));
}

}  // namespace properties

}  // namespace
}  // namespace tuple
}  // namespace util
