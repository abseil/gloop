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

#include "gloop/util/tuple/bindings/bindings.h"

#include <cstddef>
#include <tuple>
#include <utility>

#include "gtest/gtest.h"

namespace util::tuple::bindings {
namespace {

using ::testing::StaticAssertTypeEq;

template <class T, size_t N>
using field_types_t = typename bindings_traits_with_size<T, N>::field_types;

TEST(FieldTypes, Works) {
  struct S {
    int a;
    int& b;
    int&& c;
  };
  StaticAssertTypeEq<field_types_t<S, 3>, std::tuple<int, int&, int&&>>();
}

TEST(Fields, Works) {
  int b = 0, c = 0;
  struct {
    int a;
    int& b;
    int&& c;
  } x = {0, b, std::move(c)};
  int& a = x.a;
  const auto& y = x;
  {
    auto t = bindings_traits_with_size<decltype(x), 3>::field_refs(x);
    StaticAssertTypeEq<decltype(t), std::tuple<int&, int&, int&>>();
    EXPECT_EQ(&a, &std::get<0>(t));
    EXPECT_EQ(&b, &std::get<1>(t));
    EXPECT_EQ(&c, &std::get<2>(t));
  }
  {
    auto t =
        bindings_traits_with_size<decltype(x), 3>::field_refs(std::move(x));
    StaticAssertTypeEq<decltype(t), std::tuple<int&&, int&, int&&>>();
    EXPECT_EQ(&a, &std::get<0>(t));
    EXPECT_EQ(&b, &std::get<1>(t));
    EXPECT_EQ(&c, &std::get<2>(t));
  }
  {
    auto t = bindings_traits_with_size<decltype(x), 3>::field_refs(y);
    StaticAssertTypeEq<decltype(t), std::tuple<const int&, int&, int&>>();
    EXPECT_EQ(&a, &std::get<0>(t));
    EXPECT_EQ(&b, &std::get<1>(t));
    EXPECT_EQ(&c, &std::get<2>(t));
  }
  {
    auto t =
        bindings_traits_with_size<decltype(x), 3>::field_refs(std::move(y));
    StaticAssertTypeEq<decltype(t), std::tuple<const int&&, int&, int&&>>();
    EXPECT_EQ(&a, &std::get<0>(t));
    EXPECT_EQ(&b, &std::get<1>(t));
    EXPECT_EQ(&c, &std::get<2>(t));
  }
}

}  // namespace
}  // namespace util::tuple::bindings
