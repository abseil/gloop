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

// Copyright 2007 Google Inc. All Rights Reserved.

#include "gloop/util/bitmap/bitmap.h"

#include <algorithm>  // for min and max
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/util/random/acmrandom.h"
#include "gloop/util/random/mt_random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, bm_bitmap_size, 10000, "Size of bitmap for benchmarks");

#if defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
constexpr bool kSmallerTests = true;
#else
constexpr bool kSmallerTests = false;
#endif

namespace {

using testing::ElementsAre;
using testing::ElementsAreArray;
using util::bitmap::internal::BasicBitmap;

// Fills in the bitmap with roughly 'percent_filled' bits randomly set. Returns
// a pair of count of bits set and the sum of the indices of the set bits.
template <typename W>
std::pair<typename BasicBitmap<W>::size_type, int64_t> FillBitMap(
    int percent_filled, BasicBitmap<W>* bitmap) {
  ACMRandom rnd(testing::GTEST_FLAG(random_seed));
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

template <class T>
struct is_atomic : std::false_type {};
template <class T>
struct is_atomic<std::atomic<T>> : std::true_type {};
template <class T>
struct is_atomic<const std::atomic<T>> : std::true_type {};

template <class T>
struct remove_atomic {
  using type = T;
};
template <class T>
struct remove_atomic<std::atomic<T>> {
  using type = T;
};
template <class T>
struct remove_atomic<const std::atomic<T>> {
  using type = const T;
};

template <typename Word>
constexpr auto kAllOnes = static_cast<Word>(~Word{0});

template <typename W>
void BM_FindNextSetBit(benchmark::State& state) {
  const int percent_filled = state.range(0);
  BasicBitmap<W> bitmap(absl::GetFlag(FLAGS_bm_bitmap_size), false);
  const int64_t expected_sum = FillBitMap(percent_filled, &bitmap).second;

  for (auto s : state) {
    int64_t summed_index = 0;
    for (typename BasicBitmap<W>::size_type index = 0;
         bitmap.FindNextSetBit(&index); ++index) {
      summed_index += index;
    }
    benchmark::DoNotOptimize(summed_index == expected_sum);
  }
}

template <typename W>
void BM_FindPreviousSetBit(benchmark::State& state) {
  const int percent_filled = state.range(0);
  BasicBitmap<W> bitmap(absl::GetFlag(FLAGS_bm_bitmap_size), false);
  const int64_t expected_sum = FillBitMap(percent_filled, &bitmap).second;

  for (auto s : state) {
    int64_t summed_index = 0;
    for (size_t index = bitmap.bits() - 1; bitmap.FindPreviousSetBit(&index);
         --index) {
      summed_index += index;
    }
    benchmark::DoNotOptimize(summed_index == expected_sum);
  }
}

template <typename W>
void BM_GetOnesCount(benchmark::State& state) {
  const int size = state.range(0);
  BasicBitmap<W> bitmap(size);
  // Note: The percent_filled doesn't affect the performance of
  // GetOnesCount.
  const size_t expected_sum = FillBitMap(50, &bitmap).first;

  for (auto s : state) {
    benchmark::DoNotOptimize(expected_sum == bitmap.GetOnesCount());
  }
}

template <typename W>
void BM_GetOnesCountInRangePartialWords(benchmark::State& state) {
  const int size = state.range(0);
  if (size >= 32) {
    BasicBitmap<W> bitmap(size, false);

    // We want to try various patterns of pieces of words vs. whole
    // words.  We don't bother filling the array because & doesn't
    // care.
    for (auto s : state) {
      for (size_t start = 0; start <= std::min(size, 32); start++) {
        for (size_t end = std::max(static_cast<int>(start), size - 32);
             end <= size; end++) {
          benchmark::DoNotOptimize(bitmap.GetOnesCountInRange(start, end));
        }
      }
    }
  }
}

template <typename W>
void BM_BitmapIterateBits(benchmark::State& state) {
  const int percent_filled = state.range(0);
  BasicBitmap<W> bitmap(absl::GetFlag(FLAGS_bm_bitmap_size), false);
  const int64_t expected_sum = FillBitMap(percent_filled, &bitmap).second;

  for (auto s : state) {
    int64_t summed_index = 0;
    for (uint32_t index : bitmap.TrueBitIndices()) {
      summed_index += index;
    }
    benchmark::DoNotOptimize(expected_sum == summed_index);
  }
  state.SetBytesProcessed(state.iterations() * bitmap.GetOnesCount());
}

template <typename W>
void BM_Union(benchmark::State& state) {
  BasicBitmap<W> bitmap1(state.range(0));
  BasicBitmap<W> bitmap2(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(bitmap1);
    benchmark::DoNotOptimize(bitmap2);
    bitmap1.Union(bitmap2);
  }
}

template <typename W>
void BM_Intersection(benchmark::State& state) {
  BasicBitmap<W> bitmap1(state.range(0));
  BasicBitmap<W> bitmap2(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(bitmap1);
    benchmark::DoNotOptimize(bitmap2);
    bitmap1.Intersection(bitmap2);
  }
}

static void SomeSizes(benchmark::Benchmark* b) {
  for (int i : {100, 75, 50, 25, 10, 5, 1}) b->Arg(i);
}
BENCHMARK_TEMPLATE(BM_FindNextSetBit, uint32_t)->Apply(&SomeSizes);
BENCHMARK_TEMPLATE(BM_FindNextSetBit, uint64_t)->Apply(&SomeSizes);
BENCHMARK_TEMPLATE(BM_FindPreviousSetBit, uint32_t)->Apply(&SomeSizes);
BENCHMARK_TEMPLATE(BM_FindPreviousSetBit, uint64_t)->Apply(&SomeSizes);

BENCHMARK_TEMPLATE(BM_GetOnesCount, uint32_t)->Range(0, 20 << 20);
BENCHMARK_TEMPLATE(BM_GetOnesCount, uint64_t)->Range(0, 20 << 20);

BENCHMARK_TEMPLATE(BM_BitmapIterateBits, uint32_t)->Range(0, 20 << 20);
BENCHMARK_TEMPLATE(BM_BitmapIterateBits, uint64_t)->Range(0, 20 << 20);

BENCHMARK_TEMPLATE(BM_GetOnesCountInRangePartialWords, uint32_t)
    ->Range(32, 2048);
BENCHMARK_TEMPLATE(BM_GetOnesCountInRangePartialWords, uint64_t)
    ->Range(32, 2048);

BENCHMARK_TEMPLATE(BM_Union, uint32_t)->Range(64, 1 << 30);
BENCHMARK_TEMPLATE(BM_Union, uint64_t)->Range(64, 1 << 30);

BENCHMARK_TEMPLATE(BM_Intersection, uint32_t)->Range(64, 1 << 30);
BENCHMARK_TEMPLATE(BM_Intersection, uint64_t)->Range(64, 1 << 30);

template <typename W>
class BitmapTest : public testing::Test {};

TYPED_TEST_SUITE_P(BitmapTest);

TYPED_TEST_P(BitmapTest, TestRange) {
  BasicBitmap<TypeParam> map(100);
  EXPECT_FALSE(map.TestRange(0, 100));
  EXPECT_TRUE(map.IsAllZeroes());
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  EXPECT_TRUE(map.TestRange(0, 100));
  EXPECT_FALSE(map.IsAllZeroes());
  EXPECT_FALSE(map.IsAllOnes());
  for (int i = 0; i < 100; i++) EXPECT_EQ(map.Get(i), (i % 7) == 0);

  map.SetAll(false);
  for (int i = 0; i < 100; i++) EXPECT_EQ(map.Get(i), false);
  EXPECT_FALSE(map.TestRange(0, 0));
  EXPECT_FALSE(map.TestRange(0, 1));
  EXPECT_FALSE(map.TestRange(31, 31));
  EXPECT_FALSE(map.TestRange(30, 31));
  EXPECT_FALSE(map.TestRange(31, 32));
  EXPECT_FALSE(map.TestRange(32, 33));
  EXPECT_FALSE(map.TestRange(98, 99));
  EXPECT_FALSE(map.TestRange(99, 100));
  EXPECT_FALSE(map.TestRange(100, 100));
  EXPECT_FALSE(map.TestRange(0, 32));
  EXPECT_FALSE(map.TestRange(0, 100));
  map.SetRange(11, 21, true);
  for (int i = 0; i < 100; i++) EXPECT_EQ(map.Get(i), (i >= 11) && (i < 21));

  EXPECT_TRUE(map.TestRange(0, 32));
  EXPECT_TRUE(map.TestRange(0, 100));
  EXPECT_TRUE(map.TestRange(11, 21));
  EXPECT_TRUE(map.TestRange(15, 16));
  EXPECT_FALSE(map.TestRange(15, 15));
  EXPECT_TRUE(map.TestRange(5, 12));
  EXPECT_FALSE(map.TestRange(5, 11));
  EXPECT_TRUE(map.TestRange(20, 60));
  EXPECT_FALSE(map.TestRange(21, 60));

  map.SetAll(true);
  EXPECT_FALSE(map.TestRange(0, 0));
  EXPECT_TRUE(map.TestRange(0, 1));
  EXPECT_FALSE(map.TestRange(31, 31));
  EXPECT_TRUE(map.TestRange(30, 31));
  EXPECT_TRUE(map.TestRange(31, 32));
  EXPECT_TRUE(map.TestRange(31, 33));
  EXPECT_TRUE(map.TestRange(32, 33));
  EXPECT_TRUE(map.TestRange(98, 99));
  EXPECT_TRUE(map.TestRange(99, 100));
  EXPECT_FALSE(map.TestRange(100, 100));
  EXPECT_TRUE(map.TestRange(0, 32));
  EXPECT_TRUE(map.TestRange(0, 100));
  EXPECT_TRUE(map.TestRange(0, 10));
  EXPECT_TRUE(map.TestRange(40, 60));
  EXPECT_TRUE(map.TestRange(90, 100));
  EXPECT_TRUE(map.IsAllOnes());
  for (int i = 0; i < 100; i++) EXPECT_EQ(map.Get(i), true);

  map.SetRange(70, 99, false);
  EXPECT_TRUE(map.TestRange(69, 99));
  EXPECT_TRUE(map.TestRange(70, 100));
  EXPECT_FALSE(map.TestRange(70, 99));
  for (int i = 0; i < 100; i++) EXPECT_EQ(map.Get(i), (i < 70) || (i >= 99));

  map.SetRange(60, 64, false);
  EXPECT_TRUE(map.TestRange(65, 66));
  EXPECT_FALSE(map.TestRange(65, 65));
  EXPECT_TRUE(map.TestRange(64, 65));
  EXPECT_FALSE(map.TestRange(64, 64));
  EXPECT_FALSE(map.TestRange(63, 64));
  EXPECT_FALSE(map.TestRange(60, 61));
  EXPECT_FALSE(map.TestRange(60, 60));
  EXPECT_TRUE(map.TestRange(59, 60));
  EXPECT_TRUE(map.TestRange(32, 64));
  EXPECT_TRUE(map.TestRange(64, 96));
  EXPECT_TRUE(map.TestRange(60, 72));

  EXPECT_EQ(map.bits(), 100);
}

TYPED_TEST_P(BitmapTest, RequiredArraySize) {
  // Test that array size is always at least 1 (even for 0 bits).
  EXPECT_EQ(1, BasicBitmap<TypeParam>::RequiredArraySize(0));
  EXPECT_EQ(1, BasicBitmap<TypeParam>::RequiredArraySize(1));

  constexpr size_t bits_in_word = 8 * sizeof(TypeParam);

  // Test numbers of bits around the 1 word boundary.
  EXPECT_EQ(1, BasicBitmap<TypeParam>::RequiredArraySize(bits_in_word - 1));
  EXPECT_EQ(1, BasicBitmap<TypeParam>::RequiredArraySize(bits_in_word));
  EXPECT_EQ(2, BasicBitmap<TypeParam>::RequiredArraySize(bits_in_word + 1));

  // Test numbers of bits around the 2 word boundary.
  EXPECT_EQ(2, BasicBitmap<TypeParam>::RequiredArraySize(2 * bits_in_word - 1));
  EXPECT_EQ(2, BasicBitmap<TypeParam>::RequiredArraySize(2 * bits_in_word));
  EXPECT_EQ(3, BasicBitmap<TypeParam>::RequiredArraySize(2 * bits_in_word + 1));

  // Verify that RequiredArraySize can be used to initialize constexpr.
  constexpr auto kArraySizeForZeroBits =
      BasicBitmap<TypeParam>::RequiredArraySize(0);
  EXPECT_EQ(1, kArraySizeForZeroBits);
}

TYPED_TEST_P(BitmapTest, OverAllocate) {
  // Test that we don't over allocate on boundaries
  BasicBitmap<TypeParam> map32(8 * sizeof(TypeParam));
  EXPECT_EQ(1, map32.array_size());

  BasicBitmap<TypeParam> map64(2 * 8 * sizeof(TypeParam));
  EXPECT_EQ(2, map64.array_size());
}

TYPED_TEST_P(BitmapTest, HighOrderMapElementMask) {
  // Test HighOrderMapElementMask
  BasicBitmap<TypeParam> map0(0u, false);
  EXPECT_EQ(0x0, map0.HighOrderMapElementMask());
  EXPECT_TRUE(map0.IsAllZeroes());
  EXPECT_TRUE(map0.IsAllOnes());

  if constexpr (sizeof(TypeParam) == 1) {
    BasicBitmap<TypeParam> map10(10u, false);
    EXPECT_EQ(0x03, map10.HighOrderMapElementMask());
    EXPECT_TRUE(map10.IsAllZeroes());
    EXPECT_FALSE(map10.IsAllOnes());

    BasicBitmap<TypeParam> map19(19u, true);
    EXPECT_EQ(0x07, map19.HighOrderMapElementMask());
    EXPECT_FALSE(map19.IsAllZeroes());
    EXPECT_TRUE(map19.IsAllOnes());
  } else {
    BasicBitmap<TypeParam> map10(10u, false);
    EXPECT_EQ(0x3FF, map10.HighOrderMapElementMask());
    EXPECT_TRUE(map10.IsAllZeroes());
    EXPECT_FALSE(map10.IsAllOnes());

    BasicBitmap<TypeParam> map42(42u, true);
    EXPECT_EQ(sizeof(TypeParam) == 8 ? 0x3FFFFFFFFFF : 0x3FF,
              map42.HighOrderMapElementMask());
    EXPECT_FALSE(map42.IsAllZeroes());
    EXPECT_TRUE(map42.IsAllOnes());
  }

  BasicBitmap<TypeParam> map64(64u, true);

  EXPECT_EQ(kAllOnes<typename remove_atomic<TypeParam>::type>,
            map64.HighOrderMapElementMask());
  EXPECT_FALSE(map64.IsAllZeroes());
  EXPECT_TRUE(map64.IsAllOnes());
}

TYPED_TEST_P(BitmapTest, FindNextSetBitBeforeLimit) {
  // Test FindNextSetBitBeforeLimit
  // Only check bits from 111 to 277 (limit bit == 278).
  // Should find all multiples of 27 in that range.
  BasicBitmap<TypeParam> map(500);
  for (int i = 0; i < 500; i++) map.Set(i, (i % 27) == 0);
  int find_me = 135;  // first one expected
  for (typename BasicBitmap<TypeParam>::size_type index = 111;
       map.FindNextSetBitBeforeLimit(&index, 278); ++index) {
    EXPECT_EQ(index, find_me);
    find_me += 27;
  }
  EXPECT_EQ(find_me, 297);  // the next find_me after 278
}

TYPED_TEST_P(BitmapTest, FindNextSetBitBeforeLimitAligned) {
  // Test FindNextSetBitBeforeLimit on aligned scans.
  BasicBitmap<TypeParam> map(256);
  for (int i = 0; i < 256; i++) map.Set(i, (i % 32) == 0);
  for (int i = 0; i < 256; i += 32) {
    typename BasicBitmap<TypeParam>::size_type index = i + 1;
    EXPECT_FALSE(map.FindNextSetBitBeforeLimit(&index, i + 32));
  }
}

const int kInterestingSizes[] = {
    0, 1, 2, 5, 10, 32, 47, 63, 64, 65, 103, 127, 128, 129,
};

template <typename TypeParam>
class BitmapIteratorTest : public testing::Test {};

TYPED_TEST_SUITE_P(BitmapIteratorTest);

TYPED_TEST_P(BitmapIteratorTest, BasicIteratorTest) {
  using size_type = typename BasicBitmap<TypeParam>::size_type;
  // Test FindNextSetBit
  // Check all bits in map.
  // Should find multiples of 7 from 0 to 98.
  BasicBitmap<TypeParam> map(100);
  for (size_type i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  int find_me = 0;  // first one expected
  for (size_type index : map.TrueBitIndices()) {
    ASSERT_EQ(find_me, index);
    find_me += 7;
  }
  EXPECT_EQ(find_me, 105);  // the next find_me after 98
}

TYPED_TEST_P(BitmapIteratorTest, EmptyBitmapIteratorTest) {
  BasicBitmap<TypeParam> map;
  for (uint32_t index : map.TrueBitIndices()) {
    FAIL() << index;
  }
}

TYPED_TEST_P(BitmapIteratorTest, NullBitmapTest) {
  util::bitmap::Bitmap32 default_constructed;
  for (auto it = default_constructed.TrueBitIndices().begin();
       it != default_constructed.TrueBitIndices().end(); ++it) {
    FAIL() << *it;
  }
}

TYPED_TEST_P(BitmapIteratorTest, BorrowedReferenceIteratorTest) {
  using size_type = typename BasicBitmap<TypeParam>::size_type;
  // Our bitmap has zero bits, but the backing memory is not zeroed out.
  // TypeParam data[] = {kAllOnes<TypeParam>, kAllOnes<TypeParam>};
  using core_type = typename remove_atomic<TypeParam>::type;
  TypeParam data[] = {kAllOnes<core_type>, kAllOnes<core_type>};
  BasicBitmap<TypeParam> map(data, 0);
  for (size_type index : map.TrueBitIndices()) {
    FAIL() << index;
  }
}

template <typename B>
std::vector<typename B::size_type> IteratedBits(const B& bitmap,
                                                typename B::size_type start,
                                                typename B::size_type end) {
  std::vector<typename B::size_type> bits;
  for (auto it = bitmap.TrueBitIndices().LowerBound(start);
       it != bitmap.TrueBitIndices().LowerBound(end); ++it) {
    bits.push_back(*it);
  }
  return bits;
}

TYPED_TEST_P(BitmapIteratorTest, SingleIteratorTest) {
  using size_type = typename BasicBitmap<TypeParam>::size_type;
  for (size_type size : kInterestingSizes) {
    BasicBitmap<TypeParam> map(size);
    for (size_type i = 0; i < size; ++i) {
      map.SetAll(false);
      map.Set(i, true);
      ASSERT_THAT(map.TrueBitIndices(), ElementsAre(i));

      // Verify that lower_bound works as expected at all positions.
      for (int pos = 0; pos <= i; ++pos) {
        ASSERT_THAT(IteratedBits(map, pos, map.bits()), ElementsAre(i));
      }
      for (size_type pos = i + 1; pos < size; ++pos) {
        ASSERT_THAT(IteratedBits(map, pos, map.bits()), ElementsAre());
      }
    }
  }
}

TYPED_TEST_P(BitmapIteratorTest, PairedIteratorTest) {
  using size_type = typename BasicBitmap<TypeParam>::size_type;
  for (size_type size : kInterestingSizes) {
    BasicBitmap<TypeParam> map(size);
    for (size_type i = 0; i < size; ++i) {
      for (size_type j = i + 1; j < size; ++j) {
        map.SetAll(false);
        map.Set(i, true);
        map.Set(j, true);
        ASSERT_THAT(map.TrueBitIndices(), ElementsAre(i, j));
        // Verify that lower_bound works as expected at all of the
        // interesting transition points.
        ASSERT_THAT(map.TrueBitIndices(), ElementsAre(i, j));
        if (i > 0) {
          ASSERT_THAT(IteratedBits(map, i - 1, map.bits()), ElementsAre(i, j));
        }
        ASSERT_THAT(IteratedBits(map, i, map.bits()), ElementsAre(i, j));
        if (i + 1 < size) {
          ASSERT_THAT(IteratedBits(map, i + 1, map.bits()), ElementsAre(j));
        }
        if (i < j - 1) {
          ASSERT_THAT(IteratedBits(map, j - 1, map.bits()), ElementsAre(j));
        }
        ASSERT_THAT(IteratedBits(map, j, map.bits()), ElementsAre(j));
        if (j + 1 < size) {
          ASSERT_THAT(IteratedBits(map, j + 1, map.bits()), ElementsAre());
        }
      }
    }
  }
}

TYPED_TEST_P(BitmapIteratorTest, RandomBitmapIteratorTest) {
  using size_type = typename BasicBitmap<TypeParam>::size_type;
  ACMRandom random(ACMRandom::DeterministicSeed());

  for (size_type size : kInterestingSizes) {
    if (size == 0) continue;
    for (double density : {0.1, 0.2, 0.5, 0.9, 1.0}) {
      for (size_type iter = 0; iter < 10; ++iter) {
        // Generate a random set of values.
        BasicBitmap<TypeParam> map(size);
        std::vector<size_type> expected;
        const int kNumSamples = std::max<int>(1, density * size);
        for (int i = 0; i < kNumSamples; ++i) {
          const uint32_t pos = random.Uniform(size);
          if (!map.Get(pos)) {
            expected.push_back(pos);
            map.Set(pos, true);
          }
        }
        std::sort(expected.begin(), expected.end());
        EXPECT_THAT(map.TrueBitIndices(), ElementsAreArray(expected));

        // Pick some random lower bounds and make sure that iteration works
        // with those.
        const int kNumLowerBounds = 1 + size / 5;
        for (size_type i = 0; i < kNumLowerBounds; ++i) {
          const size_type lower = random.Uniform(size);
          const std::vector<size_type> lower_expected(
              std::lower_bound(expected.begin(), expected.end(), lower),
              expected.end());
          EXPECT_THAT(IteratedBits(map, lower, map.bits()),
                      ElementsAreArray(lower_expected));
        }
      }
    }
  }
}

TYPED_TEST_P(BitmapTest, FindNextUnsetBit) {
  // Test FindNextUnsetBit
  // Check all bits in map.
  // Should find multiples of 7 from 0 to 98.
  BasicBitmap<TypeParam> map(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) != 0);
  typename BasicBitmap<TypeParam>::size_type index = 0;
  int find_me = 0;  // first one expected
  for (index = 0; map.FindNextUnsetBit(&index); ++index) {
    EXPECT_EQ(index, find_me);
    find_me += 7;
  }
  EXPECT_EQ(index, 98 + 1);
  EXPECT_EQ(find_me, 105);  // the next find_me after 98
}

TYPED_TEST_P(BitmapTest, FindNextUnsetBitBeforeLimit) {
  // Test FindNextUnsetBitBeforeLimit.
  // Turn off groups-of-3 bits at a stride of 27.
  // Only check bits from 111 to 277 (limit bit == 278).
  BasicBitmap<TypeParam> map(500);
  map.SetAll(true);
  for (int i = 0; i < 500; /*no incr*/) {
    if (0 == i % 27) {
      for (int j = 0; j < 3 && i < 500; ++i, ++j) map.Set(i, false);
    } else {
      ++i;
    }
  }
  int find_me = 135;  // first one expected
  for (typename BasicBitmap<TypeParam>::size_type index = 111;
       map.FindNextUnsetBitBeforeLimit(&index, 278); ++index) {
    EXPECT_EQ(find_me, index);
    if ((find_me % 27) < 2) {
      find_me += 1;
    } else {
      find_me += 25;
    }
  }
  EXPECT_EQ(find_me, 297);  // the next find_me after 278
}

TYPED_TEST_P(BitmapTest, FindNextUnsetBitBeforeLimit_Edges) {
  BasicBitmap<TypeParam> map(212);
  typename BasicBitmap<TypeParam>::size_type index = 0;
  map.SetAll(true);
  EXPECT_FALSE(map.FindNextUnsetBitBeforeLimit(&index, map.bits()));
  map.Set(0, false);
  ASSERT_TRUE(map.FindNextUnsetBitBeforeLimit(&index, map.bits()));
  EXPECT_EQ(0, index);
  ++index;
  EXPECT_FALSE(map.FindNextUnsetBitBeforeLimit(&index, map.bits()));
  map.Set(0, true);
  map.Set(map.bits() - 1, false);
  ASSERT_TRUE(map.FindNextUnsetBitBeforeLimit(&index, map.bits()));
  EXPECT_EQ(map.bits() - 1, index);
}

TYPED_TEST_P(BitmapTest, FindNextUnsetBitBeforeLimitAligned) {
  // Test FindNextUnsetBitBeforeLimit on aligned scans.
  BasicBitmap<TypeParam> map(256);
  for (int i = 0; i < 256; i++) map.Set(i, (i % 32) != 0);
  for (int i = 0; i < 256; i += 32) {
    typename BasicBitmap<TypeParam>::size_type index = i + 1;
    EXPECT_FALSE(map.FindNextUnsetBitBeforeLimit(&index, i + 32));
  }
}

TYPED_TEST_P(BitmapTest, FindNextSetBit) {
  // Test FindNextSetBit
  // Check all bits in map.
  // Should find multiples of 7 from 0 to 98.
  BasicBitmap<TypeParam> map(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  typename BasicBitmap<TypeParam>::size_type index = 0;
  int find_me = 0;  // first one expected
  for (index = 0; map.FindNextSetBit(&index); ++index) {
    EXPECT_EQ(index, find_me);
    find_me += 7;
  }
  EXPECT_EQ(index, 98 + 1);
  EXPECT_EQ(find_me, 105);  // the next find_me after 98
}

TYPED_TEST_P(BitmapTest, FindPreviousSetBitBeforeLimit) {
  // Test FindPreviousSetBitBeforeLimit
  // Check all bits in map.
  // Should find multiples of 7 from 98 to 0.
  BasicBitmap<TypeParam> map(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  int find_me = 98;  // first one expected
  // Check that the limit is respected.
  typename BasicBitmap<TypeParam>::size_type index;
  for (index = 99; index > 0 && map.FindPreviousSetBitBeforeLimit(&index, 19);
       --index) {
    EXPECT_EQ(index, find_me);
    find_me -= 7;
  }
  EXPECT_EQ(find_me, 14);  // the previous find_me before 21

  // Next, find the rest of the bits.
  for (; index > 0 && map.FindPreviousSetBit(&index); --index) {
    EXPECT_EQ(index, find_me);
    find_me -= 7;
  }
  EXPECT_EQ(find_me, -7);  // the previous find_me before 0
}

TYPED_TEST_P(BitmapTest, FindPreviousSetBitBeforeLimit_Edges) {
  {  // Zero-size map.
    BasicBitmap<TypeParam> map(0, true);
    typename BasicBitmap<TypeParam>::size_type index = 0;
    EXPECT_FALSE(map.FindPreviousSetBit(&index));
  }

  {  // An empty map.
    BasicBitmap<TypeParam> empty_map(100, false);
    typename BasicBitmap<TypeParam>::size_type index = 99;
    EXPECT_FALSE(empty_map.FindPreviousSetBit(&index));
  }

  {  // Single bit map.
    BasicBitmap<TypeParam> map(1, true);
    typename BasicBitmap<TypeParam>::size_type index = 0;
    EXPECT_TRUE(map.FindPreviousSetBit(&index));
    EXPECT_EQ(index, 0);
  }

  {  // Index bounds checking.
    BasicBitmap<TypeParam> map(50, false);
    map.Set(25, true);
    typename BasicBitmap<TypeParam>::size_type index = 25;
    EXPECT_TRUE(map.FindPreviousSetBit(&index));
    EXPECT_EQ(index, 25);
    // Limit out of bounds.
    EXPECT_FALSE(map.FindPreviousSetBitBeforeLimit(&index, 60));
    EXPECT_EQ(index, 25);
    // Index out of bounds.
    index = 80;
    EXPECT_FALSE(map.FindPreviousSetBit(&index));
    // Do not search past limit.
    index = 40;
    EXPECT_TRUE(map.FindPreviousSetBitBeforeLimit(&index, 25));
    EXPECT_EQ(index, 25);
  }
}

TYPED_TEST_P(BitmapTest, FindPreviousUnsetBitBeforeLimit) {
  // Test FindPreviousSetBitBeforeLimit
  // Check all bits in map.
  // Should find multiples of 7 from 98 to 0.
  BasicBitmap<TypeParam> map(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) != 0);
  int find_me = 98;  // first one expected
  // Check that the limit is respected.
  typename BasicBitmap<TypeParam>::size_type index;
  for (index = 99; index > 0 && map.FindPreviousUnsetBitBeforeLimit(&index, 19);
       --index) {
    EXPECT_EQ(index, find_me);
    find_me -= 7;
  }
  EXPECT_EQ(find_me, 14);  // the previous find_me before 21

  // Next, find the rest of the bits.
  for (; index > 0 && map.FindPreviousUnsetBit(&index); --index) {
    EXPECT_EQ(index, find_me);
    find_me -= 7;
  }
  EXPECT_EQ(find_me, -7);  // the previous find_me before 0
}

TYPED_TEST_P(BitmapTest, FindPreviousUnsetBitBeforeLimit_Edges) {
  {  // Zero-size map.
    BasicBitmap<TypeParam> map(0, false);
    typename BasicBitmap<TypeParam>::size_type index = 0;
    EXPECT_FALSE(map.FindPreviousUnsetBit(&index));
  }

  {  // An all set map.
    BasicBitmap<TypeParam> empty_map(100, true);
    typename BasicBitmap<TypeParam>::size_type index = 99;
    EXPECT_FALSE(empty_map.FindPreviousUnsetBit(&index));
  }

  {  // Single bit map.
    BasicBitmap<TypeParam> map(1, false);
    typename BasicBitmap<TypeParam>::size_type index = 0;
    EXPECT_TRUE(map.FindPreviousUnsetBit(&index));
    EXPECT_EQ(index, 0);
  }

  {  // Index bounds checking.
    BasicBitmap<TypeParam> map(50, true);
    map.Set(25, false);
    typename BasicBitmap<TypeParam>::size_type index = 25;
    EXPECT_TRUE(map.FindPreviousUnsetBit(&index));
    EXPECT_EQ(index, 25);
    // Limit out of bounds.
    EXPECT_FALSE(map.FindPreviousUnsetBitBeforeLimit(&index, 60));
    EXPECT_EQ(index, 25);
    // Index out of bounds.
    index = 80;
    EXPECT_FALSE(map.FindPreviousUnsetBit(&index));
    // Do not search past limit.
    index = 40;
    EXPECT_TRUE(map.FindPreviousUnsetBitBeforeLimit(&index, 25));
    EXPECT_EQ(index, 25);
  }
}

TYPED_TEST_P(BitmapTest, Union) {
  // Test union
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 3) == 0);

  map.Union(map2);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(map.Get(i), ((i % 7) == 0) || ((i % 3) == 0));
}

TYPED_TEST_P(BitmapTest, UnionDifferentSizeLargerTarget) {
  // Test union with different size maps, target of union is larger
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(50);
  // SetAll actually affects the bits above max size, which is what
  // we want to make sure that their values aren't causing any harm
  map.SetAll(true);
  map2.SetAll(true);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  for (int i = 0; i < 50; i++) map2.Set(i, (i % 3) == 0);

  map.Union(map2);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(map.Get(i), ((i % 7) == 0) || (i < 50 && (i % 3) == 0));
}

TYPED_TEST_P(BitmapTest, UnionDifferentSizeSmallerTarget) {
  // Test union with different size maps, target of union is smaller
  BasicBitmap<TypeParam> map(50);
  BasicBitmap<TypeParam> map2(100);
  // SetAll actually affects the bits above max size, which is what
  // we want to make sure that their values aren't causing any harm
  map.SetAll(true);
  map2.SetAll(true);
  for (int i = 0; i < 50; i++) map.Set(i, (i % 7) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 3) == 0);

