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

#include "gloop/util/bitmap/inlined_bitvector.h"

#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include "absl/hash/hash_testing.h"
#include "absl/log/log.h"
#include "benchmark/benchmark.h"
#include "gloop/util/random/shared_bit_gen.h"
#include "gtest/gtest.h"

namespace util::bitmap {
namespace {
typedef InlinedBitVector<64> BitVecWithDifferingInlineBits;

template <class BitVec>
void FillWithAlternatingBits(BitVec& v) {
  const size_t n = v.size();
  v.ClearBits(0, n);
  for (size_t i = 0; i < n; i += 2) {
    v.set_bit(i);
  }
}

class Container {
 public:
  Container() = default;

 private:
  // '51' is arbitrary
  util::bitmap::InlinedBitVector<51> bits_;
};

TEST(ContainerTest, CanInstantiateEmptyConstClassWithInlinedBitVectorField) {
  // Test that a constant object can have an InlinedVector as a field (catches
  // subtle issues with InlinedVector constructor declarations)
  const Container c;  // If there's no compiler error here, the test passes.
  EXPECT_TRUE(true);
}

// Test suite instantiated with InlinedBitVector<N> for many N's
template <class BitVec>
class BitVecTest : public testing::Test {};

constexpr size_t kMaxTestSize = 128;

using TestedTypes = testing::Types<
    // small
    InlinedBitVector<1>, InlinedBitVector<25>,
    // neighborhood of 32
    InlinedBitVector<31>, InlinedBitVector<32>, InlinedBitVector<33>,
    // neighborhood of 64
    InlinedBitVector<63>, InlinedBitVector<64>, InlinedBitVector<65>,
    // neighborhood of 96
    InlinedBitVector<95>, InlinedBitVector<96>, InlinedBitVector<97>,
    // neighborhood of 128
    InlinedBitVector<127>, InlinedBitVector<128>, InlinedBitVector<129>,
    // large
    InlinedBitVector<1024>, InlinedBitVector<1234> >;
TYPED_TEST_SUITE(BitVecTest, TestedTypes);

TYPED_TEST(BitVecTest, Empty) {
  TypeParam bv;
  EXPECT_EQ(bv.ToString(), "{}");
}

TYPED_TEST(BitVecTest, ToStringStability) {
  TypeParam bv(10);
  for (int i = 0; i < 5; ++i) {
    bv.set_bit(i);
  }
  EXPECT_EQ(bv.ToString(), "{0,1,2,3,4}");

  bv.clear_bit(2);
  bv.set_bit(9);
  EXPECT_EQ(bv.ToString(), "{0,1,3,4,9}");
}

TYPED_TEST(BitVecTest, SimpleOps) {
  TypeParam bv(128);
  bv.assign_bit(0, true);
  EXPECT_EQ(bv.ToString(), "{0}");
  bv.clear_bit(0);
  bv.set_bit(7);
  EXPECT_EQ(bv.ToString(), "{7}");
  bv.set_bit(3);
  EXPECT_EQ(bv.ToString(), "{3,7}");
  bv.set_bit(33);
  EXPECT_EQ(bv.ToString(), "{3,7,33}");
  EXPECT_TRUE(bv.get_bit(3));
  EXPECT_TRUE(bv[3]);
  EXPECT_FALSE(bv.get_bit(4));
  EXPECT_FALSE(bv[4]);
  bv.resize(1097);
  bv.set_bit(1096);
  EXPECT_EQ(bv.ToString(), "{3,7,33,1096}");
  bv.assign_bit(1096, false);
  EXPECT_EQ(bv.ToString(), "{3,7,33}");
  bv.assign_bit(3, false);
  EXPECT_EQ(bv.ToString(), "{7,33}");
}

TYPED_TEST(BitVecTest, ClearBits) {
  // Pick sizes that are both multiples and non-multiples of 64 to
  // catch partial last word, or read-past-end errors.
  for (int size : std::initializer_list<int>{140, 256}) {
    TypeParam bv_ones(size);
    for (int i = 0; i < size; ++i) {
      bv_ones.set_bit(i);
    }

    for (int start = 0; start <= size; ++start) {
      for (int end = start; end <= size; ++end) {
        TypeParam bv(bv_ones), bv_simple(bv_ones);
        bv.ClearBits(start, end);
        for (int i = start; i < end; ++i) {
          bv_simple.clear_bit(i);
        }
        EXPECT_TRUE(bv_simple.Equals(bv))
            << "ClearBits(" << start << ":" << end << ")"
            << "\n   Actual:" << bv.ToString()
            << "\n Expected: " << bv_simple.ToString();
      }
    }
  }
}

TYPED_TEST(BitVecTest, SetBits) {
  // Pick sizes that are both multiples and non-multiples of 64 to
  // catch partial last word, or read-past-end errors.
  for (int size : std::initializer_list<int>{140, 256}) {
    TypeParam bv_zeros(size);
    for (int i = 0; i < size; ++i) {
      bv_zeros.clear_bit(i);
    }

    for (int start = 0; start <= size; ++start) {
      for (int end = start; end <= size; ++end) {
        TypeParam bv(bv_zeros), bv_simple(bv_zeros);
        bv.SetBits(start, end);
        for (int i = start; i < end; ++i) {
          bv_simple.set_bit(i);
        }
        EXPECT_TRUE(bv_simple.Equals(bv))
            << "SetBits(" << start << ":" << end << ")"
            << "\n   Actual:" << bv.ToString()
            << "\n Expected: " << bv_simple.ToString();
      }
    }
  }
}

// Testing both Equals and operator==/!=.
TYPED_TEST(BitVecTest, Equals) {
  for (int size = 0; size < 80; ++size) {
    TypeParam bv1(size);
    TypeParam bv2(bv1);

    EXPECT_TRUE(bv1.Equals(bv2));
    EXPECT_TRUE(bv1 == bv2);
    EXPECT_FALSE(bv1 != bv2);
    for (int i = 0; i < size; ++i) {
      if (i > 0) {
        bv1.clear_bit(i - 1);
      }
      bv1.set_bit(i);
      EXPECT_FALSE(bv1.Equals(bv2))
          << "\nbv1=" << bv1.ToString() << "\nbv2=" << bv2.ToString();
      EXPECT_FALSE(bv1 == bv2);
      EXPECT_TRUE(bv1 != bv2);
    }
  }
}

TYPED_TEST(BitVecTest, Assignment) {
  constexpr int kMaxLength = kMaxTestSize;
  for (int l = 0; l < kMaxLength; l++) {
    // Make vector with alternating bits.
    TypeParam src(l);
    FillWithAlternatingBits(src);

    // Copy constructor
    TypeParam dst1(src);
    EXPECT_TRUE(src.Equals(dst1))
        << src.ToString() << " vs. " << dst1.ToString();

    // Copy assignment
    TypeParam dst2;
    dst2.resize(1000);
    dst2 = src;
    EXPECT_TRUE(src.Equals(dst2))
        << src.ToString() << " vs. " << dst2.ToString();

    // Move constructor
    TypeParam moved1(std::move(dst2));
    EXPECT_TRUE(src.Equals(moved1))
        << src.ToString() << " vs. " << moved1.ToString();
    EXPECT_EQ(dst2.size(), 0);  // NOLINT: bugprone-use-after-move

    // Move assignment
    TypeParam moved2;
    moved2 = std::move(dst1);
    EXPECT_TRUE(src.Equals(moved2))
        << src.ToString() << " vs. " << moved2.ToString();
    EXPECT_EQ(dst1.size(), 0);  // NOLINT: bugprone-use-after-move

    // Move assignment over pre-existing data.
    TypeParam tmp(src);
    TypeParam moved3(l);
    moved3 = std::move(tmp);
    EXPECT_TRUE(src.Equals(moved3))
        << src.ToString() << " vs. " << moved3.ToString();
  }
}

// Testing both Equals and operator==/!=.
TYPED_TEST(BitVecTest, Equals_Inverted) {
  for (int size = 0; size < 80; ++size) {
    TypeParam bv1(size);
    bv1.SetBits(0, size);
    TypeParam bv2(bv1);

    EXPECT_TRUE(bv1.Equals(bv2));
    EXPECT_TRUE(bv1 == bv2);
    EXPECT_FALSE(bv1 != bv2);
    for (int i = 0; i < size; ++i) {
      if (i > 0) {
        bv1.set_bit(i - 1);
      }
      bv1.clear_bit(i);
      EXPECT_FALSE(bv1.Equals(bv2))
          << "\nbv1=" << bv1.ToString() << "\nbv2=" << bv2.ToString();
      EXPECT_FALSE(bv1 == bv2);
      EXPECT_TRUE(bv1 != bv2);
    }
  }
}

// Same as `Equals` test above, except uses differing number of inline bits
// between the two bit vectors.
TYPED_TEST(BitVecTest, Equals_DifferingInlineBits) {
  for (int size = 0; size < 80; ++size) {
    TypeParam bv1(size);
    BitVecWithDifferingInlineBits bv2(size);

    EXPECT_TRUE(bv1.Equals(bv2));
    EXPECT_TRUE(bv1 == bv2);
    EXPECT_FALSE(bv1 != bv2);
    for (int i = 0; i < size; ++i) {
      if (i > 0) {
        bv1.clear_bit(i - 1);
      }
      bv1.set_bit(i);
      EXPECT_FALSE(bv1.Equals(bv2))
          << "\nbv1=" << bv1.ToString() << "\nbv2=" << bv2.ToString();
      EXPECT_FALSE(bv1 == bv2);
      EXPECT_TRUE(bv1 != bv2);
    }
  }
}

TYPED_TEST(BitVecTest, count) {
  TypeParam bv(87);
  bv.set_bit(2);
  bv.set_bit(31);
  bv.set_bit(32);
  bv.set_bit(86);
  EXPECT_EQ(4, bv.count());
}

TYPED_TEST(BitVecTest, resize) {
  constexpr int kMaxLength = kMaxTestSize;
  InlinedBitVector<64> bv;
  for (int l1 = 0; l1 < kMaxLength; l1++) {
    for (int l2 = 0; l2 < kMaxLength; l2++) {
      bv.resize(l1);
      for (int i = 0; i < l1; i++) bv.set_bit(i);
      for (int i = l1; i > 0; i--) {
        bv.resize(i - 1);
        bv.resize(i);
        EXPECT_FALSE(bv.get_bit(i - 1));
      }
    }
  }
  for (int l1 = 0; l1 < kMaxLength; l1++) {
    for (int l2 = 0; l2 < kMaxLength; l2++) {
      bv.resize(l1);
      for (int i = 0; i < l1; i++) bv.set_bit(i);
      bv.resize(l2);
      if (l2 > 0) {
        bv.set_bit(l2 - 1);
        bv.resize(l2 - 1);
        bv.resize(l2);
        EXPECT_FALSE(bv.get_bit(l2 - 1));
        bv.resize(l2 + 1);
        EXPECT_FALSE(bv.get_bit(l2));
        bv.set_bit(l2);
        EXPECT_TRUE(bv.get_bit(l2));
        bv.resize(l2 + 2);
        EXPECT_TRUE(bv.get_bit(l2));
        EXPECT_FALSE(bv.get_bit(l2 + 1));
      }
    }
  }
}

TYPED_TEST(BitVecTest, FindNextSetBit) {
  for (size_t len = 1; len < kMaxTestSize; len++) {
    for (size_t b1 = 0; b1 < len - 1; b1++) {
      for (size_t b2 = b1 + 1; b2 < len; b2++) {
        TypeParam bv;
        bv.resize(len);

        size_t i1 = 0;
        EXPECT_FALSE(bv.FindNextSetBit(&i1));

        bv.set_bit(b1);
        size_t i2 = 0;
        EXPECT_TRUE(bv.FindNextSetBit(&i2));
        EXPECT_EQ(i2, b1);
        i2++;
        EXPECT_FALSE(bv.FindNextSetBit(&i2));
        bv.set_bit(b2);

        size_t index = 0;
        if (len == 0) {
          EXPECT_FALSE(bv.FindNextSetBit(&index));
        } else {
          EXPECT_TRUE(bv.FindNextSetBit(&index));
          EXPECT_EQ(index, b1);
          index++;
          EXPECT_TRUE(bv.FindNextSetBit(&index));
          EXPECT_EQ(index, b2);
          index++;
          EXPECT_FALSE(bv.FindNextSetBit(&index));
        }
      }
    }
  }
}
TYPED_TEST(BitVecTest, FindNextSetBitBeforeLimit) {
  for (size_t len = 1; len < kMaxTestSize; len++) {
    for (size_t b1 = 0; b1 < len - 1; b1++) {
      for (size_t b2 = b1 + 1; b2 < len; b2++) {
        TypeParam bv;
        bv.resize(len);

        size_t i1 = 0;
        EXPECT_FALSE(bv.FindNextSetBitBeforeLimit(&i1, len));

        bv.set_bit(b1);
        size_t i2 = 0;
        EXPECT_TRUE(bv.FindNextSetBitBeforeLimit(&i2, len));
        EXPECT_EQ(i2, b1);
        EXPECT_FALSE(bv.FindNextSetBitBeforeLimit(&i2, b1));
        bv.set_bit(b2);

        size_t index = 0;
        if (len == 0) {
          EXPECT_FALSE(bv.FindNextSetBitBeforeLimit(&index, len));
        } else {
          EXPECT_TRUE(bv.FindNextSetBitBeforeLimit(&index, len));
          EXPECT_EQ(index, b1);
          index++;
          EXPECT_FALSE(bv.FindNextSetBitBeforeLimit(&index, b2));
          EXPECT_TRUE(bv.FindNextSetBitBeforeLimit(&index, len));
          EXPECT_EQ(index, b2);
          index++;
          EXPECT_FALSE(bv.FindNextSetBitBeforeLimit(&index, len));
        }
      }
    }
  }
}
TYPED_TEST(BitVecTest, FindNextSetBitAndResizeInteraction) {
  for (size_t len = 1; len < kMaxTestSize; len++) {
    for (size_t b1 = 0; b1 < len - 1; b1++) {
      TypeParam bv;
      bv.resize(len);
      for (size_t new_len = len - 1; new_len > 0; new_len--) {
        bv.set_bit(new_len);
        size_t index = (new_len / 32) * 32;
        EXPECT_TRUE(bv.FindNextSetBit(&index));
        EXPECT_EQ(index, new_len);
        bv.resize(new_len);
        size_t index2 = (new_len / 32) * 32;
        EXPECT_FALSE(bv.FindNextSetBit(&index2));
      }
    }
  }
}

TYPED_TEST(BitVecTest, FindNextUnsetBit) {
  // Check all bits in map.
  // Should find multiples of 7 from 0 to 98.
  TypeParam bv(100);
  for (int i = 0; i < 100; i++) {
    if (i % 7 != 0) {
      bv.set_bit(i);
    }
  }
  size_t index = 0;
  int find_me = 0;  // first one expected
  for (index = 0; bv.FindNextUnsetBit(&index); ++index) {
    EXPECT_EQ(index, find_me);
    find_me += 7;
  }
  EXPECT_EQ(index, 98 + 1);
  EXPECT_EQ(find_me, 105);  // the next find_me after 98
}

TYPED_TEST(BitVecTest, FindNextUnsetBitBeforeLimit) {
  // Turn off groups-of-3 bits at a stride of 27.
  // Only check bits from 111 to 277 (limit bit == 278).
  TypeParam bv(500);
  for (int i = 0; i < 500; i++) bv.set_bit(i);
  for (int i = 0; i < 500; /*no incr*/) {
    if (0 == i % 27) {
      for (int j = 0; j < 3 && i < 500; ++i, ++j) bv.clear_bit(i);
    } else {
      ++i;
    }
  }
  int find_me = 135;  // first one expected
  for (size_t index = 111; bv.FindNextUnsetBitBeforeLimit(&index, 278);
       ++index) {
    EXPECT_EQ(find_me, index);
    if ((find_me % 27) < 2) {
      find_me += 1;
    } else {
      find_me += 25;
    }
  }
  EXPECT_EQ(find_me, 297);  // the next find_me after 278
}

TYPED_TEST(BitVecTest, Union) {
  TypeParam bv;
  TypeParam bv2;

  bv2.resize(10);
  bv2.set_bit(5);
  bv2.set_bit(7);

  bv.resize(10);
  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv2.clear();
  bv2.resize(20);
  bv2.set_bit(15);

  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 20);
  EXPECT_EQ(bv.ToString(), "{5,7,15}");

