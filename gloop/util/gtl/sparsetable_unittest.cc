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

// Since sparsetable is templatized, it's important that we test every
// function in every class in this file -- not just to see if it
// works, but even if it compiles.

#include "gloop/util/gtl/sparsetable.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

inline std::string AsString(int n) {
  const int N = 64;
  char buf[N];
  snprintf(buf, N, "%d", n);
  return std::string(buf);
}

// Test sparsetable with a POD type, int.
TEST(SparseTableTest, Int) {
  sparsetable<int> x(7), y(70), z;
  x.set(4, 10);
  y.set(12, -12);
  y.set(47, -47);
  y.set(48, -48);
  y.set(49, -49);

  const sparsetable<int> constx(x);
  const sparsetable<int> consty(y);

  // ----------------------------------------------------------------------
  // Test the plain iterators

  EXPECT_THAT(x, ElementsAre(0, 0, 0, 0, 10, 0, 0));

  std::vector<int> const_x_elements;
  for (sparsetable<int>::const_iterator it = x.begin(); it != x.end(); ++it) {
    const_x_elements.push_back(*it);
  }
  EXPECT_THAT(const_x_elements, ElementsAre(0, 0, 0, 0, 10, 0, 0));

  std::vector<int> rx_elements;
  for (sparsetable<int>::reverse_iterator it = x.rbegin(); it != x.rend();
       ++it) {
    rx_elements.push_back(*it);
  }
  EXPECT_THAT(rx_elements, ElementsAre(0, 0, 10, 0, 0, 0, 0));

  std::vector<int> constx_rx_elements;
  for (sparsetable<int>::const_reverse_iterator it = constx.rbegin();
       it != constx.rend(); ++it) {
    constx_rx_elements.push_back(*it);
  }
  EXPECT_THAT(constx_rx_elements, ElementsAre(0, 0, 10, 0, 0, 0, 0));

  EXPECT_THAT(z, IsEmpty());

  {  // array version
    EXPECT_EQ(static_cast<int>(x[3]), 0);
    EXPECT_EQ(static_cast<int>(x[4]), 10);
    EXPECT_EQ(static_cast<int>(x[5]), 0);
  }
  {
    sparsetable<int>::iterator it;  // non-const version
    EXPECT_EQ(static_cast<int>(x.begin()[4]), 10);
    it = x.begin() + 4;  // should point to the non-zero value
    EXPECT_EQ(static_cast<int>(*it), 10);
    it--;
    --it;
    it += 5;
    it -= 2;
    it++;
    ++it;
    it = it - 3;
    it = 1 + it;  // now at 5
    EXPECT_EQ(static_cast<int>(it[-2]), 0);
    EXPECT_EQ(static_cast<int>(it[-1]), 10);
    *it = 55;
    EXPECT_EQ(static_cast<int>(it[0]), 55);
    EXPECT_EQ(static_cast<int>(*it), 55);
    int* x6 = &(it[1]);
    *x6 = 66;
    EXPECT_EQ(static_cast<int>(*(it + 1)), 66);

    // Let's test comparitors as well
    EXPECT_EQ(it, it);
    EXPECT_LE(it, it);
    EXPECT_GE(it, it);

    sparsetable<int>::iterator it_minus_1 = it - 1;
    EXPECT_NE(it, it_minus_1);
    EXPECT_GT(it, it_minus_1);
    EXPECT_GE(it, it_minus_1);
    EXPECT_NE(it_minus_1, it);
    EXPECT_LT(it_minus_1, it);
    EXPECT_LE(it_minus_1, it);

    sparsetable<int>::iterator it_plus_1 = it + 1;
    EXPECT_NE(it, it_plus_1);
    EXPECT_LT(it, it_plus_1);
    EXPECT_LE(it, it_plus_1);
    EXPECT_NE(it_plus_1, it);
    EXPECT_GT(it_plus_1, it);
    EXPECT_GE(it_plus_1, it);
  }
  {
    sparsetable<int>::const_iterator it;  // const version
    EXPECT_EQ(static_cast<int>(x.begin()[4]), 10);
    it = x.begin() + 4;  // should point to the non-zero value
    EXPECT_EQ(*it, 10);
    it--;
    --it;
    it += 5;
    it -= 2;
    it++;
    ++it;
    it = it - 3;
    it = 1 + it;  // now at 5
    EXPECT_EQ(it[-2], 0);
    EXPECT_EQ(it[-1], 10);
    EXPECT_EQ(*it, 55);
    EXPECT_EQ(*(it + 1), 66);

    // Let's test comparitors as well
    EXPECT_EQ(it, it);
    EXPECT_LE(it, it);
    EXPECT_GE(it, it);

    sparsetable<int>::const_iterator it_minus_1 = it - 1;
    EXPECT_NE(it, it_minus_1);
    EXPECT_GT(it, it_minus_1);
    EXPECT_GE(it, it_minus_1);
    EXPECT_NE(it_minus_1, it);
    EXPECT_LT(it_minus_1, it);
    EXPECT_LE(it_minus_1, it);

    sparsetable<int>::const_iterator it_plus_1 = it + 1;
    EXPECT_NE(it, it_plus_1);
    EXPECT_LT(it, it_plus_1);
    EXPECT_LE(it, it_plus_1);
    EXPECT_NE(it_plus_1, it);
    EXPECT_GT(it_plus_1, it);
    EXPECT_GE(it_plus_1, it);
  }

  EXPECT_EQ(x.begin(), x.begin() + 1 - 1);
  EXPECT_LT(x.begin(), x.end());
  EXPECT_FALSE(z.begin() < z.end());
  EXPECT_LE(z.begin(), z.end());
  EXPECT_EQ(z.begin(), z.end());

  // ----------------------------------------------------------------------
  // Test the non-empty iterators

  std::vector<int> nonempty_x;
  for (sparsetable<int>::nonempty_iterator it = x.nonempty_begin();
       it != x.nonempty_end(); ++it) {
    nonempty_x.push_back(*it);
  }
  EXPECT_THAT(nonempty_x, ElementsAre(10, 55, 66));

  std::vector<int> nonempty_y;
  for (sparsetable<int>::const_nonempty_iterator it = y.nonempty_begin();
       it != y.nonempty_end(); ++it) {
    nonempty_y.push_back(*it);
  }
  EXPECT_THAT(nonempty_y, ElementsAre(-12, -47, -48, -49));

  std::vector<int> nonempty_ry;
  for (sparsetable<int>::reverse_nonempty_iterator it = y.nonempty_rbegin();
       it != y.nonempty_rend(); ++it) {
    nonempty_ry.push_back(*it);
  }
  EXPECT_THAT(nonempty_ry, ElementsAre(-49, -48, -47, -12));

  std::vector<int> consty_nonempty_ry;
  for (sparsetable<int>::const_reverse_nonempty_iterator it =
           consty.nonempty_rbegin();
       it != consty.nonempty_rend(); ++it) {
    consty_nonempty_ry.push_back(*it);
  }
  EXPECT_THAT(consty_nonempty_ry, ElementsAre(-49, -48, -47, -12));

  std::vector<int> nonempty_z;
  for (sparsetable<int>::nonempty_iterator it = z.nonempty_begin();
       it != z.nonempty_end(); ++it) {
    nonempty_z.push_back(*it);
  }
  EXPECT_THAT(nonempty_z, IsEmpty());

  {
    sparsetable<int>::nonempty_iterator it;  // non-const version
    EXPECT_EQ(*y.nonempty_begin(), -12);
    EXPECT_EQ(*x.nonempty_begin(), 10);
    it = x.nonempty_begin();
    ++it;  // should be at end
    --it;
    EXPECT_EQ(*it++, 10);
    it--;
    EXPECT_EQ(*it++, 10);
  }
  {
    sparsetable<int>::const_nonempty_iterator it;  // const version
    EXPECT_EQ(*y.nonempty_begin(), -12);
    EXPECT_EQ(*x.nonempty_begin(), 10);
    it = x.nonempty_begin();
    ++it;  // should be at end
    --it;
    EXPECT_EQ(*it++, 10);
    it--;
    EXPECT_EQ(*it++, 10);
  }

  EXPECT_EQ(x.begin(), x.begin() + 1 - 1);
  EXPECT_EQ(z.begin(), z.end());

  // ----------------------------------------------------------------------
  // Test the non-empty iterators get_pos function

  sparsetable<unsigned int> gp(100);
  for (int i = 0; i < 100; i += 9) {
    gp.set(i, i);
  }

  for (sparsetable<unsigned int>::const_nonempty_iterator it =
           gp.nonempty_begin();
       it != gp.nonempty_end(); ++it) {
    EXPECT_EQ(*it, gp.get_pos(it));
  }

  for (sparsetable<unsigned int>::nonempty_iterator it = gp.nonempty_begin();
       it != gp.nonempty_end(); ++it) {
    EXPECT_EQ(*it, gp.get_pos(it));
  }

  // ----------------------------------------------------------------------
  // Test sparsetable functions
  EXPECT_EQ(x.num_nonempty(), 3);
  EXPECT_EQ(x.size(), 7);
  EXPECT_EQ(y.num_nonempty(), 4);
  EXPECT_EQ(y.size(), 70);
  EXPECT_EQ(z.num_nonempty(), 0);
  EXPECT_EQ(z.size(), 0);

  y.resize(48);  // should get rid of 48 and 49
  y.resize(70);  // 48 and 49 should still be gone
  EXPECT_EQ(y.num_nonempty(), 2);
  EXPECT_EQ(y.size(), 70);
  EXPECT_EQ(static_cast<int>(y[12]), -12);
  EXPECT_EQ(y.get(12), -12);
  y.erase(12);
  EXPECT_EQ(y.num_nonempty(), 1);
  EXPECT_EQ(y.size(), 70);
  EXPECT_EQ(static_cast<int>(y[12]), 0);
  EXPECT_EQ(y.get(12), 0);

  using std::swap;
  swap(x, y);

  y.clear();
  EXPECT_NE(y, z);

  y.resize(70);
  for (int i = 10; i < 40; ++i) y[i] = -i;
  y.erase(y.begin() + 15, y.begin() + 30);
  y.erase(y.begin() + 34);
  y.erase(12);
  y.resize(38);
  y.resize(10000);
  y[9898] = -9898;

  std::vector<size_t> set_buckets;
  for (sparsetable<int>::const_iterator it = y.begin(); it != y.end(); ++it) {
    if (y.test(it)) {
      set_buckets.push_back(it - y.begin());
    }
  }
  EXPECT_THAT(set_buckets,
              ElementsAre(10, 11, 13, 14, 30, 31, 32, 33, 35, 36, 37, 9898));
  EXPECT_EQ(y.num_nonempty(), 12);

  std::vector<int> nonempty_y_elements;
  for (sparsetable<int>::const_nonempty_iterator it = y.get_iter(32);
       it != y.nonempty_end(); ++it) {
    nonempty_y_elements.push_back(*it);
  }
  EXPECT_THAT(nonempty_y_elements, ElementsAre(-32, -33, -35, -36, -37, -9898));

  std::vector<int> nonempty_y_down_elements;
  for (sparsetable<int>::nonempty_iterator it = y.get_iter(32);
       it != y.nonempty_begin();) {
    nonempty_y_down_elements.push_back(*--it);
  }
  EXPECT_THAT(nonempty_y_down_elements,
              ElementsAre(-31, -30, -14, -13, -11, -10));

  // ----------------------------------------------------------------------
  // Test I/O
  std::string filestr = testing::TempDir() + "/.sparsetable.test";
  const char* file = filestr.c_str();
  FILE* fp = fopen(file, "wb");
  if (fp == nullptr) {
    // maybe we can't write to /tmp/.  Try the current directory
    file = ".sparsetable.test";
    fp = fopen(file, "wb");
  }
  ASSERT_NE(fp, nullptr) << "Can't open " << file;
  y.write_metadata(fp);  // only write meta-information
  y.write_nopointer_data(fp);
  fclose(fp);

  fp = fopen(file, "rb");
  ASSERT_NE(fp, nullptr) << "Can't open " << file;
  sparsetable<int> y2;
  y2.read_metadata(fp);
  y2.read_nopointer_data(fp);
  fclose(fp);

  std::vector<std::pair<size_t, int>> y2_elements;
  for (sparsetable<int>::const_iterator it = y2.begin(); it != y2.end(); ++it) {
    if (y2.test(it)) {
      y2_elements.push_back({it - y2.begin(), *it});
    }
  }
  EXPECT_THAT(
      y2_elements,
      ElementsAre(std::make_pair(10, -10), std::make_pair(11, -11),
                  std::make_pair(13, -13), std::make_pair(14, -14),
                  std::make_pair(30, -30), std::make_pair(31, -31),
                  std::make_pair(32, -32), std::make_pair(33, -33),
                  std::make_pair(35, -35), std::make_pair(36, -36),
                  std::make_pair(37, -37), std::make_pair(9898, -9898)));
  EXPECT_EQ(y2.num_nonempty(), 12);
  unlink(file);
}

