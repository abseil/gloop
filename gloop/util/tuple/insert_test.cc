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

#include "gloop/util/tuple/insert.h"

#include <tuple>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_tuple;

class Insert : public TestValues {};

TEST_F(Insert, ToEmpty) {
  EXPECT_EQ(make_tuple(), insert<0>(make_tuple()));
  EXPECT_EQ(make_tuple(x), insert<0>(make_tuple(), x));
  EXPECT_EQ(make_tuple(x, y), insert<0>(make_tuple(), x, y));
}

TEST_F(Insert, ToFront) {
  EXPECT_EQ(make_tuple(a), insert<0>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), insert<0>(make_tuple(a, b)));

  EXPECT_EQ(make_tuple(x, a), insert<0>(make_tuple(a), x));
  EXPECT_EQ(make_tuple(x, a, b), insert<0>(make_tuple(a, b), x));

  EXPECT_EQ(make_tuple(x, y, a), insert<0>(make_tuple(a), x, y));
  EXPECT_EQ(make_tuple(x, y, a, b), insert<0>(make_tuple(a, b), x, y));
}

TEST_F(Insert, ToBack) {
  EXPECT_EQ(make_tuple(a), insert<1>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), insert<2>(make_tuple(a, b)));

  EXPECT_EQ(make_tuple(a, x), insert<1>(make_tuple(a), x));
  EXPECT_EQ(make_tuple(a, b, x), insert<2>(make_tuple(a, b), x));

  EXPECT_EQ(make_tuple(a, x, y), insert<1>(make_tuple(a), x, y));
  EXPECT_EQ(make_tuple(a, b, x, y), insert<2>(make_tuple(a, b), x, y));
}

TEST_F(Insert, ToMiddle) {
  EXPECT_EQ(make_tuple(a, b), insert<1>(make_tuple(a, b)));
  EXPECT_EQ(make_tuple(a, x, b), insert<1>(make_tuple(a, b), x));
  EXPECT_EQ(make_tuple(a, x, y, b), insert<1>(make_tuple(a, b), x, y));
}

TEST_F(Insert, InsertValue) {
  auto t = insert<0>(make_tuple(), x);
  EXPECT_NE(&x, &get<0>(t));
}

TEST_F(Insert, Constexpr) {
  constexpr ::std::tuple<int> kTuple(42);
  constexpr auto kInserted = insert<1>(kTuple, 'A');
  constexpr ::std::tuple<int, char> kExpected(42, 'A');
  EXPECT_EQ(kExpected, kInserted);
}

class InsertRef : public TestValues {};

TEST_F(InsertRef, ToEmpty) {
  EXPECT_EQ(make_tuple(), insert_ref<0>(make_tuple()));
  EXPECT_EQ(make_tuple(x), insert_ref<0>(make_tuple(), x));
  EXPECT_EQ(make_tuple(x, y), insert_ref<0>(make_tuple(), x, y));
}

TEST_F(InsertRef, ToFront) {
  EXPECT_EQ(make_tuple(a), insert_ref<0>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), insert_ref<0>(make_tuple(a, b)));

  EXPECT_EQ(make_tuple(x, a), insert_ref<0>(make_tuple(a), x));
  EXPECT_EQ(make_tuple(x, a, b), insert_ref<0>(make_tuple(a, b), x));

  EXPECT_EQ(make_tuple(x, y, a), insert_ref<0>(make_tuple(a), x, y));
  EXPECT_EQ(make_tuple(x, y, a, b), insert_ref<0>(make_tuple(a, b), x, y));
}

TEST_F(InsertRef, ToBack) {
  EXPECT_EQ(make_tuple(a), insert_ref<1>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), insert_ref<2>(make_tuple(a, b)));

  EXPECT_EQ(make_tuple(a, x), insert_ref<1>(make_tuple(a), x));
  EXPECT_EQ(make_tuple(a, b, x), insert_ref<2>(make_tuple(a, b), x));

  EXPECT_EQ(make_tuple(a, x, y), insert_ref<1>(make_tuple(a), x, y));
  EXPECT_EQ(make_tuple(a, b, x, y), insert_ref<2>(make_tuple(a, b), x, y));
}

TEST_F(InsertRef, ToMiddle) {
  EXPECT_EQ(make_tuple(a, b), insert_ref<1>(make_tuple(a, b)));
  EXPECT_EQ(make_tuple(a, x, b), insert_ref<1>(make_tuple(a, b), x));
  EXPECT_EQ(make_tuple(a, x, y, b), insert_ref<1>(make_tuple(a, b), x, y));
}