  bv2.clear();
  bv2.resize(7);
  bv2.set_bit(3);

  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 20);
  EXPECT_EQ(bv.ToString(), "{3,5,7,15}");
}

// Same as `Union` test above, except uses differing number of inline bits
// between the two bit vectors.
TYPED_TEST(BitVecTest, Union_DifferingInlineBits) {
  TypeParam bv;
  BitVecWithDifferingInlineBits bv2;

  bv2.resize(10);
  bv2.set_bit(5);
  bv2.set_bit(7);

  bv.resize(10);
  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv2.clear();
  bv2.resize(20);
  bv2.set_bit(15);

  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 20);
  EXPECT_EQ(bv.ToString(), "{5,7,15}");

  bv2.clear();
  bv2.resize(7);
  bv2.set_bit(3);

  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 20);
  EXPECT_EQ(bv.ToString(), "{3,5,7,15}");
}

// Tests that `Union` doesn't leak the uninitialized bits in the last word, in
// case of different sizes.
TYPED_TEST(BitVecTest, Union_DifferingSizesDoesntLeak) {
  TypeParam bv(12), bv2(11);
  bv2.set_bit(10);
  bv2.resize(10);
  EXPECT_EQ(bv.ToString(), "{}");
  bv.Union(bv2);
  EXPECT_EQ(bv.size(), 12);
  EXPECT_EQ(bv2.ToString(), "{}");
  EXPECT_EQ(bv.ToString(), "{}");
}

