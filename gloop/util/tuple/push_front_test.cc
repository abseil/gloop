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

#include "gloop/util/tuple/push_front.h"

#include <tuple>

#include "gloop/gloop_test.h"
#include "gloop/util/tuple/test_util.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_tuple;

class PushFront : public TestValues {};

TEST_F(PushFront, Push) {
  EXPECT_EQ(make_tuple(x), push_front(make_tuple(), x));
  EXPECT_EQ(make_tuple(x, a), push_front(make_tuple(a), x));
  EXPECT_EQ(make_tuple(x, a, b), push_front(make_tuple(a, b), x));
}

TEST_F(PushFront, InsertValue) {
  auto t = push_front(make_tuple(), x);
  EXPECT_NE(&x, &get<0>(t));
}

class PushFrontRef : public TestValues {};

TEST_F(PushFrontRef, Push) {
  EXPECT_EQ(make_tuple(x), push_front_ref(make_tuple(), x));
  EXPECT_EQ(make_tuple(x, a), push_front_ref(make_tuple(a), x));
  EXPECT_EQ(make_tuple(x, a, b), push_front_ref(make_tuple(a, b), x));
}

TEST_F(PushFrontRef, NonConstRef) {
  ::std::tuple<X&> t = push_front_ref(make_tuple(), x);
  EXPECT_EQ(&x, &get<0>(t));
}

TEST_F(PushFrontRef, ConstRef) {
  const X& cx = x;
  ::std::tuple<const X&> t = push_front_ref(make_tuple(), cx);
  EXPECT_EQ(&cx, &get<0>(t));
}

}  // namespace
}  // namespace tuple
}  // namespace util
