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

#include "gloop/util/tuple/components/compressed_tuple.h"

#include <type_traits>
#include <utility>

#include "gloop/util/gtl/compressed_tuple.h"
#include "gloop/util/tuple/components/intrinsics.h"
#include "gtest/gtest.h"

namespace {

using util::tuple::assemble;
using util::tuple::compressed_tuple_tag;
using util::tuple::element;
using util::tuple::get;
using util::tuple::has_all_elements;
using util::tuple::name;
using util::tuple::size;
using util::tuple::tag;

struct Empty {
  friend constexpr bool operator==(Empty, Empty) { return true; }
};
using T = gtl::CompressedTuple<int, char, Empty>;

struct D : T {
  using T::T;
};

TEST(CompressedTuple, Tag) {
  EXPECT_TRUE((std::is_same_v<tag<T>::type, compressed_tuple_tag>));
  EXPECT_TRUE((std::is_same_v<tag<D>::type, compressed_tuple_tag>));
}

TEST(CompressedTuple, Assemble) {
  EXPECT_TRUE(
      (std::is_same_v<assemble<compressed_tuple_tag, int, char, Empty>::type,
                      T>));
}

TEST(CompressedTuple, Element) {
  EXPECT_TRUE((std::is_same_v<element<0, T>::type, int>));
  EXPECT_TRUE((std::is_same_v<element<1, T>::type, char>));
  EXPECT_TRUE((std::is_same_v<element<2, T>::type, Empty>));
  EXPECT_TRUE((std::is_same_v<element<0, D>::type, int>));
  EXPECT_TRUE((std::is_same_v<element<1, D>::type, char>));
  EXPECT_TRUE((std::is_same_v<element<2, D>::type, Empty>));
}

TEST(CompressedTuple, Size) {
  EXPECT_EQ(0, (size<gtl::CompressedTuple<>>::value));
  EXPECT_EQ(1, (size<gtl::CompressedTuple<int>>::value));
  EXPECT_EQ(2, (size<gtl::CompressedTuple<int, int>>::value));
  EXPECT_EQ(3, (size<gtl::CompressedTuple<int, int, Empty>>::value));
  EXPECT_EQ(3, (size<T>::value));
  EXPECT_EQ(3, (size<D>::value));
}

TEST(CompressedTuple, GetImpl) {
  T t;
  D d;

  // Assignment to fields.
  get<0>(t) = 42;
  get<1>(t) = 'A';
  get<2>(t) = {};
  get<0>(d) = 42;
  get<1>(d) = 'A';
  get<2>(d) = {};

  // Non-const getter.
  EXPECT_EQ(42, get<0>(t));
  EXPECT_EQ('A', get<1>(t));
  EXPECT_EQ(Empty(), get<2>(t));
  EXPECT_EQ(42, get<0>(d));
  EXPECT_EQ('A', get<1>(d));
  EXPECT_EQ(Empty(), get<2>(d));

  // Const getter.
  const T& ct = t;
  EXPECT_EQ(42, get<0>(ct));
  EXPECT_EQ('A', get<1>(ct));
  EXPECT_EQ(Empty(), get<2>(ct));
  const D& cd = d;
  EXPECT_EQ(42, get<0>(cd));
  EXPECT_EQ('A', get<1>(cd));
  EXPECT_EQ(Empty(), get<2>(cd));
}

TEST(CompressedTuple, Name) {
  EXPECT_EQ(nullptr, (name<0, T>()));
  EXPECT_EQ(nullptr, (name<1, T>()));
  EXPECT_EQ(nullptr, (name<2, T>()));
  EXPECT_EQ(nullptr, (name<0, D>()));
  EXPECT_EQ(nullptr, (name<1, D>()));
  EXPECT_EQ(nullptr, (name<2, D>()));
}

TEST(CompressedTuple, HasAllElements) {
  EXPECT_TRUE(has_all_elements<T>::value);
  EXPECT_TRUE(has_all_elements<D>::value);
}

}  // namespace
