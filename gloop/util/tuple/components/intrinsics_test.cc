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

#include "gloop/util/tuple/components/intrinsics.h"

#include <concepts>
#include <cstddef>
#include <string>
#include <type_traits>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {

namespace {

struct S {};
struct Q {};

struct TagS {};
struct TagQ {};

[[maybe_unused]] TagQ get_tuple_tag(Q q);

}  // namespace

template <>
struct tag<S> {
  using type = TagS;
};

template <>
struct intrinsics<TagS> {
  template <class... Elements>
  struct assemble {
    using type = S;
  };

  template <size_t N, class T>
  struct element : std::integral_constant<int, N> {
    static_assert(std::is_same_v<T, S>, "Wrong template argument");
  };

  template <class T>
  struct size : std::integral_constant<size_t, 42> {
    static_assert(std::is_same_v<T, S>, "Wrong template argument");
  };

  template <size_t N, class T>
  static int get(T&& t) {
    static_assert(std::is_same_v<std::decay_t<T>, S>,
                  "Wrong template argument");
    return N;
  }

  template <size_t N, class T>
  static constexpr const char* name() {
    if constexpr (N == 0) return "S0";
    if constexpr (N == 1) return "S1";
    if constexpr (N == 2) return "S2";
    if constexpr (N == 3) return "S3";
    if constexpr (N == 4) return "S4";
    return "S5+";
  }
};

template <>
struct intrinsics<TagQ> {
  using has_all_elements = std::true_type;

  template <size_t N, class T>
  struct element : std::integral_constant<int, N> {
    static_assert(std::is_same_v<T, Q>, "Wrong template argument");
  };

  template <class T>
  struct size : std::integral_constant<size_t, 42> {
    static_assert(std::is_same_v<T, Q>, "Wrong template argument");
  };

  template <size_t N, class T>
  static int get(T&& t) {
    static_assert(std::is_same_v<std::decay_t<T>, Q>,
                  "Wrong template argument");
    return N;
  }

  template <size_t N, class T>
  static constexpr const char* name() {
    if constexpr (N == 0) return "Q0";
    if constexpr (N == 1) return "Q1";
    if constexpr (N == 2) return "Q2";
    if constexpr (N == 3) return "Q3";
    if constexpr (N == 4) return "Q4";
    return "Q5+";
  }
};