  map.Union(map2);
  for (int i = 0; i < 50; i++)
    EXPECT_EQ(map.Get(i), (i < 50 && (i % 7) == 0) || ((i % 3) == 0));
}

TYPED_TEST_P(BitmapTest, Intersection) {
  // Test intersection
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 3) == 0);

  map.Intersection(map2);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(map.Get(i), ((i % 7) == 0) && ((i % 3) == 0));
}

TYPED_TEST_P(BitmapTest, IntersectionDifferentSizeLargerTarget) {
  // Test intersection with different size maps, target of
  // intersection is larger
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(50);
  // SetAll actually affects the bits above max size, which is what
  // we want to make sure that their values aren't causing any harm.
  map.SetAll(true);
  map2.SetAll(true);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 7) == 0);
  for (int i = 0; i < 50; i++) map2.Set(i, (i % 3) == 0);

  map.Intersection(map2);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(map.Get(i), ((i % 7) == 0) && (i < 50 && (i % 3) == 0));
}

TYPED_TEST_P(BitmapTest, IntersectionDifferentSizeSmallerTarget) {
  // Test intersection with different size maps, target of
  // intersection is smaller
  BasicBitmap<TypeParam> map(50);
  BasicBitmap<TypeParam> map2(100);
  // SetAll actually affects the bits above max size, which is what
  // we want to make sure that their values aren't causing any harm
  map.SetAll(true);
  map2.SetAll(true);
  for (int i = 0; i < 50; i++) map.Set(i, (i % 7) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 3) == 0);

  map.Intersection(map2);
  for (int i = 0; i < 50; i++)
    EXPECT_EQ(map.Get(i), (i < 50 && (i % 7) == 0) && ((i % 3) == 0));
}

