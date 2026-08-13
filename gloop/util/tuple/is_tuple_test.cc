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

#include "gloop/util/tuple/is_tuple.h"

#include <tuple>
#include <utility>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

TEST(IsTuple, Yes) {
  EXPECT_TRUE((is_tuple<::std::tuple<>>::value));
  EXPECT_TRUE((is_tuple<::std::tuple<int>>::value));
  EXPECT_TRUE((is_tuple<::std::tuple<int, char>>::value));
  EXPECT_TRUE((is_tuple<std::pair<int, char>>::value));
}

struct Incomplete;
struct Complete {};

TEST(IsTuple, No) {
  EXPECT_FALSE(is_tuple<int>::value);
  EXPECT_FALSE(is_tuple<void>::value);
  EXPECT_FALSE(is_tuple<Incomplete>::value);
  EXPECT_FALSE(is_tuple<Complete>::value);
}

}  // namespace
}  // namespace tuple
}  // namespace util
