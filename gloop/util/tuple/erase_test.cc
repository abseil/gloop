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

using ::testing::FieldsAre;
using ::testing::Ref;

class Erase : public TestValues {};

TEST_F(Erase, NoOp) {
  EXPECT_EQ(erase(std::tuple()), std::tuple());
  EXPECT_THAT(erase(std::make_tuple(a, b)), FieldsAre(a, b));
}

TEST_F(Erase, EraseAll) {
  EXPECT_EQ((erase<0, 1>(std::make_tuple(a, b))), std::tuple());
}

TEST_F(Erase, EraseFirst) {
  EXPECT_THAT(erase<0>(std::make_tuple(a, b)), FieldsAre(b));
}

TEST_F(Erase, EraseLast) {
  EXPECT_THAT(erase<1>(std::make_tuple(a, b)), FieldsAre(a));
}

TEST_F(Erase, EraseMiddle) {
  EXPECT_THAT(erase<1>(std::make_tuple(a, b, c)), FieldsAre(a, c));
}

TEST_F(Erase, EraseEven) {
  EXPECT_THAT((erase<0, 2>(std::make_tuple(a, b, c))), FieldsAre(b));
}

TEST_F(Erase, DuplicateIndex) {
  EXPECT_THAT((erase<0, 0>(std::make_tuple(a, b))), FieldsAre(b));
}

TEST_F(Erase, ReverseOrder) {
  EXPECT_THAT((erase<2, 0>(std::make_tuple(a, b, c))), FieldsAre(b));
}

TEST_F(Erase, NonConst) {
  std::tuple<A&, B&> t(a, b);
  std::tuple<B&> q = erase<0>(t);
  EXPECT_THAT(q, FieldsAre(Ref(b)));
}

TEST_F(Erase, Constexpr) {
  constexpr std::tuple<int, char, double> kTuple(42, 'A', 2.5);
  constexpr auto kErased = erase<1>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(42, 2.5));
}

class EraseRange : public TestValues {};

TEST_F(EraseRange, NoOp) {
  EXPECT_EQ((erase_range<0, 0>(std::tuple())), std::tuple());

  const auto t = std::make_tuple(a, b);
  EXPECT_THAT((erase_range<0, 0>(t)), FieldsAre(a, b));
  EXPECT_THAT((erase_range<1, 1>(t)), FieldsAre(a, b));
  EXPECT_THAT((erase_range<2, 2>(t)), FieldsAre(a, b));
}

TEST_F(EraseRange, EraseAll) {
  EXPECT_EQ((erase_range<0, 1>(std::make_tuple(a))), std::tuple());
  EXPECT_EQ((erase_range<0, 2>(std::make_tuple(a, b))), std::tuple());
}

TEST_F(EraseRange, EraseFirst) {
  EXPECT_THAT((erase_range<0, 1>(std::make_tuple(a, b))), FieldsAre(b));
}

TEST_F(EraseRange, EraseLast) {
  EXPECT_THAT((erase_range<1, 2>(std::make_tuple(a, b))), FieldsAre(a));
}

TEST_F(EraseRange, EraseMiddle) {
  EXPECT_THAT((erase_range<1, 2>(std::make_tuple(a, b, c))), FieldsAre(a, c));
}

TEST_F(EraseRange, LeaveFirst) {
  EXPECT_THAT((erase_range<1, 3>(std::make_tuple(a, b, c))), FieldsAre(a));
}

TEST_F(EraseRange, LeaveLast) {
  EXPECT_THAT((erase_range<0, 2>(std::make_tuple(a, b, c))), FieldsAre(c));
}

TEST_F(EraseRange, NonConst) {
  std::tuple<A&, B&> t(a, b);
  std::tuple<B&> q = erase_range<0, 1>(t);
  EXPECT_THAT(q, FieldsAre(Ref(b)));
}

TEST_F(EraseRange, Constexpr) {
  constexpr std::tuple<int, char, double> kTuple(42, 'A', 2.5);
  constexpr auto kErased = erase_range<0, 2>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(2.5));
}

class EraseIfIndex : public TestValues {};

struct IndexNeValue {
  template <std::size_t I, class T>
  struct apply : std::integral_constant<bool, I != T::value> {};
};

TEST_F(EraseIfIndex, Functional) {
  EXPECT_EQ(erase_if_index<IndexNeValue>(std::tuple()), std::tuple());
  EXPECT_EQ(erase_if_index<IndexNeValue>(std::make_tuple(x)), std::tuple());
  EXPECT_THAT(erase_if_index<IndexNeValue>(std::make_tuple(a)), FieldsAre(a));
  EXPECT_THAT(erase_if_index<IndexNeValue>(std::make_tuple(a, x)),
              FieldsAre(a));
  EXPECT_THAT(erase_if_index<IndexNeValue>(std::make_tuple(x, b)),
              FieldsAre(b));
  EXPECT_THAT(erase_if_index<IndexNeValue>(std::make_tuple(a, x, c)),
              FieldsAre(a, c));
  EXPECT_THAT(erase_if_index<IndexNeValue>(std::make_tuple(x, b, y)),
              FieldsAre(b));
}

TEST_F(EraseIfIndex, Constexpr) {
  constexpr auto kTuple =
      std::make_tuple(std::integral_constant<std::size_t, 0>{},
                      std::integral_constant<std::size_t, 5>{});
  constexpr auto kErased = erase_if_index<IndexNeValue>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(std::integral_constant<std::size_t, 0>{}));
}

class EraseIf : public TestValues {};

struct Negative {
  template <class T>
  struct apply : std::integral_constant<bool, (T::value < 0)> {};
};

TEST_F(EraseIf, Functional) {
  EXPECT_EQ(erase_if<Negative>(std::tuple()), std::tuple());
  EXPECT_EQ(erase_if<Negative>(std::make_tuple(x)), std::tuple());
  EXPECT_THAT(erase_if<Negative>(std::make_tuple(a)), FieldsAre(a));
  EXPECT_THAT(erase_if<Negative>(std::make_tuple(x, a)), FieldsAre(a));
  EXPECT_THAT(erase_if<Negative>(std::make_tuple(a, x)), FieldsAre(a));
  EXPECT_THAT(erase_if<Negative>(std::make_tuple(a, x, b)), FieldsAre(a, b));
  EXPECT_THAT(erase_if<Negative>(std::make_tuple(x, a, y)), FieldsAre(a));
}

TEST_F(EraseIf, Constexpr) {
  constexpr auto kTuple = std::make_tuple(std::integral_constant<int, -1>{},
                                          std::integral_constant<int, 42>{});
  constexpr auto kErased = erase_if<Negative>(kTuple);
  EXPECT_THAT(kErased, FieldsAre(std::integral_constant<int, 42>{}));
}

}  // namespace
}  // namespace tuple
}  // namespace util