TYPED_TEST_P(BitmapTest, IsIntersectionNonEmpty) {
  // Tests IsIntersectionNonEmpty
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(100);

  // No bits set
  EXPECT_FALSE(map.IsIntersectionNonEmpty(map2));
  EXPECT_FALSE(map2.IsIntersectionNonEmpty(map));

  // No bits overlap
  for (int i = 0; i < 100; i++) map.Set(i, (i % 2) == 1);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 2) == 0);
  EXPECT_FALSE(map.IsIntersectionNonEmpty(map2));
  EXPECT_FALSE(map2.IsIntersectionNonEmpty(map));

  // One bit overlap
  for (int i = 0; i < 100; i++) map.Set(i, (i % 20) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, i == 60);
  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));

  // Several bits overlap
  for (int i = 0; i < 100; i++) map.Set(i, (i % 20) == 1);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 10) == 1);
  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));

  // All bits overlap
  map.SetAll(true);
  map2.SetAll(true);
  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));
}

TYPED_TEST_P(BitmapTest, IsIntersectionNonEmpty32BitMaps) {
  // Tests IsIntersectionNonEmpty when one or both maps is 32 bits.
  BasicBitmap<TypeParam> map(32, false);
  BasicBitmap<TypeParam> map2(32, false);
  BasicBitmap<TypeParam> map3(33, false);
  for (int i = 0; i < 32; i++) map.Set(i, (i % 2) == 1);
  for (int i = 0; i < 32; i++) map2.Set(i, (i % 2) == 1);
  for (int i = 0; i < 33; i++) map3.Set(i, (i % 2) == 1);

  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));

  EXPECT_TRUE(map.IsIntersectionNonEmpty(map3));
  EXPECT_TRUE(map3.IsIntersectionNonEmpty(map));
}

