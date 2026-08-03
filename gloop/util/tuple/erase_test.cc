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

#include "gloop/util/tuple/erase.h"

#include <cstddef>
#include <tuple>
#include <type_traits>

#include "gloop/util/tuple/test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::integral_constant;
using ::std::make_tuple;
using ::std::size_t;
using ::std::tuple;
using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::Ref;

class Erase : public TestValues {};

TEST_F(Erase, NoOp) {
  EXPECT_THAT(erase(make_tuple()), Eq(make_tuple()));
  EXPECT_THAT(erase(make_tuple(a, b)), FieldsAre(a, b));
}

TEST_F(Erase, EraseAll) {
  EXPECT_THAT((erase<0, 1>(make_tuple(a, b))), Eq(make_tuple()));
}

TEST_F(Erase, EraseFirst) {
  EXPECT_THAT(erase<0>(make_tuple(a, b)), FieldsAre(b));
}

TEST_F(Erase, EraseLast) {
  EXPECT_THAT(erase<1>(make_tuple(a, b)), FieldsAre(a));
}

TEST_F(Erase, EraseMiddle) {
  EXPECT_THAT(erase<1>(make_tuple(a, b, c)), FieldsAre(a, c));
}

TEST_F(Erase, EraseEven) {
  EXPECT_THAT((erase<0, 2>(make_tuple(a, b, c))), FieldsAre(b));
}

TEST_F(Erase, DuplicateIndex) {
  EXPECT_THAT((erase<0, 0>(make_tuple(a, b))), FieldsAre(b));
}

TEST_F(Erase, ReverseOrder) {
  EXPECT_THAT((erase<2, 0>(make_tuple(a, b, c))), FieldsAre(b));
}

TEST_F(Erase, NonConst) {
  tuple<A&, B&> t(a, b);
  tuple<B&> q = erase<0>(t);
  EXPECT_THAT(q, FieldsAre(Ref(b)));
}

TEST_F(Erase, Constexpr) {
  constexpr tuple<int, char, double> kTuple(42, 'A', 2.5);
  constexpr auto kErased = erase<1>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(42, 2.5));
}

class EraseRange : public TestValues {};

TEST_F(EraseRange, NoOp) {
  EXPECT_THAT((erase_range<0, 0>(make_tuple())), Eq(make_tuple()));

  const auto t = make_tuple(a, b);
  EXPECT_THAT((erase_range<0, 0>(t)), FieldsAre(a, b));
  EXPECT_THAT((erase_range<1, 1>(t)), FieldsAre(a, b));
  EXPECT_THAT((erase_range<2, 2>(t)), FieldsAre(a, b));
}

TEST_F(EraseRange, EraseAll) {
  EXPECT_THAT((erase_range<0, 1>(make_tuple(a))), Eq(make_tuple()));
  EXPECT_THAT((erase_range<0, 2>(make_tuple(a, b))), Eq(make_tuple()));
}

TEST_F(EraseRange, EraseFirst) {
  EXPECT_THAT((erase_range<0, 1>(make_tuple(a, b))), FieldsAre(b));
}

TEST_F(EraseRange, EraseLast) {
  EXPECT_THAT((erase_range<1, 2>(make_tuple(a, b))), FieldsAre(a));
}

TEST_F(EraseRange, EraseMiddle) {
  EXPECT_THAT((erase_range<1, 2>(make_tuple(a, b, c))), FieldsAre(a, c));
}

TEST_F(EraseRange, LeaveFirst) {
  EXPECT_THAT((erase_range<1, 3>(make_tuple(a, b, c))), FieldsAre(a));
}

TEST_F(EraseRange, LeaveLast) {
  EXPECT_THAT((erase_range<0, 2>(make_tuple(a, b, c))), FieldsAre(c));
}

TEST_F(EraseRange, NonConst) {
  tuple<A&, B&> t(a, b);
  tuple<B&> q = erase_range<0, 1>(t);
  EXPECT_THAT(q, FieldsAre(Ref(b)));
}

TEST_F(EraseRange, Constexpr) {
  constexpr tuple<int, char, double> kTuple(42, 'A', 2.5);
  constexpr auto kErased = erase_range<0, 2>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(2.5));
}

class EraseIfIndex : public TestValues {};

struct IndexNeValue {
  template <size_t I, class T>
  struct apply : integral_constant<bool, I != T::value> {};
};

TEST_F(EraseIfIndex, Functional) {
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple()), Eq(make_tuple()));
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple(x)), Eq(make_tuple()));
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple(a)), FieldsAre(a));
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple(a, x)), FieldsAre(a));
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple(x, b)), FieldsAre(b));
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple(a, x, c)),
              FieldsAre(a, c));
  EXPECT_THAT(erase_if_index<IndexNeValue>(make_tuple(x, b, y)), FieldsAre(b));
}

TEST_F(EraseIfIndex, Constexpr) {
  constexpr auto kTuple = make_tuple(integral_constant<size_t, 0>{},
                                     integral_constant<size_t, 5>{});
  constexpr auto kErased = erase_if_index<IndexNeValue>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(integral_constant<size_t, 0>{}));
}

class EraseIf : public TestValues {};

struct Negative {
  template <class T>
  struct apply : integral_constant<bool, (T::value < 0)> {};
};

TEST_F(EraseIf, Functional) {
  EXPECT_THAT(erase_if<Negative>(make_tuple()), Eq(make_tuple()));
  EXPECT_THAT(erase_if<Negative>(make_tuple(x)), Eq(make_tuple()));
  EXPECT_THAT(erase_if<Negative>(make_tuple(a)), FieldsAre(a));
  EXPECT_THAT(erase_if<Negative>(make_tuple(x, a)), FieldsAre(a));
  EXPECT_THAT(erase_if<Negative>(make_tuple(a, x)), FieldsAre(a));
  EXPECT_THAT(erase_if<Negative>(make_tuple(a, x, b)), FieldsAre(a, b));
  EXPECT_THAT(erase_if<Negative>(make_tuple(x, a, y)), FieldsAre(a));
}

TEST_F(EraseIf, Constexpr) {
  constexpr auto kTuple =
      make_tuple(integral_constant<int, -1>{}, integral_constant<int, 42>{});
  constexpr auto kErased = erase_if<Negative>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(integral_constant<int, 42>{}));
}

}  // namespace
}  // namespace tuple
}  // namespace util