TYPED_TEST(BitVecTest, Intersection) {
  TypeParam bv;
  TypeParam bv2;

  bv.resize(10);
  bv.set_bit(5);
  bv.set_bit(7);

  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{}");

  bv.set_bit(2);
  bv.set_bit(5);

  bv2.clear();
  bv2.resize(5);
  bv2.set_bit(2);
  bv2.set_bit(3);

  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{2}");

  bv.resize(50);
  for (int i = 0; i < 50; ++i) bv.set_bit(i);

  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 50);
  EXPECT_EQ(bv.ToString(), "{2,3}");

  bv2.clear();
  bv2.resize(100);
  bv2.set_bit(3);
  bv2.set_bit(5);
  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 50);
  EXPECT_EQ(bv.ToString(), "{3}");
}

TYPED_TEST(BitVecTest, IsIntersectionNonEmpty) {
  TypeParam bv;
  TypeParam bv2;

  bv.resize(10);
  bv.set_bit(5);
  bv.set_bit(7);

  EXPECT_FALSE(bv.IsIntersectionNonEmpty(bv2));

  bv2.clear();
  bv2.resize(5);
  bv2.set_bit(2);
  bv2.set_bit(3);

  EXPECT_FALSE(bv.IsIntersectionNonEmpty(bv2));

  bv.resize(50);
  for (int i = 0; i < 50; ++i) bv.set_bit(i);

  EXPECT_TRUE(bv.IsIntersectionNonEmpty(bv2));

  bv2.clear();
  bv2.resize(100);
  bv2.set_bit(3);
  bv2.set_bit(5);
  EXPECT_TRUE(bv.IsIntersectionNonEmpty(bv2));
}