TEST_F(InsertRef, NonConstRef) {
  ::std::tuple<X&> t = insert_ref<0>(make_tuple(), x);
  EXPECT_EQ(&x, &get<0>(t));
}

// This test is disable when compiling with gcc because of the compiler bug
// See http://b/10321377.
//
// TODO: Enable this test when http://b/10321377 is fixed.

#if defined(__clang__)

TEST_F(InsertRef, ConstRef) {
  const X& cx = x;
  ::std::tuple<const X&> t = insert_ref<0>(make_tuple(), cx);
  EXPECT_EQ(&cx, &get<0>(t));
}

#endif

TEST_F(InsertRef, Constexpr) {
  static constexpr int kVal = 10;
  constexpr ::std::tuple<int> kTuple(42);
  constexpr auto kInserted = insert_ref<0>(kTuple, kVal);
  constexpr ::std::tuple<const int&, int> kExpected(kVal, 42);
  EXPECT_EQ(kExpected, kInserted);
  static_assert(&get<0>(kInserted) == &kVal, "Should be reference");
}

class InsertTuple : public TestValues {};

TEST_F(InsertTuple, ToEmpty) {
  EXPECT_EQ(make_tuple(), insert_tuple<0>(make_tuple(), make_tuple()));
  EXPECT_EQ(make_tuple(x), insert_tuple<0>(make_tuple(), make_tuple(x)));
  EXPECT_EQ(make_tuple(x, y), insert_tuple<0>(make_tuple(), make_tuple(x, y)));
}

TEST_F(InsertTuple, ToFront) {
  EXPECT_EQ(make_tuple(a), insert_tuple<0>(make_tuple(a), make_tuple()));
  EXPECT_EQ(make_tuple(x, a), insert_tuple<0>(make_tuple(a), make_tuple(x)));
  EXPECT_EQ(make_tuple(x, y, a),
            insert_tuple<0>(make_tuple(a), make_tuple(x, y)));

  EXPECT_EQ(make_tuple(a, b), insert_tuple<0>(make_tuple(a, b), make_tuple()));
  EXPECT_EQ(make_tuple(x, a, b),
            insert_tuple<0>(make_tuple(a, b), make_tuple(x)));
  EXPECT_EQ(make_tuple(x, y, a, b),
            insert_tuple<0>(make_tuple(a, b), make_tuple(x, y)));
}

TEST_F(InsertTuple, ToBack) {
  EXPECT_EQ(make_tuple(a), insert_tuple<1>(make_tuple(a), make_tuple()));
  EXPECT_EQ(make_tuple(a, x), insert_tuple<1>(make_tuple(a), make_tuple(x)));
  EXPECT_EQ(make_tuple(a, x, y),
            insert_tuple<1>(make_tuple(a), make_tuple(x, y)));

  EXPECT_EQ(make_tuple(a, b), insert_tuple<2>(make_tuple(a, b), make_tuple()));
  EXPECT_EQ(make_tuple(a, b, x),
            insert_tuple<2>(make_tuple(a, b), make_tuple(x)));
  EXPECT_EQ(make_tuple(a, b, x, y),
            insert_tuple<2>(make_tuple(a, b), make_tuple(x, y)));
}

TEST_F(InsertTuple, ToMiddle) {
  EXPECT_EQ(make_tuple(a, b), insert_tuple<1>(make_tuple(a, b), make_tuple()));
  EXPECT_EQ(make_tuple(a, x, b),
            insert_tuple<1>(make_tuple(a, b), make_tuple(x)));
  EXPECT_EQ(make_tuple(a, x, y, b),
            insert_tuple<1>(make_tuple(a, b), make_tuple(x, y)));
}

TEST_F(InsertTuple, InsertValue) {
  auto t = make_tuple(x);
  auto q = insert_tuple<0>(make_tuple(), t);
  EXPECT_NE(&get<0>(t), &get<0>(q));
}

TEST_F(InsertTuple, InsertReference) {
  ::std::tuple<X&> t = insert_tuple<0>(make_tuple(), ::std::tie(x));
  EXPECT_EQ(&x, &get<0>(t));
}

TEST_F(InsertTuple, Constexpr) {
  constexpr ::std::tuple<int> kTuple1(42);
  constexpr ::std::tuple<char, double> kTuple2('A', 2.5);
  constexpr auto kInserted = insert_tuple<1>(kTuple1, kTuple2);
  constexpr ::std::tuple<int, char, double> kExpected(42, 'A', 2.5);
  EXPECT_EQ(kExpected, kInserted);
}

}  // namespace
}  // namespace tuple
}  // namespace util
