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

#include "gloop/util/tuple/components/ignore_index.h"

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

struct AddOne {
  constexpr int operator()(int x) const { return x + 1; }
};

struct GetFortyTwo {
  constexpr int operator()() const { return 42; }
};

static constexpr AddOne kAddOne;
static constexpr GetFortyTwo kGetFortyTwo;

TEST(IgnoreIndexTest, Constexpr) {
  constexpr auto ignored = ignore_index(&kAddOne);
  constexpr int res1 = ignored.template operator()<0>(10);
  EXPECT_EQ(11, res1);

  constexpr auto ignored_no_args = ignore_index_no_args(&kGetFortyTwo);
  constexpr int res2 = ignored_no_args.template operator()<0>();
  EXPECT_EQ(42, res2);
}

}  // namespace
}  // namespace tuple
}  // namespace util
