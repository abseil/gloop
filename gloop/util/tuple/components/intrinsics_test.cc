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

#include <cstddef>
#include <string>
#include <type_traits>

#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"

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
  typedef TagS type;
};

template <>
struct intrinsics<TagS> {
  template <::size_t N, class T>
  struct element : ::std::integral_constant<int, N> {
    static_assert(::std::is_same<T, S>::value, "Wrong template argument");
  };

  template <class T>
  struct size : ::std::integral_constant<::size_t, 42> {
    static_assert(::std::is_same<T, S>::value, "Wrong template argument");
  };

  template <::size_t N, class T>
  static int get(T&& t) {
    static_assert(::std::is_same<typename ::std::decay<T>::type, S>::value,
                  "Wrong template argument");
    return N;
  }

  template <::size_t N, class T>
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

  template <::size_t N, class T>
  struct element : ::std::integral_constant<int, N> {
    static_assert(::std::is_same<T, Q>::value, "Wrong template argument");
  };

  template <class T>
  struct size : ::std::integral_constant<::size_t, 42> {
    static_assert(::std::is_same<T, Q>::value, "Wrong template argument");
  };

  template <::size_t N, class T>
  static int get(T&& t) {
    static_assert(::std::is_same<typename ::std::decay<T>::type, Q>::value,
                  "Wrong template argument");
    return N;
  }

  template <::size_t N, class T>
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

template <class T, ::size_t N, int R>
void VerifyElement() {
  EXPECT_TRUE((::std::is_same<typename element<N, T>::type,
                              ::std::integral_constant<int, R>>::value));
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
  EXPECT_EQ(42, size<S>::value);
  EXPECT_EQ(42, size<const S>::value);
  EXPECT_EQ(42, size<volatile S>::value);
  EXPECT_EQ(42, size<S&>::value);
  EXPECT_EQ(42, size<const volatile S&>::value);
  EXPECT_EQ(42, size<const volatile S&&>::value);

  EXPECT_EQ(42, size<Q>::value);
  EXPECT_EQ(42, size<const Q>::value);
  EXPECT_EQ(42, size<volatile Q>::value);
  EXPECT_EQ(42, size<Q&>::value);
  EXPECT_EQ(42, size<const volatile Q&>::value);
  EXPECT_EQ(42, size<const volatile Q&&>::value);
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

  EXPECT_EQ(0, get<0>(s));
  EXPECT_EQ(1, get<1>(s));
  EXPECT_EQ(0, get<0>(q));
  EXPECT_EQ(1, get<1>(q));

  // With lvalues.
  EXPECT_EQ(0, get<0>(cs));
  EXPECT_EQ(0, get<0>(vs));
  EXPECT_EQ(0, get<0>(cvs));
  EXPECT_EQ(0, get<0>(cq));
  EXPECT_EQ(0, get<0>(vq));
  EXPECT_EQ(0, get<0>(cvq));

  // With rvalues.
  EXPECT_EQ(0, get<0>(Make<S>()));
  EXPECT_EQ(0, get<0>(Make<const S>()));
  EXPECT_EQ(0, get<0>(Make<Q>()));
  EXPECT_EQ(0, get<0>(Make<const Q>()));
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

  typedef ::std::integral_constant<int, 0> Zero;
  typedef ::std::integral_constant<int, 1> One;

  EXPECT_EQ(0, get<Zero>(s));
  EXPECT_EQ(1, get<One>(s));
  EXPECT_EQ(0, get<Zero>(q));
  EXPECT_EQ(1, get<One>(q));

  // With lvalues.
  EXPECT_EQ(0, get<Zero>(cs));
  EXPECT_EQ(0, get<Zero>(vs));
  EXPECT_EQ(0, get<Zero>(cvs));
  EXPECT_EQ(0, get<Zero>(cq));
  EXPECT_EQ(0, get<Zero>(vq));
  EXPECT_EQ(0, get<Zero>(cvq));

  // With rvalues.
  EXPECT_EQ(0, get<Zero>(Make<S>()));
  EXPECT_EQ(0, get<Zero>(Make<const S>()));
  EXPECT_EQ(0, get<Zero>(Make<Q>()));
  EXPECT_EQ(0, get<Zero>(Make<const Q>()));
}

TEST(Intrinsics, IndexOf) {
  typedef ::std::integral_constant<int, 0> Zero;
  typedef ::std::integral_constant<int, 1> One;

  EXPECT_EQ(0, (index_of<Zero, S>()));
  EXPECT_EQ(1, (index_of<One, S>()));
  EXPECT_NE(2, (index_of<One, S>()));
}

TEST(Intrinsics, Name) {
  EXPECT_STREQ("S0", (name<0, S>()));
  static_assert(absl::string_view(name<0, S>()) == "S0");
  EXPECT_STREQ("S1", (name<1, S>()));
  EXPECT_STREQ("Q0", (name<0, Q>()));
  EXPECT_STREQ("Q1", (name<1, Q>()));

  EXPECT_STREQ("S0", (name<0, const S>()));
  EXPECT_STREQ("S0", (name<0, volatile S>()));
  EXPECT_STREQ("S0", (name<0, S&>()));
  EXPECT_STREQ("S0", (name<0, const volatile S&>()));
  EXPECT_STREQ("S0", (name<0, const volatile S&&>()));
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

}  // namespace

}  // namespace tuple
}  // namespace util