// Same as `Intersection` test above, except uses differing number of inline
// bits between the two bit vectors.
TYPED_TEST(BitVecTest, Intersection_DifferingInlineBits) {
  TypeParam bv;
  BitVecWithDifferingInlineBits bv2;

  bv.resize(10);
  bv.set_bit(5);
  bv.set_bit(7);

  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{}");

  bv.set_bit(2);
  bv.set_bit(5);

  bv2.clear();
  bv2.resize(5);
  bv2.set_bit(2);
  bv2.set_bit(3);

  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{2}");

  bv.resize(50);
  for (int i = 0; i < 50; ++i) bv.set_bit(i);

  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 50);
  EXPECT_EQ(bv.ToString(), "{2,3}");

  bv2.clear();
  bv2.resize(100);
  bv2.set_bit(3);
  bv2.set_bit(5);
  bv.Intersection(bv2);
  EXPECT_EQ(bv.size(), 50);
  EXPECT_EQ(bv.ToString(), "{3}");
}

TYPED_TEST(BitVecTest, Difference) {
  TypeParam bv;
  TypeParam bv2;

  bv.resize(10);
  bv.set_bit(5);
  bv.set_bit(7);

  bv2.resize(10);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv2.Difference(bv);
  EXPECT_EQ(bv2.size(), 10);
  EXPECT_EQ(bv2.ToString(), "{}");

  bv2.set_bit(5);
  bv2.set_bit(7);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{}");

  bv.set_bit(5);
  bv.set_bit(7);

  bv2.clear();
  bv2.resize(20);
  bv2.set_bit(15);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv2.clear();
  bv2.resize(7);
  bv2.set_bit(5);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{7}");
}