// Test sparsetable with a non-POD type, std::string
TEST(SparseTableTest, String) {
  sparsetable<std::string> x(7), y(70), z;
  x.set(4, "foo");
  y.set(12, "orange");
  y.set(47, "grape");
  y.set(48, "pear");
  y.set(49, "apple");

  // ----------------------------------------------------------------------
  // Test the plain iterators

  std::vector<std::string> x_elements;
  for (sparsetable<std::string>::iterator it = x.begin(); it != x.end(); ++it) {
    x_elements.push_back(*it);
  }
  EXPECT_THAT(x_elements, ElementsAre("", "", "", "", "foo", "", ""));

  std::vector<std::string> z_elements;
  for (sparsetable<std::string>::iterator it = z.begin(); it != z.end(); ++it) {
    z_elements.push_back(*it);
  }
  EXPECT_THAT(z_elements, IsEmpty());

  EXPECT_EQ(x.begin(), x.begin() + 1 - 1);
  EXPECT_LT(x.begin(), x.end());
  EXPECT_FALSE(z.begin() < z.end());
  EXPECT_LE(z.begin(), z.end());
  EXPECT_EQ(z.begin(), z.end());

  // ----------------------------------------------------------------------
  // Test the non-empty iterators
  std::vector<std::string> nonempty_x;
  for (sparsetable<std::string>::nonempty_iterator it = x.nonempty_begin();
       it != x.nonempty_end(); ++it) {
    nonempty_x.push_back(*it);
  }
  EXPECT_THAT(nonempty_x, ElementsAre("foo"));

  std::vector<std::string> nonempty_y;
  for (sparsetable<std::string>::const_nonempty_iterator it =
           y.nonempty_begin();
       it != y.nonempty_end(); ++it) {
    nonempty_y.push_back(*it);
  }
  EXPECT_THAT(nonempty_y, ElementsAre("orange", "grape", "pear", "apple"));

  std::vector<std::string> nonempty_z;
  for (sparsetable<std::string>::nonempty_iterator it = z.nonempty_begin();
       it != z.nonempty_end(); ++it) {
    nonempty_z.push_back(*it);
  }
  EXPECT_THAT(nonempty_z, IsEmpty());

  // ----------------------------------------------------------------------
  // Test sparsetable functions
  EXPECT_EQ(x.num_nonempty(), 1);
  EXPECT_EQ(x.size(), 7);
  EXPECT_EQ(y.num_nonempty(), 4);
  EXPECT_EQ(y.size(), 70);
  EXPECT_EQ(z.num_nonempty(), 0);
  EXPECT_EQ(z.size(), 0);

  y.resize(48);  // should get rid of 48 and 49
  y.resize(70);  // 48 and 49 should still be gone
  EXPECT_EQ(y.num_nonempty(), 2);
  EXPECT_EQ(y.size(), 70);
  EXPECT_EQ(static_cast<std::string>(y[12]), "orange");
  EXPECT_EQ(y.get(12), "orange");
  y.erase(12);
  EXPECT_EQ(y.num_nonempty(), 1);
  EXPECT_EQ(y.size(), 70);
  EXPECT_EQ(static_cast<std::string>(y[12]), "");
  EXPECT_EQ(y.get(12), "");
  using std::swap;
  swap(x, y);

  y.clear();
  EXPECT_NE(y, z);

  y.resize(70);
  for (int i = 10; i < 40; ++i) y.set(i, AsString(-i));
  y.erase(y.begin() + 15, y.begin() + 30);
  y.erase(y.begin() + 34);
  y.erase(12);
  y.resize(38);
  y.resize(10000);
  y.set(9898, AsString(-9898));

  std::vector<size_t> set_buckets;
  for (sparsetable<std::string>::const_iterator it = y.begin(); it != y.end();
       ++it) {
    if (y.test(it)) {
      set_buckets.push_back(it - y.begin());
    }
  }
  EXPECT_THAT(set_buckets,
              ElementsAre(10, 11, 13, 14, 30, 31, 32, 33, 35, 36, 37, 9898));
  EXPECT_EQ(y.num_nonempty(), 12);

  std::vector<std::string> nonempty_y_elements;
  for (sparsetable<std::string>::const_nonempty_iterator it = y.get_iter(32);
       it != y.nonempty_end(); ++it) {
    nonempty_y_elements.push_back(*it);
  }
  EXPECT_THAT(nonempty_y_elements,
              ElementsAre("-32", "-33", "-35", "-36", "-37", "-9898"));

  std::vector<std::string> nonempty_y_down_elements;
  for (sparsetable<std::string>::nonempty_iterator it = y.get_iter(32);
       it != y.nonempty_begin();) {
    nonempty_y_down_elements.push_back(*--it);
  }
  EXPECT_THAT(nonempty_y_down_elements,
              ElementsAre("-31", "-30", "-14", "-13", "-11", "-10"));
}

