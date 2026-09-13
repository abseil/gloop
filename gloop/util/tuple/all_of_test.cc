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

#include "gloop/util/tuple/all_of.h"

#include <stddef.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

struct ValueBiggerThanSize {
  template <class Elem>
  bool operator()(const Elem& elem) const {
    return elem > sizeof(elem);
  }
};

struct SizeOfLessThan {
  template <class Elem>
  bool operator()() const {
    return sizeof(Elem) < value;
  }
  size_t value;
};

TEST(AllOf, Empty) {
  EXPECT_TRUE(all_of(ValueBiggerThanSize(), ::std::make_tuple()));
  EXPECT_TRUE(all_of<::std::tuple<>>(SizeOfLessThan{1}));
}

TEST(AllOf, BinaryMatrix) {
  EXPECT_FALSE(all_of(ValueBiggerThanSize(), ::std::make_tuple(1, 1)));
  EXPECT_FALSE(all_of(ValueBiggerThanSize(), ::std::make_tuple(1, 5)));
  EXPECT_FALSE(all_of(ValueBiggerThanSize(), ::std::make_tuple(5, 1)));
  EXPECT_TRUE(all_of(ValueBiggerThanSize(), ::std::make_tuple(5, 5)));

  EXPECT_FALSE((all_of<::std::tuple<::int32_t, ::int32_t>>(SizeOfLessThan{3})));
  EXPECT_FALSE((all_of<::std::tuple<int32_t, ::int8_t>>(SizeOfLessThan{3})));
  EXPECT_FALSE((all_of<::std::tuple<::int8_t, ::int32_t>>(SizeOfLessThan{3})));
  EXPECT_TRUE((all_of<::std::tuple<::int8_t, ::int8_t>>(SizeOfLessThan{3})));
}

TEST(AllOf, Mutable) {
  auto dec = [](int& n) { return --n; };
  ::std::tuple<int, int> t(2, 3);
  EXPECT_TRUE(all_of(dec, t));
  EXPECT_EQ(::std::make_tuple(1, 2), t);
  EXPECT_FALSE(all_of(dec, t));
  EXPECT_EQ(::std::make_tuple(0, 2), t);
}

template <::size_t N>
struct MetaSizeofLessThan {
  template <class Elem>
  struct apply : ::std::integral_constant<bool, (sizeof(Elem) < N)> {};
};

TEST(AllTypesOf, Empty) {
  EXPECT_TRUE((all_types_of<MetaSizeofLessThan<3>, ::std::tuple<>>::value));
}

TEST(AllTypesOf, BinaryMatrix) {
  EXPECT_FALSE((all_types_of<MetaSizeofLessThan<3>,
                             ::std::tuple<::int32_t, ::int32_t>>::value));
  EXPECT_FALSE((all_types_of<MetaSizeofLessThan<3>,
                             ::std::tuple<::int32_t, ::int8_t>>::value));
  EXPECT_FALSE((all_types_of<MetaSizeofLessThan<3>,
                             ::std::tuple<::int8_t, ::int32_t>>::value));
  EXPECT_TRUE((all_types_of<MetaSizeofLessThan<3>,
                            ::std::tuple<::int8_t, ::int8_t>>::value));
}

}  // namespace
}  // namespace tuple
}  // namespace util
