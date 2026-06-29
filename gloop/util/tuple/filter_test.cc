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

#include "gloop/util/tuple/filter.h"

#include <cstddef>
#include <tuple>
#include <type_traits>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;

class FilterIndex : public TestValues {};

struct IndexEqualsValue {
  template <::size_t I, class T>
  struct apply : ::std::integral_constant<bool, I == T::value> {};
};

TEST_F(FilterIndex, Functional) {
  EXPECT_EQ(make_tuple(), filter_index<IndexEqualsValue>(make_tuple()));
  EXPECT_EQ(make_tuple(), filter_index<IndexEqualsValue>(make_tuple(x)));
  EXPECT_EQ(make_tuple(a), filter_index<IndexEqualsValue>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a), filter_index<IndexEqualsValue>(make_tuple(a, x)));
  EXPECT_EQ(make_tuple(b), filter_index<IndexEqualsValue>(make_tuple(x, b)));
  EXPECT_EQ(make_tuple(a, c),
            filter_index<IndexEqualsValue>(make_tuple(a, x, c)));
  EXPECT_EQ(make_tuple(b), filter_index<IndexEqualsValue>(make_tuple(x, b, y)));
}

class Filter : public TestValues {};

struct NonNegative {
  template <class T>
  struct apply : ::std::integral_constant<bool, (T::value >= 0)> {};
};

TEST_F(Filter, Functional) {
  EXPECT_EQ(make_tuple(), filter<NonNegative>(make_tuple()));
  EXPECT_EQ(make_tuple(), filter<NonNegative>(make_tuple(x)));
  EXPECT_EQ(make_tuple(a), filter<NonNegative>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a), filter<NonNegative>(make_tuple(x, a)));
  EXPECT_EQ(make_tuple(a), filter<NonNegative>(make_tuple(a, x)));
  EXPECT_EQ(make_tuple(a, b), filter<NonNegative>(make_tuple(a, x, b)));
  EXPECT_EQ(make_tuple(a), filter<NonNegative>(make_tuple(x, a, y)));
}

TEST_F(FilterIndex, Constexpr) {
  constexpr auto kTuple = make_tuple(::std::integral_constant<::size_t, 0>{},
                                     ::std::integral_constant<::size_t, 5>{});
  constexpr auto kFiltered = filter_index<IndexEqualsValue>(kTuple);
  constexpr auto kExpected =
      make_tuple(::std::integral_constant<::size_t, 0>{});
  EXPECT_EQ(kExpected, kFiltered);
}

TEST_F(Filter, Constexpr) {
  constexpr auto kTuple = make_tuple(::std::integral_constant<int, -1>{},
                                     ::std::integral_constant<int, 42>{});
  constexpr auto kFiltered = filter<NonNegative>(kTuple);
  constexpr auto kExpected = make_tuple(::std::integral_constant<int, 42>{});
  EXPECT_EQ(kExpected, kFiltered);
}

}  // namespace
}  // namespace tuple
}  // namespace util
