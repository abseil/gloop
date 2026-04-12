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

#include "gloop/gloop_test.h"
#include "gloop/util/tuple/test_util.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;

class Erase : public TestValues {};

TEST_F(Erase, NoOp) {
  EXPECT_EQ(make_tuple(), erase(make_tuple()));
  EXPECT_EQ(make_tuple(a, b), erase(make_tuple(a, b)));
}

TEST_F(Erase, EraseAll) {
  EXPECT_EQ(make_tuple(), (erase<0, 1>(make_tuple(a, b))));
}

TEST_F(Erase, EraseFirst) {
  EXPECT_EQ(make_tuple(b), erase<0>(make_tuple(a, b)));
}

TEST_F(Erase, EraseLast) {
  EXPECT_EQ(make_tuple(a), erase<1>(make_tuple(a, b)));
}

TEST_F(Erase, EraseMiddle) {
  EXPECT_EQ(make_tuple(a, c), erase<1>(make_tuple(a, b, c)));
}

TEST_F(Erase, EraseEven) {
  EXPECT_EQ(make_tuple(b), (erase<0, 2>(make_tuple(a, b, c))));
}

TEST_F(Erase, DuplicateIndex) {
  EXPECT_EQ(make_tuple(b), (erase<0, 0>(make_tuple(a, b))));
}

TEST_F(Erase, ReverseOrder) {
  EXPECT_EQ(make_tuple(b), (erase<2, 0>(make_tuple(a, b, c))));
}

TEST_F(Erase, NonConst) {
  ::std::tuple<A&, B&> t(a, b);
  ::std::tuple<B&> q = erase<0>(t);
  EXPECT_EQ(&b, &get<0>(q));
}

class EraseRange : public TestValues {};

TEST_F(EraseRange, NoOp) {
  EXPECT_EQ(make_tuple(), (erase_range<0, 0>(make_tuple())));

  auto t = make_tuple(a, b);
  EXPECT_EQ(t, (erase_range<0, 0>(t)));
  EXPECT_EQ(t, (erase_range<1, 1>(t)));
  EXPECT_EQ(t, (erase_range<2, 2>(t)));
}

TEST_F(EraseRange, EraseAll) {
  EXPECT_EQ(make_tuple(), (erase_range<0, 1>(make_tuple(a))));
  EXPECT_EQ(make_tuple(), (erase_range<0, 2>(make_tuple(a, b))));
}

TEST_F(EraseRange, EraseFirst) {
  EXPECT_EQ(make_tuple(b), (erase_range<0, 1>(make_tuple(a, b))));
}

TEST_F(EraseRange, EraseLast) {
  EXPECT_EQ(make_tuple(a), (erase_range<1, 2>(make_tuple(a, b))));
}

TEST_F(EraseRange, EraseMiddle) {
  EXPECT_EQ(make_tuple(a, c), (erase_range<1, 2>(make_tuple(a, b, c))));
}

TEST_F(EraseRange, LeaveFirst) {
  EXPECT_EQ(make_tuple(a), (erase_range<1, 3>(make_tuple(a, b, c))));
}

TEST_F(EraseRange, LeaveLast) {
  EXPECT_EQ(make_tuple(c), (erase_range<0, 2>(make_tuple(a, b, c))));
}

TEST_F(EraseRange, NonConst) {
  ::std::tuple<A&, B&> t(a, b);
  ::std::tuple<B&> q = erase_range<0, 1>(t);
  EXPECT_EQ(&b, &get<0>(q));
}

class EraseIfIndex : public TestValues {};

struct IndexNeValue {
  template <::size_t I, class T>
  struct apply : ::std::integral_constant<bool, I != T::value> {};
};

TEST_F(EraseIfIndex, Functional) {
  EXPECT_EQ(make_tuple(), erase_if_index<IndexNeValue>(make_tuple()));
  EXPECT_EQ(make_tuple(), erase_if_index<IndexNeValue>(make_tuple(x)));
  EXPECT_EQ(make_tuple(a), erase_if_index<IndexNeValue>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a), erase_if_index<IndexNeValue>(make_tuple(a, x)));
  EXPECT_EQ(make_tuple(b), erase_if_index<IndexNeValue>(make_tuple(x, b)));
  EXPECT_EQ(make_tuple(a, c),
            erase_if_index<IndexNeValue>(make_tuple(a, x, c)));
  EXPECT_EQ(make_tuple(b), erase_if_index<IndexNeValue>(make_tuple(x, b, y)));
}

class EraseIf : public TestValues {};

struct Negative {
  template <class T>
  struct apply : ::std::integral_constant<bool, (T::value < 0)> {};
};

TEST_F(EraseIf, Functional) {
  EXPECT_EQ(make_tuple(), erase_if<Negative>(make_tuple()));
  EXPECT_EQ(make_tuple(), erase_if<Negative>(make_tuple(x)));
  EXPECT_EQ(make_tuple(a), erase_if<Negative>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a), erase_if<Negative>(make_tuple(x, a)));
  EXPECT_EQ(make_tuple(a), erase_if<Negative>(make_tuple(a, x)));
  EXPECT_EQ(make_tuple(a, b), erase_if<Negative>(make_tuple(a, x, b)));
  EXPECT_EQ(make_tuple(a), erase_if<Negative>(make_tuple(x, a, y)));
}

}  // namespace
}  // namespace tuple
}  // namespace util
