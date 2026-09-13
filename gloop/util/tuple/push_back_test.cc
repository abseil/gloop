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

#include "gloop/util/tuple/push_back.h"

#include <tuple>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_tuple;

class PushBack : public TestValues {};

TEST_F(PushBack, Push) {
  EXPECT_EQ(make_tuple(x), push_back(make_tuple(), x));
  EXPECT_EQ(make_tuple(a, x), push_back(make_tuple(a), x));
  EXPECT_EQ(make_tuple(a, b, x), push_back(make_tuple(a, b), x));
}

TEST_F(PushBack, InsertValue) {
  auto t = push_back(make_tuple(), x);
  EXPECT_NE(&x, &get<0>(t));
}

TEST_F(PushBack, Constexpr) {
  constexpr ::std::tuple<int> kTuple(42);
  constexpr auto kPushed = push_back(kTuple, 'A');
  constexpr ::std::tuple<int, char> kExpected(42, 'A');
  EXPECT_EQ(kExpected, kPushed);
}

class PushBackRef : public TestValues {};

TEST_F(PushBackRef, Push) {
  EXPECT_EQ(make_tuple(x), push_back_ref(make_tuple(), x));
  EXPECT_EQ(make_tuple(a, x), push_back_ref(make_tuple(a), x));
  EXPECT_EQ(make_tuple(a, b, x), push_back_ref(make_tuple(a, b), x));
}

TEST_F(PushBackRef, NonConstRef) {
  ::std::tuple<X&> t = push_back_ref(make_tuple(), x);
  EXPECT_EQ(&x, &get<0>(t));
}

TEST_F(PushBackRef, ConstRef) {
  const X& cx = x;
  ::std::tuple<const X&> t = push_back_ref(make_tuple(), cx);
  EXPECT_EQ(&cx, &get<0>(t));
}

TEST_F(PushBackRef, Constexpr) {
  static constexpr int kVal = 10;
  constexpr ::std::tuple<int> kTuple(42);
  constexpr auto kPushed = push_back_ref(kTuple, kVal);
  constexpr ::std::tuple<int, const int&> kExpected(42, kVal);
  EXPECT_EQ(kExpected, kPushed);
  static_assert(&get<1>(kPushed) == &kVal, "Should be reference");
}

}  // namespace
}  // namespace tuple
}  // namespace util