// Same as `Difference` test above, except uses differing number of inline bits
// between the two bit vectors.
TYPED_TEST(BitVecTest, Difference_DifferingInlineBits) {
  TypeParam bv;
  TypeParam bv2;

  bv.resize(10);
  bv.set_bit(5);
  bv.set_bit(7);

  bv2.resize(10);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv2.Difference(bv);
  EXPECT_EQ(bv2.size(), 10);
  EXPECT_EQ(bv2.ToString(), "{}");

  bv2.set_bit(5);
  bv2.set_bit(7);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{}");

  bv.set_bit(5);
  bv.set_bit(7);

  bv2.clear();
  bv2.resize(20);
  bv2.set_bit(15);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{5,7}");

  bv2.clear();
  bv2.resize(7);
  bv2.set_bit(5);

  bv.Difference(bv2);
  EXPECT_EQ(bv.size(), 10);
  EXPECT_EQ(bv.ToString(), "{7}");
}

// This test is slow, so just test it on a single type.
TEST(BitVecHashTest, ImplementsHashCorrectly) {
  using BitVec = InlinedBitVector<kMaxTestSize>;
  std::vector<BitVec> values;
  for (int s = 0; s < kMaxTestSize; ++s) {
    BitVec vec(s);
    values.push_back(vec);
    // Set a bit every byte.
    for (int i = 0; i < s; i += 7) {
      auto vec2 = vec;
      vec2.set_bit(i);
      values.push_back(vec2);
      if (s > 0) {
        // Create copies of earlier vectors with unused high order bits set.
        vec2.resize(s - 1);
        values.push_back(vec2);
      }
    }
  }
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(values));
}