// An instrumented allocator that keeps track of all calls to
// allocate/deallocate/construct/destroy. It stores the number of times
// they were called and the values they were called with. Such information is
// stored in the following global variables.

static size_t sum_allocate_bytes;
static size_t sum_deallocate_bytes;

void ResetAllocatorCounters() {
  sum_allocate_bytes = 0;
  sum_deallocate_bytes = 0;
}

template <class T>
class instrumented_allocator {
 public:
  typedef T value_type;
  typedef uint16_t size_type;
  typedef ptrdiff_t difference_type;

  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;

  instrumented_allocator() = default;
  instrumented_allocator(const instrumented_allocator&) = default;
  ~instrumented_allocator() = default;

  pointer address(reference r) const { return &r; }
  const_pointer address(const_reference r) const { return &r; }

  pointer allocate(size_type n, const_pointer = nullptr) {
    sum_allocate_bytes += n * sizeof(value_type);
    return static_cast<pointer>(malloc(n * sizeof(value_type)));
  }
  void deallocate(pointer p, size_type n) {
    sum_deallocate_bytes += n * sizeof(value_type);
    free(p);
  }

  size_type max_size() const {
    return static_cast<size_type>(-1) / sizeof(value_type);
  }

  void construct(pointer p, const value_type& val) { new (p) value_type(val); }
  void destroy(pointer p) { p->~value_type(); }

