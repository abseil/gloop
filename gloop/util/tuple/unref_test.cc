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

#include "gloop/util/tuple/unref.h"

#include <tuple>
#include <type_traits>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_tuple;
using ::std::tie;

class Unref : public TestValues {};

TEST_F(Unref, Functional) {
  EXPECT_EQ(make_tuple(), unref(make_tuple()));
  EXPECT_EQ(make_tuple(a), unref(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), unref(make_tuple(a, b)));
}

TEST_F(Unref, NonConstValue) {
  ::std::tuple<A> t(a);
  const ::std::tuple<A> q(a);
  typedef decltype(unref(t)) T;
  typedef decltype(unref(q)) Q;
  EXPECT_TRUE((std::is_same<T, ::std::tuple<A>>::value));
  EXPECT_TRUE((std::is_same<Q, ::std::tuple<A>>::value));
}

TEST_F(Unref, ConstValue) {
  ::std::tuple<const A> t(a);
  const ::std::tuple<const A> q(a);
  typedef decltype(unref(t)) T;
  typedef decltype(unref(q)) Q;
  EXPECT_TRUE((std::is_same<T, ::std::tuple<const A>>::value));
  EXPECT_TRUE((std::is_same<Q, ::std::tuple<const A>>::value));
}

TEST_F(Unref, NonConstRef) {
  ::std::tuple<A&> t(a);
  const ::std::tuple<A&> q(a);
  typedef decltype(unref(t)) T;
  typedef decltype(unref(q)) Q;
  EXPECT_TRUE((std::is_same<T, ::std::tuple<A>>::value));
  EXPECT_TRUE((std::is_same<Q, ::std::tuple<A>>::value));
}

TEST_F(Unref, ConstRef) {
  ::std::tuple<const A&> t(a);
  const ::std::tuple<const A&> q(a);
  typedef decltype(unref(t)) T;
  typedef decltype(unref(q)) Q;
  EXPECT_TRUE((std::is_same<T, ::std::tuple<const A>>::value));
  EXPECT_TRUE((std::is_same<Q, ::std::tuple<const A>>::value));
}

}  // namespace
}  // namespace tuple
}  // namespace util
