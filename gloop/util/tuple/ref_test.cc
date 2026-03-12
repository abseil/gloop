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

#include "gloop/util/tuple/ref.h"

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

class Ref : public TestValues {};

TEST_F(Ref, Functional) {
  EXPECT_EQ(make_tuple(), tuple::ref<std_tuple_tag>(make_tuple()));
  EXPECT_EQ(make_tuple(a), tuple::ref<std_tuple_tag>(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), tuple::ref<std_tuple_tag>(make_tuple(a, b)));
  EXPECT_EQ(make_tuple(a, b), tuple::ref<std_tuple_tag>(make_pair(a, b)));
  EXPECT_EQ(make_tuple(), tuple::ref(make_tuple()));
  EXPECT_EQ(make_tuple(a), tuple::ref(make_tuple(a)));
  EXPECT_EQ(make_tuple(a, b), tuple::ref(make_tuple(a, b)));
}

TEST_F(Ref, NonConstValue) {
  ::std::tuple<A> t(a);
  const ::std::tuple<A> q(a);
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref<std_tuple_tag>(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref<std_tuple_tag>(q)));
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref(q)));
}

TEST_F(Ref, ConstValue) {
  ::std::tuple<const A> t(a);
  const ::std::tuple<const A> q(a);
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref<std_tuple_tag>(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref<std_tuple_tag>(q)));
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref(q)));
}

TEST_F(Ref, NonConstRef) {
  ::std::tuple<A&> t(a);
  const ::std::tuple<A&> q(a);
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref<std_tuple_tag>(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref<std_tuple_tag>(q)));
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref(q)));
}

TEST_F(Ref, ConstRef) {
  ::std::tuple<const A&> t(a);
  const ::std::tuple<const A&> q(a);
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref<std_tuple_tag>(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref<std_tuple_tag>(q)));
  EXPECT_EQ(&get<0>(t), &get<0>(tuple::ref(t)));
  EXPECT_EQ(&get<0>(q), &get<0>(tuple::ref(q)));
}

}  // namespace
}  // namespace tuple
}  // namespace util