static void BM_VectorInit(benchmark::State& state) {
  using BitVec = InlinedBitVector<kMaxTestSize>;
  const int arg = state.range(0);

  for (auto s : state) {
    BitVec bv(arg);
    for (int j = 0; j < arg; j++) {
      bv.set_bit(j);
      benchmark::DoNotOptimize(bv);
    }
  }
}
BENCHMARK(BM_VectorInit)->Range(0, 1024);

static void BM_VectorGet(benchmark::State& state) {
  using BitVec = InlinedBitVector<kMaxTestSize>;

  BitVec bv(32);
  FillWithAlternatingBits(bv);
  for (auto s : state) {
    benchmark::DoNotOptimize(bv[3]);
  }
}
BENCHMARK(BM_VectorGet)->Range(1, 1 << 20);

// Make many simultaneously live bit vectors and use them.
static void BM_ManyVectors(benchmark::State& state) {
  using BitVec = InlinedBitVector<64>;

  const int num_vectors = state.range(0);
  std::vector<BitVec> vectors(num_vectors);
  for (BitVec& v : vectors) {
    v.resize(10);
  }

  // Treat one vector access as one iteration so numbers are easily
  // comparable across different values of num_vectors.
  bool x = false;
  while (state.KeepRunningBatch(num_vectors)) {
    for (BitVec& v : vectors) {
      x ^= v[3];
      benchmark::DoNotOptimize(x);
    }
  }
  VLOG(1) << x;
}
BENCHMARK(BM_ManyVectors)->RangeMultiplier(2)->Range(1, 1048576);

}  // namespace
}  // namespace util::bitmap
