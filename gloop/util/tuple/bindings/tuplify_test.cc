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

#include "gloop/util/tuple/bindings/tuplify.h"

#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/streamable.h"
#include "gloop/util/tuple/struct.h"
#include "gtest/gtest.h"

namespace {

struct A {
  using tuplify = ::util::tuple::Tuplify<2>;
  int a;
  int b;
  friend TUPLE_DEFINE_OP(A, eq);
};

TEST(Tuplify, Works) {
  A a{1, 2};
  EXPECT_EQ(::util::tuple::get<0>(a), 1);
  EXPECT_EQ(::util::tuple::get<1>(a), 2);
  EXPECT_EQ(::util::tuple::to_string(a), "{1, 2}");
  A b{3, 2};
  EXPECT_EQ(a, a);
  EXPECT_FALSE(a == b);
}

}  // namespace