TYPED_TEST_P(BitmapTest, IsIntersectionNonEmpty64BitMaps) {
  // Tests IsIntersectionNonEmpty when one or both maps is 64 bits.
  BasicBitmap<TypeParam> map(64, false);
  BasicBitmap<TypeParam> map2(64, false);
  BasicBitmap<TypeParam> map3(65, false);
  for (int i = 0; i < 64; i++) map.Set(i, (i % 2) == 1);
  for (int i = 0; i < 64; i++) map2.Set(i, (i % 2) == 1);
  for (int i = 0; i < 65; i++) map3.Set(i, (i % 2) == 1);

  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));

  EXPECT_TRUE(map.IsIntersectionNonEmpty(map3));
  EXPECT_TRUE(map3.IsIntersectionNonEmpty(map));
}

TYPED_TEST_P(BitmapTest, IsIntersectionNonEmptyZeroSizeMaps) {
  // Tests IsIntersectionNonEmpty when one or both maps is zero-size.
  BasicBitmap<TypeParam> map(0, false);
  BasicBitmap<TypeParam> map2(0, false);
  BasicBitmap<TypeParam> map3(10, false);
  for (int i = 0; i < 10; i++) map3.Set(i, (i % 2) == 1);

  EXPECT_FALSE(map.IsIntersectionNonEmpty(map2));
  EXPECT_FALSE(map2.IsIntersectionNonEmpty(map));

  EXPECT_FALSE(map.IsIntersectionNonEmpty(map3));
  EXPECT_FALSE(map3.IsIntersectionNonEmpty(map));
}

