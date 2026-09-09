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

#include "gloop/util/tuple/zip.h"

#include <stddef.h>

#include <tuple>
#include <utility>

#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"
#include "gloop/util/tuple/struct.h"
#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;
using ::std::pair;
using ::std::tie;
using ::std::tuple;

class Zip : public TestValues {};

TEST_F(Zip, NoArgs) { EXPECT_EQ(make_tuple(), zip()); }

TEST_F(Zip, OneArg) {
  EXPECT_EQ(make_tuple(), zip(make_tuple()));
  EXPECT_EQ(make_tuple(make_tuple(a)), zip(make_tuple(a)));
  EXPECT_EQ(make_tuple(make_tuple(a), make_tuple(b)), zip(make_tuple(a, b)));
}

TEST_F(Zip, TwoArgs) {
  EXPECT_EQ(make_tuple(), zip(make_tuple(), make_tuple()));
  EXPECT_EQ(make_tuple(), zip(make_tuple(), make_tuple(a)));
  EXPECT_EQ(make_tuple(), zip(make_tuple(a), make_tuple()));
  EXPECT_EQ(make_tuple(make_tuple(a, b)), zip(make_tuple(a), make_tuple(b)));
  EXPECT_EQ(make_tuple(make_tuple(a, c)), zip(make_tuple(a, b), make_tuple(c)));
  EXPECT_EQ(make_tuple(make_tuple(a, b)), zip(make_tuple(a), make_tuple(b, c)));
  EXPECT_EQ(make_tuple(make_tuple(a, c), make_tuple(b, d)),
            zip(make_tuple(a, b), make_tuple(c, d)));
}

TEST_F(Zip, Values) {
  auto t = make_tuple(a);
  auto q = zip(t);
  EXPECT_NE(&get<0>(get<0>(q)), &get<0>(t));
}

TEST_F(Zip, Refs) {
  auto t = zip(tie(a));
  EXPECT_EQ(&get<0>(get<0>(t)), &a);
}

TEST_F(Zip, ExplicitTagNoArgs) {
  EXPECT_EQ(make_tuple(), zip<std_tuple_tag>());
}

TEST_F(Zip, ExplicitTagWithArgs) {
  auto t = make_tuple(a, b);
  auto q = make_tuple(c, d);
  pair<pair<A, C>, pair<B, D>> r = zip<pair_tag>(t, q);
  (void)r;
}

class ZipRef : public TestValues {};

TEST_F(ZipRef, NoArgs) { EXPECT_EQ(make_tuple(), zip_ref()); }

TEST_F(ZipRef, OneArg) {
  EXPECT_EQ(make_tuple(), zip_ref(make_tuple()));
  EXPECT_EQ(make_tuple(make_tuple(a)), zip_ref(make_tuple(a)));
  EXPECT_EQ(make_tuple(make_tuple(a), make_tuple(b)),
            zip_ref(make_tuple(a, b)));
}

TEST_F(ZipRef, TwoArgs) {
  EXPECT_EQ(make_tuple(), zip_ref(make_tuple(), make_tuple()));
  EXPECT_EQ(make_tuple(), zip_ref(make_tuple(), make_tuple(a)));
  EXPECT_EQ(make_tuple(), zip_ref(make_tuple(a), make_tuple()));
  EXPECT_EQ(make_tuple(make_tuple(a, b)),
            zip_ref(make_tuple(a), make_tuple(b)));
  EXPECT_EQ(make_tuple(make_tuple(a, c)),
            zip_ref(make_tuple(a, b), make_tuple(c)));
  EXPECT_EQ(make_tuple(make_tuple(a, b)),
            zip_ref(make_tuple(a), make_tuple(b, c)));
  EXPECT_EQ(make_tuple(make_tuple(a, c), make_tuple(b, d)),
            zip_ref(make_tuple(a, b), make_tuple(c, d)));
}

TEST_F(ZipRef, Values) {
  auto t = make_tuple(a);
  auto q = zip_ref(t);
  EXPECT_EQ(&get<0>(get<0>(q)), &get<0>(t));
}

TEST_F(ZipRef, Refs) {
  auto t = zip_ref(tie(a));
  EXPECT_EQ(&get<0>(get<0>(t)), &a);
}

TEST_F(ZipRef, ExplicitTagNoArgs) {
  EXPECT_EQ(make_tuple(), zip_ref<std_tuple_tag>());
}

TEST_F(ZipRef, ExplicitTagWithArgs) {
  auto t = make_tuple(a, b);
  auto q = make_tuple(c, d);
  pair<pair<A, C>, pair<B, D>> r = zip_ref<pair_tag>(t, q);
  (void)r;
}

struct S {
  TUPLE_DEFINE_STRUCT(S, (), (int, x));
};

TEST_F(ZipRef, ZipStructs) {
  S s = {};
  tuple<tuple<int&, int&>> t = zip_ref<std_tuple_tag>(s, s);
  EXPECT_EQ(&s.x, &get<0>(get<0>(t)));
  EXPECT_EQ(&s.x, &get<1>(get<0>(t)));
}

}  // namespace
}  // namespace tuple
}  // namespace util
