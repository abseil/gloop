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

#include "gloop/util/tuple/pop_back.h"

#include <tuple>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;
using ::std::tuple;

class PopBack : public TestValues {};

TEST_F(PopBack, ToEmpty) { EXPECT_EQ(make_tuple(), pop_back(make_tuple(a))); }

TEST_F(PopBack, NonConstRef) {
  const ::std::tuple<A&, B&> t(a, b);
  ::std::tuple<A&> q = pop_back(t);
  EXPECT_EQ(&a, &get<0>(q));
}

TEST_F(PopBack, ConstRef) {
  const ::std::tuple<const A&, const B&> t(a, b);
  ::std::tuple<const A&> q = pop_back(t);
  EXPECT_EQ(&a, &get<0>(q));
}

TEST_F(PopBack, Copy) {
  auto t = make_tuple(a, b);
  auto q = pop_back(t);
  EXPECT_NE(&get<0>(q), &get<0>(t));
}
TEST_F(PopBack, Constexpr) {
  constexpr auto in = std::make_tuple(42, 'A');
  constexpr auto out = pop_back(in);
  static_assert(out == std::make_tuple(42));
}

}  // namespace
}  // namespace tuple
}  // namespace util