TYPED_TEST_P(BitmapTest, IsIntersectionNonEmptyDifferentSizeTarget) {
  // Test IsIntersectionNonEmpty with different size maps
  BasicBitmap<TypeParam> map(50);
  BasicBitmap<TypeParam> map2(100);

  // No bits overlap; larger map has non-overlapping bits all set
  map.SetAll(true);
  for (int i = 0; i < 100; i++) map2.Set(i, i >= 50);
  EXPECT_FALSE(map.IsIntersectionNonEmpty(map2));
  EXPECT_FALSE(map2.IsIntersectionNonEmpty(map));

  // Several bits overlap
  for (int i = 0; i < 50; i++) map.Set(i, (i % 20) == 1);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 10) == 1);
  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));

  // All bits overlap; larger map has non-overlapping bits all unset
  map.SetAll(true);
  for (int i = 0; i < 100; i++) map2.Set(i, i < 50);
  EXPECT_TRUE(map.IsIntersectionNonEmpty(map2));
  EXPECT_TRUE(map2.IsIntersectionNonEmpty(map));
}

TYPED_TEST_P(BitmapTest, Complement) {
  // Test Complement
  BasicBitmap<TypeParam> map(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 2) == 0);

  map.Complement();
  for (int i = 0; i < 100; i++) EXPECT_EQ(map.Get(i), ((i % 2) != 0));
}

TYPED_TEST_P(BitmapTest, Difference) {
  // Test Difference
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 3) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 2) == 0);
  map.Difference(map2);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(map.Get(i), ((i % 3) == 0) && ((i % 2) != 0));
}

TYPED_TEST_P(BitmapTest, ExclusiveOr) {
  // Test ExclusiveOr
  BasicBitmap<TypeParam> map(100);
  BasicBitmap<TypeParam> map2(100);
  for (int i = 0; i < 100; i++) map.Set(i, (i % 3) == 0);
  for (int i = 0; i < 100; i++) map2.Set(i, (i % 2) == 0);
  map.ExclusiveOr(map2);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(map.Get(i), ((i % 3) == 0) ^ ((i % 2) == 0));
}

TYPED_TEST_P(BitmapTest, IsEqual) {
  // Test IsEqual;
  const int num_bits = 73;

  BasicBitmap<TypeParam> map1(num_bits);
  BasicBitmap<TypeParam> map2(num_bits);

  // set the even bits.
  for (int i = 0; i < num_bits; i++) map1.Set(i, (i % 2) == 0);

  // this way the high order bits are different in this one.
  map2.Complement();
  for (int i = 0; i < num_bits; i++) map2.Set(i, (i % 2) == 0);

  EXPECT_TRUE(map1.IsEqual(map2));
  EXPECT_TRUE(map2.IsEqual(map1));
  EXPECT_EQ(map1, map2);
  EXPECT_EQ(map2, map1);

  for (int i = 0; i < num_bits; i++) {
    map2.Set(i, (i % 2) == 1);
    EXPECT_FALSE(map1.IsEqual(map2));
    EXPECT_FALSE(map2.IsEqual(map1));
    EXPECT_NE(map1, map2);
    EXPECT_NE(map2, map1);
    map2.Set(i, (i % 2) == 0);
  }

  EXPECT_TRUE(map1.IsEqual(map2));
  EXPECT_TRUE(map2.IsEqual(map1));
  EXPECT_EQ(map1, map2);
  EXPECT_EQ(map2, map1);
}

TYPED_TEST_P(BitmapTest, IsSubsetOf) {
  BasicBitmap<TypeParam> map1(1000);
  MTRandom rnd(testing::GTEST_FLAG(random_seed));
  for (int i = 0; i < 1000; i++) {
    map1.Set(i, rnd.OneIn(3));
  }

  BasicBitmap<TypeParam> map2(map1);
  EXPECT_TRUE(map1.IsSubsetOf(map2));
  EXPECT_TRUE(map2.IsSubsetOf(map1));

  // For each set bit in map2, turn it off and check if IsSubset is false.
  // For each unset bit in map2, turn it on and check if IsSubset is true.
  for (int i = 0; i < 1000; i++) {
    if (map2.Get(i)) {
      map2.Set(i, false);
      EXPECT_FALSE(map1.IsSubsetOf(map2));
      map2.Set(i, true);
    } else {
      map2.Set(i, true);
      EXPECT_TRUE(map1.IsSubsetOf(map2));
      map2.Set(i, false);
    }
  }
}

TYPED_TEST_P(BitmapTest, HashCodeBasics) {
  for (const int num_bits : {0, 13, 32, 64, 73, 100, 200, 300}) {
    BasicBitmap<TypeParam> map1(num_bits);
    BasicBitmap<TypeParam> map2(num_bits);

    // set the even bits.
    for (int i = 0; i < num_bits; i++) map1.Set(i, (i % 2) == 0);

    // this way the high order bits are different in this one.
    map2.Complement();
    for (int i = 0; i < num_bits; i++) map2.Set(i, (i % 2) == 0);

    ASSERT_EQ(map1, map2);
    ASSERT_EQ(map1.HashCode(), map2.HashCode());
    ASSERT_EQ(map1.HashCode(), absl::Hash<BasicBitmap<TypeParam>>{}(map1));
    ASSERT_EQ(map2.HashCode(), absl::Hash<BasicBitmap<TypeParam>>{}(map2));

    int num_single_bit_hash_collisions = 0;
    for (int i = 0; i < num_bits; i++) {
      map2.Set(i, (i % 2) == 1);
      if (map1.HashCode() == map2.HashCode()) {
        ++num_single_bit_hash_collisions;
      }
      map2.Set(i, (i % 2) == 0);
    }

    EXPECT_LE(num_single_bit_hash_collisions, 1)
        << "Hash function collides too often.";
  }
}

