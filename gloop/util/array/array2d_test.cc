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

#include "gloop/util/array/array2d.h"

#include <cstddef>
#include <limits>

#include "gtest/gtest.h"

namespace {

TEST(Array2DTest, IntegerOverflowInitOwned) {
  typedef std::ptrdiff_t PD;
  PD h = 65536;
  PD w = (std::numeric_limits<PD>::max() / h) + 1;
  EXPECT_DEATH(Array2D<char> array(h, w),
               "Integer overflow in Array2D dimensions");
}

TEST(Array2DTest, IntegerOverflowRealloc) {
  typedef std::ptrdiff_t PD;
  PD h = 65536;
  PD w = (std::numeric_limits<PD>::max() / h) + 1;
  Array2D<char> array(10, 10);
  EXPECT_DEATH(array.Realloc(h, w), "Integer overflow in Array2D dimensions");
}

}  // namespace
