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

#include <cstdint>
#include <utility>

#include "gloop/gloop_test.h"
#include "gloop/util/gtl/unique_array.h"

template <typename T>
void TestFillArray2D(int32_t h, int32_t w) {
  Array2D<T> a(h, w);
  int ctr;

  ctr = 0;

  // Fill 'a' with numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      a(y, x) = static_cast<T>(ctr);
    }
  }

  ctr = 0;

  // Check to see 'a' got filled correctly
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      EXPECT_EQ(a(y, x), static_cast<T>(ctr));
    }
  }

  // Check contains functions
  EXPECT_FALSE(a.Contains(-1, 0));
  EXPECT_FALSE(a.Contains(0, -1));
  EXPECT_FALSE(a.Contains(h - 1, w));
  EXPECT_FALSE(a.Contains(h, w - 1));
  bool array_has_elements = (h != 0) && (w != 0);
  EXPECT_EQ(a.Contains(0, 0), array_has_elements);
  EXPECT_EQ(a.Contains(h - 1, w - 1), array_has_elements);
  if (h > 4 && w > 2) {
    EXPECT_TRUE(a.ContainsWithMargin(2, 1, 2, 1));
    EXPECT_FALSE(a.ContainsWithMargin(2 - 1, 1, 2, 1));
    EXPECT_FALSE(a.ContainsWithMargin(2, 1 - 1, 2, 1));
    EXPECT_TRUE(a.ContainsWithMargin(h - 2 - 1, w - 1 - 1, 2, 1));
    EXPECT_FALSE(a.ContainsWithMargin(h - 2 - 1, w - 1, 2, 1));
    EXPECT_FALSE(a.ContainsWithMargin(h - 2, w - 1 - 1, 2, 1));
  }
}

template <typename T>
void TestFillDefaultArray2D(int32_t h, int32_t w) {
  T value = 42;
  Array2D<T> a1(h, w, value);
  Array2D<T> a2(h, w);
  a2.Fill(value);

  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      EXPECT_EQ(a1(y, x), value);
      EXPECT_EQ(a2(y, x), value);
    }
  }
}

template <typename T>
void TestArray2DSharing(int32_t h, int32_t w) {
  Array2D<T> a(h, w);
  Array2D<T> b(SHARE_WITH_INSTANCE, &a);
  int ctr;

  ctr = 0;

  // Fill 'a' with numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      a(y, x) = static_cast<T>(ctr);
    }
  }

  ctr = 0;

  // Make sure 'b' contains the same numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(a(y, x), b(y, x));
    }
  }
}

template <typename T>
void TestArray2DCopying(int32_t h, int32_t w) {
  Array2D<T> a(h, w);
  int ctr;

  ctr = 0;

  // Fill 'a' with numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      a(y, x) = static_cast<T>(ctr);
    }
  }

  Array2D<T> b(COPY_FROM_INSTANCE, &a);

  ctr = 0;

  // Make sure 'b' contains the same numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(a(y, x), b(y, x));
    }
  }

  // Change numbers in 'b'
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      --b(y, x);
    }
  }

  // Make sure they got changed correctly
  // and independently from 'a'
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(b(y, x), (a(y, x) - 1));
    }
  }
}

template <typename T>
void TestForeignShareArray2D(int32_t h, int32_t w) {
  // A one dimensional buffer whose memory is used by the 2D array 'a'
  auto single_d_buffer = gtl::MakeUniqueArrayForOverwrite<T>(h * w);
  // This 2D array mirrors the contents of the single_d_buffer
  Array2D<T> a(SHARE_WITH_FOREIGN_INSTANCE, h, w, single_d_buffer.data());
  int ctr;

  ctr = 0;

  // Fill 'a' with numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      a(y, x) = static_cast<T>(ctr);
    }
  }

  ctr = 0;

  // Check to see 'a' got filled correctly
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      EXPECT_EQ(a(y, x), static_cast<T>(ctr));
    }
  }

  ctr = 0;

  // Check to see if the single_d_buffer is
  // filled with exactly the same items
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      EXPECT_EQ(single_d_buffer[ctr], static_cast<T>(ctr));
    }
  }

  // Create a new buffer
  auto another_buffer = gtl::MakeUniqueArrayForOverwrite<T>(h * w);

  ctr = 0;

  // Fill it with different numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      another_buffer[ctr] = static_cast<T>(ctr * 2);
    }
  }

  // Remap the array to the new buffer
  a.RemapToNewBuffer(another_buffer.data());

  ctr = 0;

  // Check to see that the numbers are correct
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      EXPECT_EQ(a(y, x), static_cast<T>(ctr * 2));
    }
  }

  ctr = 0;

  // Check to see that the other buffer's numbers
  // are still in tact
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x, ++ctr) {
      EXPECT_EQ(single_d_buffer[ctr], static_cast<T>(ctr));
    }
  }
}