TYPED_TEST_P(BitmapTest, CompareTo) {
  {
    // Test the case where we have bitmaps that are the same, except for
    // leading zeroes.  In these cases, we should return a positive number for
    // a.CompareTo(b) where a is the larger bitmap and a negative number of
    // a.CompareTo(b) where a is the smaller bitmap.
    BasicBitmap<TypeParam> map1(5);
    BasicBitmap<TypeParam> map2(10);

    // Set the maps to: (high order bit is listed first):
    // 01101
    // 0000001101
    map1.Set(0, 1);
    map1.Set(2, 1);
    map1.Set(3, 1);
    map2.Set(0, 1);
    map2.Set(2, 1);
    map2.Set(3, 1);

    EXPECT_LT(map1.CompareTo(map2), 0);
    EXPECT_LT(map1, map2);
    EXPECT_GT(map2.CompareTo(map1), 0);
    EXPECT_GT(map2, map1);
    EXPECT_EQ(0, map1.CompareTo(map1));
    EXPECT_EQ(0, map2.CompareTo(map2));
  }
  {
    // Tests the case where the high order bits that are in a bitmap with more
    // bits is set to 1.
    // map1: 1111
    // map2: 10000
    BasicBitmap<TypeParam> map1(4, true);
    BasicBitmap<TypeParam> map2(5);
    map2.Set(4, true);
    EXPECT_LT(map1.CompareTo(map2), 0);
    EXPECT_LT(map1, map2);
    EXPECT_GT(map2.CompareTo(map1), 0);
    EXPECT_GT(map2, map1);
  }
  {
    // Tests the case where we have bitmaps of the same size:
    // map1: 1101
    // map2: 1011
    // In this case, map1 should be greater than map2
    BasicBitmap<TypeParam> map1(4);
    BasicBitmap<TypeParam> map2(4);
    map1.Set(3, true);
    map1.Set(2, true);
    map1.Set(0, true);
    map2.Set(3, true);
    map2.Set(1, true);
    map2.Set(0, true);
    EXPECT_GT(map1.CompareTo(map2), 0);
    EXPECT_GT(map1, map2);
    EXPECT_LT(map2.CompareTo(map1), 0);
    EXPECT_LT(map2, map1);
  }
  {
    // We expect the following to be a strictly increasing ordering (high order
    // bits first):
    // b1: 0
    // b2: 00
    // b3: 1
    // b4: 001
    // b5: 10
    // b6: 011
    // b7: 1000
    // b8: 1010
    // b9: 1111
    BasicBitmap<TypeParam> b1(1);
    BasicBitmap<TypeParam> b2(2);
    BasicBitmap<TypeParam> b3(1, true);
    BasicBitmap<TypeParam> b4(3);
    b4.Set(0, true);
    BasicBitmap<TypeParam> b5(2);
    b5.Set(1, true);
    BasicBitmap<TypeParam> b6(3, true);
    b6.Set(2, false);
    BasicBitmap<TypeParam> b7(4);
    b7.Set(3, true);
    BasicBitmap<TypeParam> b8(4);
    b8.Set(3, true);
    b8.Set(1, true);
    BasicBitmap<TypeParam> b9(4, true);

    std::vector<BasicBitmap<TypeParam>*> bitmaps;
    bitmaps.push_back(&b1);
    bitmaps.push_back(&b2);
    bitmaps.push_back(&b3);
    bitmaps.push_back(&b4);
    bitmaps.push_back(&b5);
    bitmaps.push_back(&b6);
    bitmaps.push_back(&b7);
    bitmaps.push_back(&b8);
    for (int i = 0; i < bitmaps.size(); ++i) {
      for (int j = 0; j < bitmaps.size(); ++j) {
        int compare = bitmaps.at(i)->CompareTo(*bitmaps.at(j));
        if (i == j) {
          EXPECT_EQ(0, compare);
          EXPECT_EQ(*bitmaps.at(i), *bitmaps.at(j));
          EXPECT_LE(*bitmaps.at(i), *bitmaps.at(j));
          EXPECT_GE(*bitmaps.at(i), *bitmaps.at(j));
        } else if (i < j) {
          EXPECT_LT(compare, 0);
          EXPECT_LT(*bitmaps.at(i), *bitmaps.at(j));
          EXPECT_LE(*bitmaps.at(i), *bitmaps.at(j));
        } else {
          EXPECT_GT(compare, 0);
          EXPECT_GT(*bitmaps.at(i), *bitmaps.at(j));
          EXPECT_GE(*bitmaps.at(i), *bitmaps.at(j));
        }
      }
    }
  }
  {
    // Tests zero sized bitmaps.
    BasicBitmap<TypeParam> b1(0, false);
    BasicBitmap<TypeParam> b2(1, false);
    EXPECT_LT(b1.CompareTo(b2), 0);
    EXPECT_GT(b2.CompareTo(b1), 0);
    EXPECT_LT(b1, b2);
    EXPECT_GT(b2, b1);
  }
  {
    // Now test cases where we have bitmaps that are larger than the word size.
    // Make sure that we sense set bits in the high order word.
    BasicBitmap<TypeParam> b1(35, true);
    BasicBitmap<TypeParam> b2(40, true);
    EXPECT_LT(b1.CompareTo(b2), 0);
    EXPECT_GT(b2.CompareTo(b1), 0);
    EXPECT_LT(b1, b2);
    EXPECT_GT(b2, b1);

    BasicBitmap<TypeParam> b3(35, true);
    BasicBitmap<TypeParam> b4(40, true);
    b4.Set(39, false);
    b4.Set(38, false);
    b4.Set(37, false);
    b4.Set(36, false);
    b4.Set(35, false);
    b4.Set(34, false);
    b4.Set(33, false);
    EXPECT_LT(b4.CompareTo(b3), 0);
    EXPECT_GT(b3.CompareTo(b4), 0);
    EXPECT_LT(b4, b3);
    EXPECT_GT(b3, b4);

    // Case where both bitmaps have multiple words.
    BasicBitmap<TypeParam> b5(100, true);
    BasicBitmap<TypeParam> b6(100, true);
    EXPECT_EQ(0, b5.CompareTo(b6));
    EXPECT_EQ(b5, b6);

    // Make sure we detect differences in the lowest order word.
    b5.Set(0, false);
    EXPECT_LT(b5.CompareTo(b6), 0);
    EXPECT_GT(b6.CompareTo(b5), 0);
    EXPECT_LT(b5, b6);
    EXPECT_GT(b6, b5);

    // Test cases on word boundaries for fencepost errors
    BasicBitmap<TypeParam> b7(33, true);
    BasicBitmap<TypeParam> b8(33, true);
    b7.Set(32, false);
    EXPECT_LT(b7.CompareTo(b8), 0);
    EXPECT_GT(b8.CompareTo(b7), 0);
    EXPECT_LT(b7, b8);
    EXPECT_GT(b8, b7);
    b8.Set(31, false);
    EXPECT_LT(b7.CompareTo(b8), 0);
    EXPECT_GT(b8.CompareTo(b7), 0);
    EXPECT_LT(b7, b8);
    EXPECT_GT(b8, b7);
    b8.Set(32, false);
    EXPECT_GT(b7.CompareTo(b8), 0);
    EXPECT_LT(b8.CompareTo(b7), 0);
    EXPECT_GT(b7, b8);
    EXPECT_LT(b8, b7);
    b7.Set(31, false);
    EXPECT_EQ(0, b7.CompareTo(b8));
    EXPECT_EQ(0, b8.CompareTo(b7));
    EXPECT_EQ(b7, b8);
    EXPECT_EQ(b8, b7);
  }
}

TYPED_TEST_P(BitmapTest, OneZeroCounts) {
  for (int i = 0; i < 3 * 5 * 8; i++) {
    int size = i * 3;
    int one_interval = (i % 5) + 1;
    BasicBitmap<TypeParam> map(size);
    int count = 0;
    for (int j = 0; j < size; j += one_interval) {
      map.Set(j, 1);
      ++count;
    }
    EXPECT_EQ(map.GetOnesCount(), count);
    EXPECT_EQ(map.GetZeroesCount(), size - count);
  }
}

