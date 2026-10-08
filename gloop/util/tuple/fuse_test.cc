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

#include "gloop/util/tuple/fuse.h"

#include <tuple>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;

int Minus(int a, int b) { return a - b; }
int ReturnOne() { return 1; }

struct MutableMinus {
  int operator()(int a, int b) { return a - b; }
};
struct MutableReturnOne {
  int operator()() { return 1; }
};

TEST(Fuse, Basic) { EXPECT_EQ(&Minus, fuse(Minus).base()); }

TEST(Fuse, Const) {
  EXPECT_EQ(2, fuse(Minus)(make_tuple(3, 1)));
  EXPECT_EQ(1, fuse(ReturnOne)(make_tuple()));
}

TEST(Fuse, MutableFuse) {
  EXPECT_EQ(2, fused<MutableMinus>()(make_tuple(3, 1)));
  EXPECT_EQ(1, fused<MutableReturnOne>()(make_tuple()));
}

}  // namespace
}  // namespace tuple
}  // namespace util
