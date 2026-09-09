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

#include "gloop/util/tuple/pop_front.h"

#include <tuple>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;
using ::std::tuple;

class PopFront : public TestValues {};

TEST_F(PopFront, ToEmpty) { EXPECT_EQ(make_tuple(), pop_front(make_tuple(a))); }

TEST_F(PopFront, NonConstRef) {
  const ::std::tuple<A&, B&> t(a, b);
  ::std::tuple<B&> q = pop_front(t);
  EXPECT_EQ(&b, &get<0>(q));
}

TEST_F(PopFront, ConstRef) {
  const ::std::tuple<const A&, const B&> t(a, b);
  ::std::tuple<const B&> q = pop_front(t);
  EXPECT_EQ(&b, &get<0>(q));
}

TEST_F(PopFront, Copy) {
  auto t = make_tuple(a, b);
  auto q = pop_front(t);
  EXPECT_NE(&get<0>(q), &get<1>(t));
}

TEST_F(PopFront, Constexpr) {
  constexpr tuple<int, char> kTuple(42, 'A');
  constexpr auto kPopped = pop_front(kTuple);
  constexpr tuple<char> kExpected('A');
  EXPECT_EQ(kExpected, kPopped);
}

}  // namespace
}  // namespace tuple
}  // namespace util
