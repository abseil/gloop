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

#include "gloop/util/tuple/reverse.h"

#include <tuple>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::make_tuple;
using ::std::tie;

class Reverse : public TestValues {};

TEST_F(Reverse, Functional) {
  EXPECT_EQ(make_tuple(), reverse(make_tuple()));
  EXPECT_EQ(make_tuple(a), reverse(make_tuple(a)));
  EXPECT_EQ(make_tuple(b, a), reverse(make_tuple(a, b)));
  EXPECT_EQ(make_tuple(c, b, a), reverse(make_tuple(a, b, c)));
}

TEST_F(Reverse, Reference) {
  auto t = reverse(tie(a));
  EXPECT_EQ(&a, &get<0>(t));
}

TEST_F(Reverse, Copy) {
  auto t = make_tuple(a);
  auto q = reverse(t);
  EXPECT_NE(&get<0>(t), &get<0>(q));
}

}  // namespace
}  // namespace tuple
}  // namespace util
