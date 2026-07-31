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

#include "gloop/util/tuple/unfuse.h"

#include <tuple>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_tuple;
using ::std::tuple;

int FusedMinus(const tuple<int, int>& a) { return get<0>(a) - get<1>(a); }
int FusedReturnOne(const tuple<>& a) { return 1; }

struct MutableFusedMinus {
  int operator()(const tuple<int, int>& a) { return get<0>(a) - get<1>(a); }
};

struct MutableFusedReturnOne {
  int operator()(const tuple<>& a) { return 1; }
};

TEST(Unfuse, Basic) { EXPECT_EQ(&FusedMinus, unfuse(FusedMinus).base()); }

TEST(Unfuse, Const) {
  EXPECT_EQ(2, unfuse(FusedMinus)(3, 1));
  EXPECT_EQ(1, unfuse(FusedReturnOne)());
}

TEST(Unfuse, Mutable) {
  EXPECT_EQ(2, unfused<MutableFusedMinus>()(3, 1));
  EXPECT_EQ(1, unfused<MutableFusedReturnOne>()());
}

}  // namespace
}  // namespace tuple
}  // namespace util
