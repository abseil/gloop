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

#include "gloop/util/tuple/transform.h"

#include <stddef.h>

#include <tuple>
#include <utility>

#include "gloop/util/tuple/std_tuple.h"
#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_pair;
using ::std::make_tuple;
using ::std::tie;
using ::std::tuple;

struct GetAddr {
  template <class T>
  T* operator()(T& val) const {
    return &val;
  }
};

struct GetRef {
  template <class T>
  T& operator()(T& val) const {
    return val;
  }
};

struct GetVal {
  template <class T>
  constexpr T operator()(const T& val) const {
    return val;
  }
};

struct GetTypeVal {
  template <class T>
  constexpr int operator()() const {
    return T::value;
  }
};

// The important part is that it's a free function. That's what we want to test.
template <class T>
constexpr T Identity(const T& t) {
  return t;
}

class Transform : public TestValues {};

TEST_F(Transform, WithValues) {
  EXPECT_EQ(make_tuple(), transform<std_tuple_tag>(GetAddr(), tie()));
  EXPECT_EQ(make_tuple(&a), transform<std_tuple_tag>(GetAddr(), tie(a)));
  EXPECT_EQ(make_tuple(&a, &b), transform<std_tuple_tag>(GetAddr(), tie(a, b)));
  EXPECT_EQ(make_tuple(), transform(GetAddr(), tie()));
  EXPECT_EQ(make_tuple(&a), transform(GetAddr(), tie(a)));
  EXPECT_EQ(make_tuple(&a, &b), transform(GetAddr(), tie(a, b)));
}

TEST_F(Transform, WithTypes) {
  EXPECT_EQ(make_tuple(), (transform<std_tuple_tag, tuple<>>(GetTypeVal())));
  EXPECT_EQ(make_tuple(1), (transform<std_tuple_tag, tuple<B>>(GetTypeVal())));
  EXPECT_EQ(make_tuple(1, 2),
            (transform<std_tuple_tag, tuple<B, C>>(GetTypeVal())));
  EXPECT_EQ(make_tuple(), transform<tuple<>>(GetTypeVal()));
  EXPECT_EQ(make_tuple(1), transform<tuple<B>>(GetTypeVal()));
  EXPECT_EQ(make_tuple(1, 2), (transform<tuple<B, C>>(GetTypeVal())));
}

TEST_F(Transform, Constexpr) {
  constexpr tuple<int, char> kTuple(42, 'A');
  constexpr auto kTransformed = transform(GetVal(), kTuple);
  EXPECT_EQ(kTuple, kTransformed);
}

TEST_F(Transform, ConstexprGenerator) {
  constexpr auto kTransformed = transform<tuple<B, C>>(GetTypeVal());
  constexpr auto kExpected = make_tuple(1, 2);
  EXPECT_EQ(kExpected, kTransformed);
}

TEST_F(Transform, NonConstRef) {
  ::std::tuple<A> t(a);
  {
    ::std::tuple<A&> q = transform<std_tuple_tag>(GetRef(), t);
    EXPECT_EQ(&get<0>(t), &get<0>(q));
  }
  {
    ::std::tuple<A&> q = transform(GetRef(), t);
    EXPECT_EQ(&get<0>(t), &get<0>(q));
  }
}

TEST_F(Transform, ConstRef) {
  const ::std::tuple<A> t(a);
  {
    ::std::tuple<const A&> q = transform<std_tuple_tag>(GetRef(), t);
    EXPECT_EQ(&get<0>(t), &get<0>(q));
  }
  {
    ::std::tuple<const A&> q = transform(GetRef(), t);
    EXPECT_EQ(&get<0>(t), &get<0>(q));
  }
}

TEST_F(Transform, Val) {
  ::std::tuple<A> t(a);
  {
    auto q = transform<std_tuple_tag>(GetVal(), t);
    EXPECT_NE(&get<0>(t), &get<0>(q));
  }
  {
    auto q = transform(GetVal(), t);
    EXPECT_NE(&get<0>(t), &get<0>(q));
  }
}

TEST_F(Transform, FreeFunction) {
  EXPECT_EQ(make_tuple(a), transform<std_tuple_tag>(Identity<A>, tie(a)));
  EXPECT_EQ(make_tuple(a), transform<std_tuple_tag>(&Identity<A>, tie(a)));
  EXPECT_EQ(make_tuple(a), transform(Identity<A>, tie(a)));
  EXPECT_EQ(make_tuple(a), transform(&Identity<A>, tie(a)));
}

struct MakePair {
  template <::size_t N, class T>
  constexpr std::pair<int, T> operator()(const T& val) const {
    return {N, val};
  }
};

class TransformIndex : public TestValues {};

TEST_F(TransformIndex, WithValues) {
  EXPECT_EQ(make_tuple(), transform_index<std_tuple_tag>(MakePair(), tie()));
  EXPECT_EQ(make_tuple(make_pair(0, a)),
            transform_index<std_tuple_tag>(MakePair(), tie(a)));
  EXPECT_EQ(make_tuple(make_pair(0, a), make_pair(1, b)),
            transform_index<std_tuple_tag>(MakePair(), tie(a, b)));
  EXPECT_EQ(make_tuple(), transform_index(MakePair(), tie()));
  EXPECT_EQ(make_tuple(make_pair(0, a)), transform_index(MakePair(), tie(a)));
  EXPECT_EQ(make_tuple(make_pair(0, a), make_pair(1, b)),
            transform_index(MakePair(), tie(a, b)));
}

struct MakeTypePair {
  template <::size_t N, class T>
  constexpr std::pair<int, int> operator()() const {
    return {N, T::value};
  }
};

TEST_F(TransformIndex, WithTypes) {
  EXPECT_EQ(make_tuple(),
            (transform_index<std_tuple_tag, tuple<>>(MakeTypePair())));
  EXPECT_EQ(make_tuple(make_pair(0, 1)),
            (transform_index<std_tuple_tag, tuple<B>>(MakeTypePair())));
  EXPECT_EQ(make_tuple(make_pair(0, 1), make_pair(1, 2)),
            (transform_index<std_tuple_tag, tuple<B, C>>(MakeTypePair())));
  EXPECT_EQ(make_tuple(), transform_index<tuple<>>(MakeTypePair()));
  EXPECT_EQ(make_tuple(make_pair(0, 1)),
            transform_index<tuple<B>>(MakeTypePair()));
  EXPECT_EQ(make_tuple(make_pair(0, 1), make_pair(1, 2)),
            (transform_index<tuple<B, C>>(MakeTypePair())));
}

TEST_F(TransformIndex, Constexpr) {
  constexpr tuple<char, double> kTuple('A', 2.5);
  constexpr auto kTransformed = transform_index(MakePair(), kTuple);
  constexpr auto kExpected = make_tuple(make_pair(0, 'A'), make_pair(1, 2.5));
  EXPECT_EQ(kExpected, kTransformed);
}

TEST_F(TransformIndex, ConstexprGenerator) {
  constexpr auto kTransformed = transform_index<tuple<B, C>>(MakeTypePair());
  constexpr auto kExpected = make_tuple(make_pair(0, 1), make_pair(1, 2));
  EXPECT_EQ(kExpected, kTransformed);
}

}  // namespace
}  // namespace tuple
}  // namespace util