TYPED_TEST_P(BitmapTest, OneZeroBeforeLimitCounts) {
  // Pick a size that is not a multiple of 32 intentionally.
  BasicBitmap<TypeParam> bitmap(40);

  EXPECT_EQ(0, bitmap.GetOnesCount());
  EXPECT_EQ(40, bitmap.GetZeroesCount());

  bitmap.Set(24, 1);
  EXPECT_EQ(1, bitmap.GetOnesCount());
  EXPECT_EQ(0, bitmap.GetOnesCountBeforeLimit(24));
  EXPECT_EQ(1, bitmap.GetOnesCountBeforeLimit(25));
  EXPECT_EQ(24, bitmap.GetZeroesCountBeforeLimit(24));
  EXPECT_EQ(24, bitmap.GetZeroesCountBeforeLimit(25));

  bitmap.Set(39, 1);
  EXPECT_EQ(2, bitmap.GetOnesCount());
  EXPECT_EQ(1, bitmap.GetOnesCountBeforeLimit(39));
  EXPECT_EQ(38, bitmap.GetZeroesCountBeforeLimit(39));
  EXPECT_EQ(38, bitmap.GetZeroesCountBeforeLimit(40));

  bitmap.SetAll(true);
  for (int i = 40; i > 0; --i) {
    EXPECT_EQ(i, bitmap.GetOnesCountBeforeLimit(i));
  }

  // Set some bit in each group of 8 bits to 0.
  for (int i = 0; i < 5; ++i) {
    bitmap.Set(8 * i + i, 0);
  }

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(i, bitmap.GetZeroesCountBeforeLimit(8 * i));
  }
  EXPECT_EQ(5, bitmap.GetZeroesCount());
}

// Helper that tests all possible cases of start, end that the
// GetOnesCountInRange function supports.
template <typename W>
void TestOneZeroInRangeHelper(const BasicBitmap<W>& bitmap) {
  for (size_t start = 0; start <= bitmap.bits(); ++start) {
    for (size_t end = start; end <= bitmap.bits(); ++end) {
      // Compute expected_count the naive way by counting individual bits.
      auto expected_ones = 0;
      for (size_t i = start; i < end; ++i) {
        if (bitmap.Get(i)) ++expected_ones;
      }
      EXPECT_EQ(expected_ones, bitmap.GetOnesCountInRange(start, end));
      EXPECT_EQ(end - start - expected_ones,
                bitmap.GetZeroesCountInRange(start, end));
    }
  }
}

TYPED_TEST_P(BitmapTest, OneZeroInRange) {
  const int kIters = kSmallerTests ? 25 : 3 * 5 * 8;  // 120
  for (int i = 0; i < kIters; i++) {
    int size = i * 3;
    int one_interval = (i % 5) + 1;
    BasicBitmap<TypeParam> map(size);
    for (int j = 0; j < size; j += one_interval) {
      map.Set(j, 1);
    }
    TestOneZeroInRangeHelper(map);
  }
}

TYPED_TEST_P(BitmapTest, CopyConstructorAndEquals) {
  // Test the copy constructor and operator=.
  BasicBitmap<TypeParam> map(100, false);
  for (int i = 0; i < map.bits(); i += 7) map.Set(i, true);
  BasicBitmap<TypeParam> map2(map);
  for (int i = 0; i < map.bits(); ++i) EXPECT_EQ(map2.Get(i), (i % 7 == 0));

  map2.SetAll(false);
  for (int i = 0; i < map.bits(); i += 13) map2.Set(i, true);
  map = map2;
  for (int i = 0; i < map.bits(); ++i) EXPECT_EQ(map.Get(i), (i % 13 == 0));
  map.operator=(map);
  for (int i = 0; i < map.bits(); ++i) EXPECT_EQ(map.Get(i), (i % 13 == 0));

  {  // Create a moved bitmap in a separate block and steal map's memory.
    map.SetAll(false);
    BasicBitmap<TypeParam> map3(std::move(map));
    ASSERT_EQ(map3.bits(), map.bits());
    for (int i = 0; i < map.bits(); i += 5) {
      map3.Set(i, true);
    }
    for (int i = 0; i < map.bits(); ++i) {
      EXPECT_EQ(map3.Get(i), (i % 5 == 0));
      EXPECT_EQ(map.Get(i), (i % 5 == 0));
    }
  }
  // Test that destruction of `map` here is safe.
}

TYPED_TEST_P(BitmapTest, DefaultConstructor) {
  // Verify that the default constructor creates a zero-sized bitmap.
  BasicBitmap<TypeParam> map;
  EXPECT_EQ(0, map.bits());
  EXPECT_EQ(1, map.array_size());
}

TYPED_TEST_P(BitmapTest, Toggle) {
  static const int kSize = 100;
  BasicBitmap<TypeParam> map(kSize, false);
  for (int i = 0; i < 100; i += 3) map.Toggle(i);
  for (int i = 0; i < 100; i += 9) map.Toggle(i);
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ((i % 3 == 0) && (i % 9 != 0), map.Get(i));
}

TYPED_TEST_P(BitmapTest, SpanConstructor) {
  // Verify that the constructor which takes absl::Span<bool> creates the
  // specified bitmap.
  BasicBitmap<TypeParam> map({true, false, true, false, true});

  EXPECT_EQ(map.bits(), 5);
  EXPECT_TRUE(map.Get(0));
  EXPECT_FALSE(map.Get(1));
  EXPECT_TRUE(map.Get(2));
  EXPECT_FALSE(map.Get(3));
  EXPECT_TRUE(map.Get(4));

  absl::FixedArray<bool, 5> v = {true, false, true, false, true};
  // Span of just the second and third items.
  BasicBitmap<TypeParam> map2(absl::Span<const bool>(v).subspan(1, 2));

  EXPECT_EQ(map2.bits(), 2);
  EXPECT_FALSE(map2.Get(0));
  EXPECT_TRUE(map2.Get(1));
}

TYPED_TEST_P(BitmapTest, MapElement) {
  constexpr std::size_t N = sizeof(TypeParam) * CHAR_BIT;

  {
    BasicBitmap<TypeParam> map(N + 5, true);
    ASSERT_EQ(map.array_size(), 2);
    EXPECT_EQ(map.GetMapElement(0), static_cast<TypeParam>(-1));

    // We don't currently test this, since GetMapElement is not well specified.
    // Construction from "true" results in an all-bits-one word.
    //
    // EXPECT_EQ(map.GetMapElement(1), static_cast<TypeParam>(0b11111));

    EXPECT_EQ(map.GetMaskedMapElement(0), static_cast<TypeParam>(-1));
    EXPECT_EQ(map.GetMaskedMapElement(1), static_cast<TypeParam>(0b11111));
  }

  {
    BasicBitmap<TypeParam> map;
    map.Resize(N + 5, true);
    ASSERT_EQ(map.array_size(), 2);
    EXPECT_EQ(map.GetMapElement(0), static_cast<TypeParam>(-1));

    // We don't currently test this, since GetMapElement is not well specified.
    // Resize currently has undefined behaviour; MSAN detects this.
    //
    // EXPECT_EQ(map.GetMapElement(1), static_cast<TypeParam>(0b11111));

    EXPECT_EQ(map.GetMaskedMapElement(0), static_cast<TypeParam>(-1));
    EXPECT_EQ(map.GetMaskedMapElement(1), static_cast<TypeParam>(0b11111));
  }
}

TYPED_TEST_P(BitmapTest, Resize) {
  const int kSize50 = 50;
  const int kSize100 = 100;
  const int kSize30 = 30;
  const uint32_t kSize4 = 3000000000u;
  const uint32_t kSize5 = std::numeric_limits<uint32_t>::max();

  BasicBitmap<TypeParam> map(kSize50, false);
  // First, the bitmap is initialized to all false.
  for (std::size_t i = 0; i != map.bits(); ++i)
    EXPECT_FALSE(map.Get(i)) << "Wrong value at inded " << i;

  // Resizing to the same size does not modify existing values, so all bits
  // remain false and the "true" fill value here is ignored.
  map.Resize(kSize50, true);
  ASSERT_EQ(kSize50, map.bits());
  for (std::size_t i = 0; i != map.bits(); ++i)
    EXPECT_FALSE(map.Get(i)) << "Wrong value at inded " << i;

  // Resizing to a larger size fills the _new_ elements with "true", but the
  // existing elements stay false.
  map.Resize(kSize100, true);
  ASSERT_EQ(kSize100, map.bits());
  for (std::size_t i = 0; i != kSize50; ++i)
    EXPECT_FALSE(map.Get(i)) << "Wrong value at inded " << i;
  for (std::size_t i = kSize50; i != kSize100; ++i)
    EXPECT_TRUE(map.Get(i)) << "Wrong value at inded " << i;

  // Shrinking back down does not modify existing values, which are still
  // "false" from the very beginning.
  map.Resize(kSize30, true);
  ASSERT_EQ(kSize30, map.bits());
  for (std::size_t i = 0; i != map.bits(); ++i)
    EXPECT_FALSE(map.Get(i)) << "Wrong value at inded " << i;

  if constexpr (is_atomic<TypeParam>::value && kSmallerTests) {
    return;  // Sanitizers have a hard time allocating this many atomics.
  }

  // Resizing to a very large size, filled "false", and setting the last value
  // "true".
  map.Resize(kSize4, false);
  ASSERT_EQ(kSize4, map.bits());
  map.Set(kSize4 - 1, true);
  EXPECT_FALSE(map.Get(kSize4 - 2));
  EXPECT_TRUE(map.Get(kSize4 - 1));

#ifdef ABSL_HAVE_THREAD_SANITIZER
  if constexpr (is_atomic<TypeParam>::value) {
    return;  // TSAN is having a hard time allocating so many atomics.
  }
#endif

  // Same, but even larger.
  map.Resize(kSize5, false);
  ASSERT_EQ(kSize5, map.bits());
  map.Set(kSize5 - 1, true);
  EXPECT_FALSE(map.Get(kSize5 - 2));
  EXPECT_TRUE(map.Get(kSize5 - 1));
}

