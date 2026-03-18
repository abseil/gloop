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

#include "gloop/util/tuple/components/pair.h"

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/components/intrinsics.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

typedef std::pair<int, char> P;

struct D : P {
  using P::P;
};

TEST(Pair, Tag) {
  EXPECT_TRUE((::std::is_same<tag<P>::type, pair_tag>::value));
  EXPECT_TRUE((::std::is_same<tag<D>::type, pair_tag>::value));
}

TEST(Pair, Assemble) {
  EXPECT_TRUE((::std::is_same<assemble<pair_tag, int, char>::type, P>::value));
}

TEST(Pair, Element) {
  EXPECT_TRUE((::std::is_same<element<0, P>::type, int>::value));
  EXPECT_TRUE((::std::is_same<element<1, P>::type, char>::value));
  EXPECT_TRUE((::std::is_same<element<0, D>::type, int>::value));
  EXPECT_TRUE((::std::is_same<element<1, D>::type, char>::value));
}

TEST(Pair, Size) {
  EXPECT_EQ(2, size<P>::value);
  EXPECT_EQ(2, size<D>::value);
}

TEST(Pair, GetImpl) {
  P p;
  D d;

  // Assignment to fields.
  get<0>(p) = 42;
  get<1>(p) = 'A';
  get<0>(d) = 42;
  get<1>(d) = 'A';

  // Non-const getter.
  EXPECT_EQ(42, get<0>(p));
  EXPECT_EQ('A', get<1>(p));
  EXPECT_EQ(42, get<0>(d));
  EXPECT_EQ('A', get<1>(d));

  // Const getter.
  const P& cp = p;
  EXPECT_EQ(42, get<0>(cp));
  EXPECT_EQ('A', get<1>(cp));
  const D& cd = d;
  EXPECT_EQ(42, get<0>(cd));
  EXPECT_EQ('A', get<1>(cd));
}

TEST(Pair, Name) {
  EXPECT_EQ(nullptr, (name<0, P>()));
  EXPECT_EQ(nullptr, (name<1, P>()));
  EXPECT_EQ(nullptr, (name<0, D>()));
  EXPECT_EQ(nullptr, (name<1, D>()));
}

TEST(Pair, HasAllElements) {
  EXPECT_TRUE(has_all_elements<P>::value);
  EXPECT_TRUE(has_all_elements<D>::value);
}

}  // namespace
}  // namespace tuple
}  // namespace util
