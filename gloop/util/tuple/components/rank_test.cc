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

#include "gloop/util/tuple/components/rank.h"

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

namespace specialized_class {

template <class T = rank_selector_t>
struct ZeroOne;

template <class T>
struct ZeroOne<rank<0, T>> {
  int operator()() const { return 0; }
};

template <class T>
struct ZeroOne<rank<1, T>> {
  int operator()() const { return 1; }
};

template <class T = rank_selector_t>
struct OneTwo;

template <class T>
struct OneTwo<rank<1, T>> {
  int operator()() const { return 1; }
};

template <class T>
struct OneTwo<rank<2, T>> {
  int operator()() const { return 2; }
};

template <class T = rank_selector_t>
struct TwoThree;

template <class T>
struct TwoThree<rank<2, T>> {
  int operator()() const { return 2; }
};

template <class T>
struct TwoThree<rank<3, T>> {
  int operator()() const { return 3; }
};

template <class T = rank_selector_t>
struct MaxRank;

template <class T>
struct MaxRank<rank<kMaxRank, T>> {
  int operator()() const { return kMaxRank; }
};

TEST(RankTest, SpecializedClass) {
  EXPECT_EQ(0, ZeroOne<>()());
  EXPECT_EQ(1, OneTwo<>()());
  EXPECT_EQ(2, TwoThree<>()());
  EXPECT_EQ(kMaxRank, MaxRank<>()());
}

}  // namespace specialized_class

}  // namespace
}  // namespace tuple
}  // namespace util