// Test a reallocated array has the right size and we can read and
// write to the first and last element without dying.
template <typename T>
void TestRealloc(int32_t h, int32_t w) {
  Array2D<T> array;
  EXPECT_EQ(array.height(), 0);
  EXPECT_EQ(array.width(), 0);
  array.Realloc(h, w);
  EXPECT_EQ(array.height(), h);
  EXPECT_EQ(array.width(), w);
  if (h > 0 && w > 0) {
    array(0, 0) = 5;
    EXPECT_EQ(array(0, 0), 5);
    // Remember array(0, 0) and array(h - 1, w - 1) might be the same
    // array element. Test separately.
    array(h - 1, w - 1) = 6;
    EXPECT_EQ(array(h - 1, w - 1), 6);
  }
}

// Tests an Array2D object can be copied.
template <typename T>
void TestCopyConstructorAndAssignment(int32_t h, int32_t w) {
  // Tests copy constructor.
  const T kTestData = static_cast<T>(10);
  Array2D<T> a(h, w, kTestData);
  Array2D<T> b(a);
  EXPECT_EQ(b.height(), h);
  EXPECT_EQ(a.width(), w);
  // Make sure 'b' contains the same numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(a(y, x), b(y, x));
    }
  }

  // Tests copy assignment operator.
  Array2D<T> c(1, 1, 0);
  c = a;
  EXPECT_EQ(c.height(), h);
  EXPECT_EQ(c.width(), w);
  // Make sure 'c' contains the same numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(a(y, x), c(y, x));
    }
  }
}

// Tests an Array2D object can be moved.
template <typename T>
void TestMoveConstructorAndAssignment(int32_t h, int32_t w) {
  // Tests move constructor.
  const T kTestData = static_cast<T>(10);
  Array2D<T> a(h, w, kTestData);
  T* data_ptr = a.data();
  Array2D<T> b(std::move(a));
  EXPECT_EQ(b.height(), h);
  EXPECT_EQ(b.width(), w);
  EXPECT_EQ(b.data(), data_ptr);
  // Make sure 'b' contains the same numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(b(y, x), kTestData);
    }
  }

  // Tests move assignment operator.
  Array2D<T> c(1, 1, 0);
  c = std::move(b);
  EXPECT_EQ(c.height(), h);
  EXPECT_EQ(c.width(), w);
  EXPECT_EQ(c.data(), data_ptr);
  // Make sure 'c' contains the same numbers
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      EXPECT_EQ(c(y, x), kTestData);
    }
  }
}

// Perform a test battery on Array2D
// of height 'h' and width 'w' .
template <typename T>
void FullTestArray2D(int32_t h, int32_t w) {
  TestFillArray2D<T>(h, w);
  TestFillDefaultArray2D<T>(h, w);
  TestArray2DSharing<T>(h, w);
  TestArray2DCopying<T>(h, w);
  TestForeignShareArray2D<T>(h, w);
  TestRealloc<T>(h, w);
  TestCopyConstructorAndAssignment<T>(h, w);
  TestMoveConstructorAndAssignment<T>(h, w);
}

TEST(Array2DTest, Int3x4) { FullTestArray2D<int>(3, 4); }

TEST(Array2DTest, Int5x2) { FullTestArray2D<int>(5, 2); }

TEST(Array2DTest, Int1x1) { FullTestArray2D<int>(1, 1); }

TEST(Array2DTest, Int0x0) { FullTestArray2D<int>(0, 0); }

TEST(Array2DTest, Float2x9) { FullTestArray2D<float>(2, 9); }

TEST(Array2DTest, Float7x4) { FullTestArray2D<float>(7, 4); }

TEST(Array2DTest, Double5x8) { FullTestArray2D<double>(5, 8); }
