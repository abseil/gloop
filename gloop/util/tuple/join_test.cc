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

#include "gloop/util/tuple/join.h"

#include <tuple>
#include <utility>

#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_pair;
using ::std::make_tuple;
using ::std::tuple;

class Join : public TestValues {};

TEST_F(Join, Empty) { EXPECT_EQ(make_tuple(), join(make_tuple())); }

TEST_F(Join, One) {
  EXPECT_EQ(make_tuple(), join(make_tuple(make_tuple())));
  EXPECT_EQ(make_tuple(a), join(make_tuple(make_tuple(a))));
  EXPECT_EQ(make_tuple(a, b), join(make_tuple(make_tuple(a, b))));
}

TEST_F(Join, Two) {
  EXPECT_EQ(make_tuple(), join(make_tuple(make_tuple(), make_tuple())));
  EXPECT_EQ(make_tuple(a), join(make_tuple(make_tuple(), make_tuple(a))));
  EXPECT_EQ(make_tuple(a), join(make_tuple(make_tuple(a), make_tuple())));
  EXPECT_EQ(make_tuple(a, b, c, d),
            join(make_tuple(make_tuple(a, b), make_tuple(c, d))));
}

TEST_F(Join, NonRecursive) {
  EXPECT_EQ(make_tuple(make_tuple(1)),
            join(make_tuple(make_tuple(make_tuple(1)))));
}

TEST_F(Join, NonConst) {
  ::std::tuple<A&> t(a);
  ::std::tuple<A&> q = join(make_tuple(t));
  EXPECT_EQ(&a, &get<0>(q));
}

TEST_F(Join, MixedTupleTypes) {
  EXPECT_EQ(make_tuple(a, b, c, d),
            join(make_tuple(make_pair(a, b), make_pair(c, d))));
}

TEST_F(Join, ExplicitTag) {
  EXPECT_EQ(make_pair(a, b), join<pair_tag>(make_tuple(make_tuple(a, b))));
}

}  // namespace
}  // namespace tuple
}  // namespace util