namespace {

template <class T, size_t N, int R>
void VerifyElement() {
  EXPECT_TRUE((std::is_same_v<typename element<N, T>::type,
                              std::integral_constant<int, R>>));
}

TEST(Intrinsics, Element) {
  VerifyElement<S, 0, 0>();
  VerifyElement<S, 1, 1>();
  VerifyElement<Q, 0, 0>();
  VerifyElement<Q, 1, 1>();

  VerifyElement<const S, 0, 0>();
  VerifyElement<S&, 0, 0>();
  VerifyElement<volatile S, 0, 0>();
  VerifyElement<const volatile S&, 0, 0>();
  VerifyElement<const volatile S&&, 0, 0>();
  VerifyElement<const Q, 0, 0>();
  VerifyElement<Q&, 0, 0>();
  VerifyElement<volatile Q, 0, 0>();
  VerifyElement<const volatile Q&, 0, 0>();
  VerifyElement<const volatile Q&&, 0, 0>();
}

TEST(Intrinsics, Size) {
  EXPECT_EQ(size<S>::value, 42);
  EXPECT_EQ(size<const S>::value, 42);
  EXPECT_EQ(size<volatile S>::value, 42);
  EXPECT_EQ(size<S&>::value, 42);
  EXPECT_EQ(size<const volatile S&>::value, 42);
  EXPECT_EQ(size<const volatile S&&>::value, 42);

  EXPECT_EQ(size<Q>::value, 42);
  EXPECT_EQ(size<const Q>::value, 42);
  EXPECT_EQ(size<volatile Q>::value, 42);
  EXPECT_EQ(size<Q&>::value, 42);
  EXPECT_EQ(size<const volatile Q&>::value, 42);
  EXPECT_EQ(size<const volatile Q&&>::value, 42);
}

template <class T>
T Make() {
  return T();
}

TEST(Intrinsics, Get) {
  S s = {};
  const S cs = {};
  volatile S vs = {};
  const volatile S cvs = {};

  Q q = {};
  const Q cq = {};
  volatile Q vq = {};
  const volatile Q cvq = {};

  EXPECT_EQ(get<0>(s), 0);
  EXPECT_EQ(get<1>(s), 1);
  EXPECT_EQ(get<0>(q), 0);
  EXPECT_EQ(get<1>(q), 1);

  // With lvalues.
  EXPECT_EQ(get<0>(cs), 0);
  EXPECT_EQ(get<0>(vs), 0);
  EXPECT_EQ(get<0>(cvs), 0);
  EXPECT_EQ(get<0>(cq), 0);
  EXPECT_EQ(get<0>(vq), 0);
  EXPECT_EQ(get<0>(cvq), 0);

  // With rvalues.
  EXPECT_EQ(get<0>(Make<S>()), 0);
  EXPECT_EQ(get<0>(Make<const S>()), 0);
  EXPECT_EQ(get<0>(Make<Q>()), 0);
  EXPECT_EQ(get<0>(Make<const Q>()), 0);
}

TEST(Intrinsics, GetByType) {
  S s = {};
  const S cs = {};
  volatile S vs = {};
  const volatile S cvs = {};

  Q q = {};
  const Q cq = {};
  volatile Q vq = {};
  const volatile Q cvq = {};

  using Zero = std::integral_constant<int, 0>;
  using One = std::integral_constant<int, 1>;

  EXPECT_EQ(get<Zero>(s), 0);
  EXPECT_EQ(get<One>(s), 1);
  EXPECT_EQ(get<Zero>(q), 0);
  EXPECT_EQ(get<One>(q), 1);

  // With lvalues.
  EXPECT_EQ(get<Zero>(cs), 0);
  EXPECT_EQ(get<Zero>(vs), 0);
  EXPECT_EQ(get<Zero>(cvs), 0);
  EXPECT_EQ(get<Zero>(cq), 0);
  EXPECT_EQ(get<Zero>(vq), 0);
  EXPECT_EQ(get<Zero>(cvq), 0);

  // With rvalues.
  EXPECT_EQ(get<Zero>(Make<S>()), 0);
  EXPECT_EQ(get<Zero>(Make<const S>()), 0);
  EXPECT_EQ(get<Zero>(Make<Q>()), 0);
  EXPECT_EQ(get<Zero>(Make<const Q>()), 0);
}

TEST(Intrinsics, IndexOf) {
  using Zero = std::integral_constant<int, 0>;
  using One = std::integral_constant<int, 1>;

  EXPECT_EQ((index_of<Zero, S>()), 0);
  EXPECT_EQ((index_of<One, S>()), 1);
  EXPECT_NE((index_of<One, S>()), 2);
}

TEST(Intrinsics, Name) {
  EXPECT_STREQ((name<0, S>()), "S0");
  static_assert(absl::string_view(name<0, S>()) == "S0");
  EXPECT_STREQ((name<1, S>()), "S1");
  EXPECT_STREQ((name<0, Q>()), "Q0");
  EXPECT_STREQ((name<1, Q>()), "Q1");

  EXPECT_STREQ((name<0, const S>()), "S0");
  EXPECT_STREQ((name<0, volatile S>()), "S0");
  EXPECT_STREQ((name<0, S&>()), "S0");
  EXPECT_STREQ((name<0, const volatile S&>()), "S0");
  EXPECT_STREQ((name<0, const volatile S&&>()), "S0");
}

TEST(Intrinsics, HasAllElements) {
  // Not even a tuple.
  EXPECT_FALSE(has_all_elements<void>::value);
  EXPECT_FALSE(has_all_elements<int>::value);
  // Doesn't define the has_all_elements intrinsic.
  EXPECT_FALSE(has_all_elements<S>::value);
  // Defines the has_all_elements intrinsic.
  EXPECT_TRUE(has_all_elements<Q>::value);
}

TEST(Intrinsics, ModernAliases) {
  EXPECT_TRUE((std::same_as<tag_t<S>, TagS>));
  EXPECT_TRUE((std::same_as<tag_t<const S&>, TagS>));
  EXPECT_TRUE((std::same_as<tag_t<Q>, TagQ>));
  EXPECT_TRUE((std::same_as<tag_t<const volatile Q&&>, TagQ>));

  EXPECT_EQ(size_v<S>, 42);
  EXPECT_EQ(size_v<const S&>, 42);
  EXPECT_EQ(size_v<Q>, 42);
  EXPECT_EQ(size_v<const volatile Q&&>, 42);

  EXPECT_TRUE((std::same_as<element_t<0, S>, std::integral_constant<int, 0>>));
  EXPECT_TRUE(
      (std::same_as<element_t<1, const Q&>, std::integral_constant<int, 1>>));

  EXPECT_TRUE((std::same_as<assemble_t<TagS, int, char>, S>));

  EXPECT_FALSE(has_all_elements_v<void>);
  EXPECT_FALSE(has_all_elements_v<int>);
  EXPECT_FALSE(has_all_elements_v<S>);
  EXPECT_TRUE(has_all_elements_v<Q>);
}

}  // namespace

}  // namespace tuple
}  // namespace util
