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

#include "gloop/util/tuple/cat.h"

#include <tuple>
#include <utility>

#include "gloop/util/tuple/std_tuple.h"
#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_pair;
using ::std::make_tuple;

class Cat : public TestValues {};

TEST_F(Cat, Empty) { EXPECT_EQ(make_tuple(), cat<std_tuple_tag>()); }

TEST_F(Cat, One) {
  EXPECT_EQ(make_tuple(), cat(make_tuple()));
  EXPECT_EQ(make_tuple(a), cat(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), cat(make_tuple(a, b)));
}

TEST_F(Cat, Two) {
  EXPECT_EQ(make_tuple(), cat(make_tuple(), make_tuple()));
  EXPECT_EQ(make_tuple(a), cat(make_tuple(), make_tuple(a)));
  EXPECT_EQ(make_tuple(a), cat(make_tuple(a), make_tuple()));
  EXPECT_EQ(make_tuple(a, b, c, d), cat(make_tuple(a, b), make_tuple(c, d)));
}

TEST_F(Cat, NonRecursive) {
  EXPECT_EQ(make_tuple(make_tuple(a)), cat(make_tuple(make_tuple(a))));
}

TEST_F(Cat, NonConst) {
  ::std::tuple<A&> t(a);
  ::std::tuple<A&> q = cat(t);
  EXPECT_EQ(&a, &get<0>(q));
}

TEST_F(Cat, ExplicitTag) {
  EXPECT_EQ(make_tuple(a, b, c),
            cat<std_tuple_tag>(make_pair(a, b), make_tuple(c)));
}

}  // namespace
}  // namespace tuple
}  // namespace util