  template <class U>
  explicit instrumented_allocator(const instrumented_allocator<U>&) {}

  template <class U>
  struct rebind {
    typedef instrumented_allocator<U> other;
  };

 private:
  void operator=(const instrumented_allocator&);
};

template <class T>
inline bool operator==(const instrumented_allocator<T>&,
                       const instrumented_allocator<T>&) {
  return true;
}

template <class T>
inline bool operator!=(const instrumented_allocator<T>&,
                       const instrumented_allocator<T>&) {
  return false;
}

// Test sparsetable with instrumented_allocator.
TEST(SparseTableTest, Allocator) {
  ResetAllocatorCounters();

  // POD (int32) with instrumented_allocator.
  typedef sparsetable<int, DEFAULT_SPARSEGROUP_SIZE,
                      instrumented_allocator<int>>
      IntSparseTable;
  const int element_size = sizeof(IntSparseTable::value_type);

  IntSparseTable* s1 = new IntSparseTable(10000);
  EXPECT_GT(sum_allocate_bytes, 0);
  for (int i = 0; i < 10000; ++i) {
    s1->set(i, 0);
  }
  EXPECT_GE(sum_allocate_bytes, 10000 * element_size);
  ResetAllocatorCounters();
  delete s1;
  EXPECT_GE(sum_deallocate_bytes, 10000 * element_size);

  IntSparseTable* s2 = new IntSparseTable(1000);
  IntSparseTable* s3 = new IntSparseTable(1000);

  for (int i = 0; i < 1000; ++i) {
    s2->set(i, 0);
    s3->set(i, 0);
  }
  EXPECT_GE(sum_allocate_bytes, 2000 * element_size);

  ResetAllocatorCounters();
  s3->clear();
  EXPECT_GE(sum_deallocate_bytes, 1000 * element_size);

  ResetAllocatorCounters();
  s2->swap(*s3);  // s2 is empty after the swap
  s2->clear();
  EXPECT_LT(sum_deallocate_bytes, 1000 * element_size);
  for (int i = 0; i < static_cast<int>(s3->size()); ++i) {
    s3->erase(i);
  }
  EXPECT_GE(sum_deallocate_bytes, 1000 * element_size);
  delete s2;
  delete s3;

  // POD (int) with default allocator.
  sparsetable<int> x, y;
  for (int s = 1000; s <= 40000; s += 1000) {
    x.resize(s);
    for (int i = 0; i < s; ++i) {
      x.set(i, i + 1);
    }
    y = x;
    for (int i = 0; i < s; ++i) {
      y.erase(i);
    }
    y.swap(x);
  }
  EXPECT_EQ(x.num_nonempty(), 0);
  EXPECT_EQ(static_cast<int>(y[0]), 1);
  EXPECT_EQ(static_cast<int>(y[39999]), 40000);
  y.clear();

  // POD (int) with std allocator.
  sparsetable<int, DEFAULT_SPARSEGROUP_SIZE, std::allocator<int>> u, v;
  for (int s = 1000; s <= 40000; s += 1000) {
    u.resize(s);
    for (int i = 0; i < s; ++i) {
      u.set(i, i + 1);
    }
    v = u;
    for (int i = 0; i < s; ++i) {
      v.erase(i);
    }
    v.swap(u);
  }
  EXPECT_EQ(u.num_nonempty(), 0);
  EXPECT_EQ(static_cast<int>(v[0]), 1);
  EXPECT_EQ(static_cast<int>(v[39999]), 40000);
  v.clear();

  // Non-POD (string) with default allocator.
  sparsetable<std::string> a, b;
  for (int s = 1000; s <= 40000; s += 1000) {
    a.resize(s);
    for (int i = 0; i < s; ++i) {
      a.set(i, "aa");
    }
    b = a;
    for (int i = 0; i < s; ++i) {
      b.erase(i);
    }
    b.swap(a);
  }
  EXPECT_EQ(a.num_nonempty(), 0);
  EXPECT_EQ(b.get(0), "aa");
  EXPECT_EQ(b.get(39999), "aa");
  b.clear();
}

}  // namespace
}  // namespace gtl