TYPED_TEST_P(BitmapTest, AssignmentOperator) {
  BasicBitmap<TypeParam> map(1, false);
  BasicBitmap<TypeParam> src(100, true);

  map = src;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(map.Get(i));
  }
}

TYPED_TEST_P(BitmapTest, AssignToUnallocatedFromAllocated) {
  BasicBitmap<TypeParam> src(100, true);
  std::unique_ptr<TypeParam[]> bit_array(new TypeParam[1]);
  BasicBitmap<TypeParam> dest(bit_array.get(), CHAR_BIT * sizeof(TypeParam));
  dest = src;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(dest.Get(i));
  }
}

TYPED_TEST_P(BitmapTest, AssignToAllocatedFromUnallocated) {
  std::unique_ptr<TypeParam[]> bit_array(
      new TypeParam[100 / sizeof(TypeParam) + 1]);
  BasicBitmap<TypeParam> src(bit_array.get(), 100);
  src.SetAll(true);
  BasicBitmap<TypeParam> dest(32, false);
  dest = src;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(dest.Get(i));
  }
}

TYPED_TEST_P(BitmapTest, EmptyToString) {
  BasicBitmap<TypeParam> b;
  EXPECT_EQ(b.ToString(), "");
  EXPECT_EQ(b.ToString(2), "");
}

TYPED_TEST_P(BitmapTest, ToStringWithGroups) {
  BasicBitmap<TypeParam> b(8);
  b.Set(1, true);
  b.Set(2, true);
  b.Set(5, true);
  b.Set(7, true);
  EXPECT_EQ(b.ToString(0), "01100101");
  EXPECT_EQ(b.ToString(2), "01_10_01_01");
  EXPECT_EQ(b.ToString(3), "011_001_01");
  EXPECT_EQ(b.ToString(6), "011001_01");
  EXPECT_EQ(b.ToString(8), "01100101");
}

TYPED_TEST_P(BitmapTest, PrintToOstream) {
  BasicBitmap<TypeParam> b(8);
  b.Set(1, true);
  b.Set(2, true);
  b.Set(5, true);
  b.Set(7, true);

  std::ostringstream string_stream;
  // Alias the std::ostringstream as a std::ostream to ensure operator<< is
  // defined for any std::ostream&.
  std::ostream& out = string_stream;
  out << b;

  EXPECT_EQ(string_stream.str(), "01100101");
}

TYPED_TEST_P(BitmapTest, ConstArray) {
  const TypeParam bit_array[]{1, 0, 0};
  const size_t size = CHAR_BIT * sizeof(TypeParam) * 3;
  const BasicBitmap<const TypeParam> b =
      BasicBitmap<const TypeParam>::CreateConst(bit_array, size);
  EXPECT_TRUE(b.Get(0));
  EXPECT_FALSE(b.Get(1));
  EXPECT_FALSE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  size_t index;
  EXPECT_TRUE(b.FindFirstSetBit(&index));
  EXPECT_EQ(index, 0);
  EXPECT_TRUE(b.FindLastSetBit(&index));
  EXPECT_EQ(index, 0);
  EXPECT_EQ(b.GetOnesCount(), 1);
  EXPECT_EQ(b.GetZeroesCount(), size - 1);
  EXPECT_TRUE(b.TestRange(0, 1));
  EXPECT_FALSE(b.TestRange(1, 2));
  EXPECT_EQ(b.ToString(), "1" + std::string(size - 1, '0'));
  const BasicBitmap<const TypeParam> b2(bit_array, size);
  EXPECT_EQ(b.CompareTo(b2), 0);
  EXPECT_TRUE(b.IsEqual(b2));
  EXPECT_TRUE(b.IsSubsetOf(b2));
  EXPECT_TRUE(b.IsIntersectionNonEmpty(b2));
}

TYPED_TEST_P(BitmapTest, FromString) {
  if constexpr (is_atomic<TypeParam>::value) {
    GTEST_SKIP() << "Skipping test for atomics, since reinterpret_cast from a "
                    "buffer to an std::atomic array is undefined.";
  }
  // Note string length should be multiply of bitmap type size, or ASAN will
  // complain.
  alignas(TypeParam) const char str[] = "\x80\xFF\xFF\xFF\xFF\xFF\xFF\x01";
  const BasicBitmap<const TypeParam> b =
      BasicBitmap<const TypeParam>::CreateConst(
          reinterpret_cast<const TypeParam*>(str), 64);
  EXPECT_FALSE(b.Get(0));
  EXPECT_TRUE(b.Get(7));
  EXPECT_FALSE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(b.ToString(),
            "0000000111111111111111111111111111111111111111111111111110000000");
  size_t index;
  EXPECT_TRUE(b.FindFirstSetBit(&index));
  EXPECT_EQ(index, 7);
  EXPECT_TRUE(b.FindLastSetBit(&index));
  EXPECT_EQ(index, 56);
  EXPECT_EQ(b.GetOnesCount(), 6 * 8 + 2);
  EXPECT_EQ(b.GetZeroesCount(), 2 * 7);
#ifdef __SANITIZE_UNDEFINED__
  return;  // this is generally UB.
#endif
  std::string str2(str);
  const BasicBitmap<const TypeParam> b2(
      reinterpret_cast<const TypeParam*>(str2.data()), 64);
  EXPECT_EQ(b.CompareTo(b2), 0);
  EXPECT_TRUE(b.IsEqual(b2));
  EXPECT_TRUE(b.IsSubsetOf(b2));
  EXPECT_TRUE(b.IsIntersectionNonEmpty(b2));
}

REGISTER_TYPED_TEST_SUITE_P(
    BitmapTest, TestRange, RequiredArraySize, OverAllocate,
    HighOrderMapElementMask, FindNextSetBitBeforeLimit,
    FindNextSetBitBeforeLimitAligned, FindNextUnsetBit,
    FindNextUnsetBitBeforeLimit, FindNextUnsetBitBeforeLimit_Edges,
    FindNextUnsetBitBeforeLimitAligned, FindNextSetBit,
    FindPreviousSetBitBeforeLimit, FindPreviousSetBitBeforeLimit_Edges,
    FindPreviousUnsetBitBeforeLimit, FindPreviousUnsetBitBeforeLimit_Edges,
    Union, UnionDifferentSizeLargerTarget, UnionDifferentSizeSmallerTarget,
    Intersection, IntersectionDifferentSizeLargerTarget,
    IntersectionDifferentSizeSmallerTarget, IsIntersectionNonEmpty,
    IsIntersectionNonEmpty32BitMaps, IsIntersectionNonEmpty64BitMaps,
    IsIntersectionNonEmptyZeroSizeMaps,
    IsIntersectionNonEmptyDifferentSizeTarget, Complement, Difference,
    ExclusiveOr, IsEqual, IsSubsetOf, HashCodeBasics, CompareTo, OneZeroCounts,
    OneZeroBeforeLimitCounts, OneZeroInRange, CopyConstructorAndEquals,
    DefaultConstructor, Toggle, SpanConstructor, MapElement, Resize,
    AssignmentOperator, AssignToUnallocatedFromAllocated,
    AssignToAllocatedFromUnallocated, EmptyToString, ToStringWithGroups,
    PrintToOstream, ConstArray, FromString);

REGISTER_TYPED_TEST_SUITE_P(BitmapIteratorTest, BasicIteratorTest,
                            EmptyBitmapIteratorTest, NullBitmapTest,
                            BorrowedReferenceIteratorTest, SingleIteratorTest,
                            PairedIteratorTest, RandomBitmapIteratorTest);

typedef ::testing::Types<char, uint8_t, uint16_t, uint32_t, uint64_t,
                         std::atomic<uint32_t>, std::atomic<uint64_t>>
    WordTypes;
INSTANTIATE_TYPED_TEST_SUITE_P(BitmapTestCases, BitmapTest, WordTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(BitmapIteratorTestCases, BitmapIteratorTest,
                               WordTypes);
}  // namespace
