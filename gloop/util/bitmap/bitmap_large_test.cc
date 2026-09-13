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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "absl/base/dynamic_annotations.h"
#include "absl/flags/flag.h"
#include "gloop/util/bitmap/bitmap.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

namespace {

using util::bitmap::internal::BasicBitmap;

// Fills in the bitmap with roughly 'percent_filled' bits randomly set. Returns
// a pair of count of bits set and the sum of the indices of the set bits.
template <typename W>
std::pair<typename BasicBitmap<W>::size_type, int64_t> FillBitMap(
    int percent_filled, BasicBitmap<W>* bitmap) {
  ACMRandom rnd(GTEST_FLAG_GET(random_seed));
  std::pair<typename BasicBitmap<W>::size_type, int64_t> bitmap_info_pair(0, 0);
  for (size_t index = 0; index < bitmap->bits(); ++index) {
    if (rnd.Uniform(100) < percent_filled) {
      bitmap->Set(index, true);
      ++bitmap_info_pair.first;
      bitmap_info_pair.second += index;
    }
  }
  return bitmap_info_pair;
}

template <typename W>
class BitmapLargeTest : public testing::Test {};

TYPED_TEST_SUITE_P(BitmapLargeTest);

TYPED_TEST_P(BitmapLargeTest, Large) {
  uint32_t large = std::numeric_limits<uint32_t>::max();
  BasicBitmap<TypeParam> map(large);
  uint32_t i;

  EXPECT_TRUE(map.IsAllZeroes());

  for (i = 0; i < large; ++i) {
    map.Set(i, true);
  }
  EXPECT_TRUE(map.IsAllOnes());

  map.Clear();
  for (i = 1; i < large / i; ++i) {
    map.Set(i * i, true);
  }
  EXPECT_EQ(i - 1, map.GetOnesCount());

  map.Clear();
  for (i = 1; i < large / i; ++i) {
    map.Set(large - i * i, true);
  }
  EXPECT_EQ(i - 1, map.GetOnesCount());
}

REGISTER_TYPED_TEST_SUITE_P(BitmapLargeTest, Large);

typedef ::testing::Types<uint32_t, uint64_t> WordTypes;
INSTANTIATE_TYPED_TEST_SUITE_P(BitmapLargeTestCases, BitmapLargeTest,
                               WordTypes);
}  // namespace
