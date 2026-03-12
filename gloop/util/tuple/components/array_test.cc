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

// Copyright 2013 Google Inc. All Rights Reserved.

#include "gloop/util/tuple/components/array.h"

#include <array>
#include <type_traits>

#include "gloop/util/tuple/components/intrinsics.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

typedef ::std::array<int, 2> A;

struct D : A {
  using A::A;
};

TEST(Array, Tag) {
  EXPECT_TRUE((::std::is_same<tag<A>::type, array_tag>::value));
  EXPECT_TRUE((::std::is_same<tag<D>::type, array_tag>::value));
}

TEST(Array, Assemble) {
  EXPECT_TRUE((::std::is_same<assemble<array_tag, int, int>::type, A>::value));
}

TEST(Array, Element) {
  EXPECT_TRUE((::std::is_same<element<0, A>::type, int>::value));
  EXPECT_TRUE((::std::is_same<element<1, A>::type, int>::value));
  EXPECT_TRUE((::std::is_same<element<0, D>::type, int>::value));
  EXPECT_TRUE((::std::is_same<element<1, D>::type, int>::value));
}

TEST(Array, Size) {
  EXPECT_EQ(2, size<A>::value);
  EXPECT_EQ(2, size<D>::value);
}

TEST(Array, GetImpl) {
  A a;
  D d;

  // Assignment to fields.
  get<0>(a) = 42;
  get<1>(a) = 24;
  get<0>(d) = 42;
  get<1>(d) = 24;

  // Non-const getter.
  EXPECT_EQ(42, get<0>(a));
  EXPECT_EQ(24, get<1>(a));
  EXPECT_EQ(42, get<0>(d));
  EXPECT_EQ(24, get<1>(d));

  // Const getter.
  const A& ca = a;
  EXPECT_EQ(42, get<0>(ca));
  EXPECT_EQ(24, get<1>(ca));
  const D& cd = d;
  EXPECT_EQ(42, get<0>(cd));
  EXPECT_EQ(24, get<1>(cd));
}

TEST(Array, Name) {
  EXPECT_EQ(nullptr, (name<0, A>()));
  EXPECT_EQ(nullptr, (name<1, A>()));
  EXPECT_EQ(nullptr, (name<0, D>()));
  EXPECT_EQ(nullptr, (name<1, D>()));
}

TEST(Array, HasAllElements) {
  EXPECT_TRUE(has_all_elements<A>::value);
  EXPECT_TRUE(has_all_elements<D>::value);
}

}  // namespace
}  // namespace tuple
}  // namespace util
