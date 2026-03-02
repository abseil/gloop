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

// Copyright 2007, Google Inc.  All rights reserved.

//
// Unit test for base/port.h.
// Many things are tested only to see that they compile.
//
// This file covers only a little of base/port.h.
// Feel free to add more.

#include "gloop/base/port.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "gtest/gtest.h"

namespace port_test {

inline void AlwaysInline() ABSL_ATTRIBUTE_ALWAYS_INLINE;
inline void AlwaysInline() {}
TEST(PortTest, TestAlwaysInline) { AlwaysInline(); }

void Noinline() ABSL_ATTRIBUTE_NOINLINE;
void Noinline() {}
TEST(PortTest, TestNoinline) { Noinline(); }

TEST(PortTest, TestIEEE754Double) {
  {  // Not a number
    uint64_t denormalized = (~0);
    denormalized >>= 1;
    double g = absl::bit_cast<double>(denormalized);
    EXPECT_EQ(fpclassify(g), FP_NAN);
    EXPECT_FALSE(isinf(g));
    EXPECT_TRUE(isnan(g));
  }

  {  // - infinite, + infinite
    double g = -3.0;
    // Using bit_cast so compiler will be unaware of the 0 and will
    // not perform any compilation time shortcuts
    const uint64_t zero64 = 0;
    const double zero = absl::bit_cast<double>(zero64);
    g /= zero;
    EXPECT_EQ(fpclassify(g), FP_INFINITE);
    EXPECT_TRUE(isinf(g));
    EXPECT_FALSE(isnan(g));

    g = 30.0 / zero;
    EXPECT_EQ(fpclassify(g), FP_INFINITE);
    EXPECT_TRUE(isinf(g));
    EXPECT_FALSE(isnan(g));
  }

  {  // + normal, - normal
    double g = 1971.0;
    EXPECT_EQ(fpclassify(g), FP_NORMAL);
    EXPECT_FALSE(isinf(g));
    EXPECT_FALSE(isnan(g));
    g = -9;
    EXPECT_EQ(fpclassify(g), FP_NORMAL);
    EXPECT_FALSE(isinf(g));
    EXPECT_FALSE(isnan(g));
  }

  {  // subnormal
    const uint64_t subnormal = 1;
    const double g = absl::bit_cast<double>(subnormal);
    EXPECT_EQ(fpclassify(g), FP_SUBNORMAL);
    EXPECT_FALSE(isinf(g));
    EXPECT_FALSE(isnan(g));
  }

  {  // + zero, - zero
    double g = 0.0;
    EXPECT_EQ(fpclassify(g), FP_ZERO);
    EXPECT_FALSE(isinf(g));
    EXPECT_FALSE(isnan(g));
    g = -0.0;
    EXPECT_EQ(fpclassify(g), FP_ZERO);
    EXPECT_FALSE(isinf(g));
    EXPECT_FALSE(isnan(g));
  }
}

TEST(PortTest, TestIEEE754Float) {
  {  // Not a number
    uint32_t denormalized = (~0);
    denormalized >>= 1;
    float f = absl::bit_cast<float>(denormalized);
    EXPECT_EQ(fpclassify(f), FP_NAN);
    EXPECT_FALSE(isinf(f));
    EXPECT_TRUE(isnan(f));
  }

  {  // - infinite, + infinite
    float f = -3.0;
    // Using bit_cast so compiler will be unaware of the 0 and will
    // not perform any compilation time shortcuts
    const uint32_t zero32 = 0;
    const float zero = absl::bit_cast<float>(zero32);
    f /= zero;
    EXPECT_EQ(fpclassify(f), FP_INFINITE);
    EXPECT_TRUE(isinf(f));
    EXPECT_FALSE(isnan(f));

    f = 30.0 / zero;
    EXPECT_EQ(fpclassify(f), FP_INFINITE);
    EXPECT_TRUE(isinf(f));
    EXPECT_FALSE(isnan(f));
  }

  {  // + normal, - normal
    float f = 1971.0;
    EXPECT_EQ(fpclassify(f), FP_NORMAL);
    EXPECT_FALSE(isinf(f));
    EXPECT_FALSE(isnan(f));
    f = -9;
    EXPECT_EQ(fpclassify(f), FP_NORMAL);
    EXPECT_FALSE(isinf(f));
    EXPECT_FALSE(isnan(f));
  }

  {  // subnormal
    const uint32_t subnormal = 1;
    const float f = absl::bit_cast<float>(subnormal);
    EXPECT_EQ(fpclassify(f), FP_SUBNORMAL);
    EXPECT_FALSE(isinf(f));
    EXPECT_FALSE(isnan(f));
  }

  {  // + zero, - zero
    float f = 0.0;
    EXPECT_EQ(fpclassify(f), FP_ZERO);
    EXPECT_FALSE(isinf(f));
    EXPECT_FALSE(isnan(f));
    f = -0.0;
    EXPECT_EQ(fpclassify(f), FP_ZERO);
    EXPECT_FALSE(isinf(f));
    EXPECT_FALSE(isnan(f));
  }
}

TEST(PortTest, TestBSwap) {
  EXPECT_EQ(0xAABB, bswap_16(0xBBAA));
  EXPECT_EQ(0xAABBCCDD, bswap_32(0xDDCCBBAA));
  EXPECT_EQ(uint64_t{uint64_t{0xAABBCCDDEEFF0011}},
            bswap_64(uint64_t{0x1100FFEEDDCCBBAA}));
}

TEST(PortTest, TestSNPrintf) {
  static const char kTestString[] = "a test string";
  enum { kTestStringLen = sizeof(kTestString) - 1 };
  char buf[kTestStringLen + 2];  // space for string, nul, and one extra byte

  // Entire buffer is given to snprintf().  Final byute should be undisturbed.
  memset(buf, 'x', sizeof(buf));
  ASSERT_EQ(snprintf(buf, sizeof(buf), "%s", kTestString), kTestStringLen);
  ASSERT_EQ(strlen(kTestString), strlen(buf));
  EXPECT_EQ(buf[kTestStringLen + 1], 'x');
  EXPECT_EQ(strcmp(kTestString, buf), 0);

  // Entire buffer minus one byte; string and nul still fit.
  memset(buf, 'x', sizeof(buf));
  ASSERT_EQ(snprintf(buf, sizeof(buf) - 1, "%s", kTestString), kTestStringLen);
  ASSERT_EQ(strlen(kTestString), strlen(buf));
  EXPECT_EQ(buf[kTestStringLen + 1], 'x');
  EXPECT_EQ(strcmp(kTestString, buf), 0);

  // Entire buffer minus two bytes; one character should be omitted from string.
  memset(buf, 'x', sizeof(buf));
  ASSERT_EQ(snprintf(buf, sizeof(buf) - 2, "%s", kTestString), kTestStringLen);
  ASSERT_EQ(strlen(kTestString) - 1, strlen(buf));
  EXPECT_EQ(buf[kTestStringLen], 'x');
  EXPECT_EQ(strncmp(kTestString, buf, kTestStringLen - 1), 0);

  // One byte buffer.
  memset(buf, 'x', sizeof(buf));
  ASSERT_EQ(snprintf(buf, 1, "%s", kTestString), kTestStringLen);
  ASSERT_EQ(strlen(buf), 0);
  EXPECT_EQ(buf[1], 'x');

  // Zero byte buffer.
  memset(buf, 'x', sizeof(buf));
  ASSERT_EQ(snprintf(buf, 0, "%s", kTestString), kTestStringLen);
  EXPECT_EQ(buf[0], 'x');

  ASSERT_EQ(snprintf(nullptr, 0, "%s", kTestString), kTestStringLen);
}

// Test that code that should compile with ABSL_ATTRIBUTE_NONNULL actually
// will.
void Arg2MayBeNull(int* arg1, int* arg2) ABSL_ATTRIBUTE_NONNULL(1);
void Arg2MayBeNull(int* arg1, int* arg2) {}
void NoArgsMayBeNull(int* arg1, int* arg2, int arg3) ABSL_ATTRIBUTE_NONNULL();
void NoArgsMayBeNull(int* arg1, int* arg2, int arg3) {}
struct Foo {
  void Arg2MayBeNull(int* arg1, int* arg2) ABSL_ATTRIBUTE_NONNULL(2) {}
};
TEST(PortTest, TestAttributeNonnull) {
  int num;
  Arg2MayBeNull(&num, nullptr);
  NoArgsMayBeNull(&num, &num, 0);
  Foo foo;
  foo.Arg2MayBeNull(&num, nullptr);
}

TEST(PortTest, TestAlignedMalloc) {
  for (size_t alignment = 1; alignment <= 1 << 20; alignment <<= 1) {
    void* p = aligned_malloc(1, static_cast<int>(alignment));
    ASSERT_TRUE(p != nullptr) << "aligned_malloc(1, " << alignment << ")";
    uintptr_t p_value = reinterpret_cast<uintptr_t>(p);
    EXPECT_EQ(p_value % alignment, 0);
    aligned_free(p);
  }
}

TEST(PortTest, TestSizedDelete) {
  for (size_t size = 1; size <= 1 << 20; size <<= 1) {
    void* p = ::operator new(size);
    ASSERT_TRUE(p != nullptr);
    ::operator delete(p, size);
  }
}

#ifndef _WIN32
TEST(PortTest, TestPthreadFormat) {
  printf("Thread %" GPRIxPTHREAD "\n", PRINTABLE_PTHREAD(pthread_self()));
  printf("Thread %" GPRIuPTHREAD "\n", PRINTABLE_PTHREAD(pthread_self()));
}
#endif

}  // namespace port_test
