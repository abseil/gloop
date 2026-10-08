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

#include "gloop/util/tuple/find_if.h"

#include <tuple>
#include <type_traits>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::tuple;

template <class T>
struct TypeEqualTo {
  template <class U>
  struct apply : ::std::is_same<T, U> {};
};

class FindIndexIf : public TestValues {};

TEST_F(FindIndexIf, Empty) {
  EXPECT_EQ(-1, (find_index_if<TypeEqualTo<A>, tuple<>>::value));
}

TEST_F(FindIndexIf, OneElement) {
  EXPECT_EQ(0, (find_index_if<TypeEqualTo<A>, tuple<A>>::value));
  EXPECT_EQ(-1, (find_index_if<TypeEqualTo<B>, tuple<A>>::value));
}

TEST_F(FindIndexIf, TwoElements) {
  EXPECT_EQ(0, (find_index_if<TypeEqualTo<A>, tuple<A, B>>::value));
  EXPECT_EQ(1, (find_index_if<TypeEqualTo<B>, tuple<A, B>>::value));
  EXPECT_EQ(-1, (find_index_if<TypeEqualTo<C>, tuple<A, B>>::value));
}

class FindIf : public TestValues {};

TEST_F(FindIf, NonConst) {
  tuple<A, B> t(a, b);
  A* p = &find_if<TypeEqualTo<A>>(t);
  B* q = &find_if<TypeEqualTo<B>>(t);
  EXPECT_EQ(&get<0>(t), p);
  EXPECT_EQ(&get<1>(t), q);
}

TEST_F(FindIf, Const) {
  const tuple<A, B> t(a, b);
  const A* p = &find_if<TypeEqualTo<A>>(t);
  const B* q = &find_if<TypeEqualTo<B>>(t);
  EXPECT_EQ(&get<0>(t), p);
  EXPECT_EQ(&get<1>(t), q);
}

}  // namespace
}  // namespace tuple
}  // namespace util
