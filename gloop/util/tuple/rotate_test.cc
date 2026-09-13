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

#include "gloop/util/tuple/rotate.h"

#include <string>
#include <tuple>

#include "gloop/util/tuple/matchers.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::get;
using ::std::tie;

TEST_F(TestValues, Example) {
  ::std::tuple<int, ::std::string, void*> a(42, "hello", nullptr);
  ::std::tuple<::std::string, void*, int> b = rotate<1>(a);
  EXPECT_THAT(b, testing::Tuple("hello", nullptr, 42));
}

TEST_F(TestValues, Functional) {
  EXPECT_EQ(tie(), rotate<0>(tie()));
  EXPECT_EQ(tie(a), rotate<0>(tie(a)));
  EXPECT_EQ(tie(a), rotate<1>(tie(a)));
  EXPECT_EQ(tie(a, b), rotate<0>(tie(a, b)));
  EXPECT_EQ(tie(b, a), rotate<1>(tie(a, b)));
  EXPECT_EQ(tie(a, b), rotate<2>(tie(a, b)));
  EXPECT_EQ(tie(a, b, c), rotate<0>(tie(a, b, c)));
  EXPECT_EQ(tie(b, c, a), rotate<1>(tie(a, b, c)));
  EXPECT_EQ(tie(c, a, b), rotate<2>(tie(a, b, c)));
  EXPECT_EQ(tie(a, b, c), rotate<3>(tie(a, b, c)));
}

TEST_F(TestValues, Ref) { EXPECT_EQ(&a, &get<0>(rotate<0>(tie(a)))); }

TEST_F(TestValues, ExplicitTag) {
  auto p = rotate<pair_tag, 1>(tie(a, b));
  EXPECT_EQ(b, p.first);
  EXPECT_EQ(a, p.second);
}

}  // namespace
}  // namespace tuple
}  // namespace util
