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

#include "gloop/util/hash/transparent_hash.h"

#include <string.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/stl_util.h"
#include "gtest/gtest.h"

namespace {

struct A {};
struct B {};

bool operator==(A, B) { return true; }
bool operator==(B, A) { return false; }

TEST(TransparentHash, TransparentEq) {
  // TransparentEq()(x, y) should call x == y, not y == x.
  util_hash::TransparentEq eq;
  EXPECT_TRUE(eq(A(), B()));
  EXPECT_FALSE(eq(B(), A()));
}

TEST(TransparentHash, StringHash) {
  util_hash::StringHash h;

  EXPECT_EQ(h(absl::string_view("")), h(std::string("")));
  EXPECT_EQ(h(absl::string_view("abcde")), h(std::string("abcde")));
  EXPECT_EQ(h(std::vector<char>{'a', 'b', 'c', 'd', 'e'}),
            h(std::string("abcde")));
  char c1[] = {'a', 'b', 'c', 'd', 'e', 0};
  EXPECT_EQ(h("abcde"), h(c1));
}

TEST(TransparentHash, StringEq) {
  util_hash::StringEq equals;

  // Use const StringEq to ensure that all of the operator() overloads are
  // const.
  const util_hash::StringEq& eq = equals;

  std::string tmp1("a");
  std::string tmp2("a");

  // Create objects x, y, and z such that they're equal, but they store their
  // chars in three different pieces of memory.
  const char* x = tmp1.c_str();
  std::string y("a");
  absl::string_view z(tmp2);

  // Call eq::operator() with all combinations of const char*, string, and
  // StringPiece.
  EXPECT_TRUE(eq(x, x));
  EXPECT_TRUE(eq(x, y));
  EXPECT_TRUE(eq(x, z));
  EXPECT_TRUE(eq(y, x));
  EXPECT_TRUE(eq(y, y));
  EXPECT_TRUE(eq(y, z));
  EXPECT_TRUE(eq(z, x));
  EXPECT_TRUE(eq(z, y));
  EXPECT_TRUE(eq(z, z));
}

TEST(TransparentHash, StringNotEq) {
  util_hash::StringEq equals;
  const util_hash::StringEq& eq = equals;

  typedef std::basic_string<char, std::char_traits<char>,
                            gtl::STLCountingAllocator<char>>
      CountedString;
  int64_t allocated = 0;

  std::string x1("a");
  CountedString y1("a", gtl::STLCountingAllocator<char>(&allocated));
  absl::string_view z1(x1);

  std::string x2("b");
  CountedString y2("b", gtl::STLCountingAllocator<char>(&allocated));
  absl::string_view z2(x2);

  // Call eq::operator() with all combinations of the types above.
  EXPECT_TRUE(eq(x1, x1));
  EXPECT_TRUE(eq(x1, y1));
  EXPECT_TRUE(eq(x1, z1));
  EXPECT_TRUE(eq(y1, x1));
  EXPECT_TRUE(eq(y1, y1));
  EXPECT_TRUE(eq(y1, z1));
  EXPECT_TRUE(eq(z1, x1));
  EXPECT_TRUE(eq(z1, y1));
  EXPECT_TRUE(eq(z1, z1));

  EXPECT_FALSE(eq(x1, x2));
  EXPECT_FALSE(eq(x1, y2));
  EXPECT_FALSE(eq(x1, z2));
  EXPECT_FALSE(eq(y1, x2));
  EXPECT_FALSE(eq(y1, y2));
  EXPECT_FALSE(eq(y1, z2));
  EXPECT_FALSE(eq(z1, x2));
  EXPECT_FALSE(eq(z1, y2));
  EXPECT_FALSE(eq(z1, z2));
}

TEST(TransparentHash, EqualToWithNulls) {
  util_hash::StringEq equals;
  const util_hash::StringEq& eq = equals;

  // These two objects should not be considered equal.
  std::string x("a\0b", 3);
  const char* y = "a";

  EXPECT_FALSE(eq(x, y));
  EXPECT_FALSE(eq(y, x));
  EXPECT_FALSE(eq(absl::string_view(x), y));
  EXPECT_FALSE(eq(y, absl::string_view(x)));
  EXPECT_FALSE(eq(absl::string_view("\0", 1), "\0"));
}

// A class which looks like a string, for the purposes of StringEq, and whose
// data() method returns a T.
template <class T>
struct StringishType {
  T data() const { return nullptr; }
  size_t size() const { return 0; }
};

// Check that StringEq is happy to compare two objects whose data() methods
// return types which differ only in their const-ness.
TEST(TransparentHash, StringEqTypes) {
  util_hash::StringEq equals;
  const util_hash::StringEq& eq = equals;

  // We don't care about the value returned by eq -- we just want to check that
  // the call passes StringEq's static asserts and compiles.
  eq(StringishType<char*>(), StringishType<const char*>());
  eq(StringishType<const char*>(), StringishType<char*>());
  eq(StringishType<const char* const>(), StringishType<char*>());
  eq(StringishType<char*>(), StringishType<const char* const>());
}

// Get a vector containing some strings of the given length.
std::vector<std::string> GetStrings(int length) {
  std::vector<std::string> r;
  for (int i = 0; i < 4096; i++) {
    r.push_back(absl::StrFormat("%0*d", length, i).substr(0, length));
  }
  return r;
}

// Check strings for equalty using EqualityPredicate.
template <class EqualityPredicate>
void BM_StringEq(benchmark::State& state) {
  const int str_length = state.range(0);
  std::vector<std::string> strings1 = GetStrings(str_length);
  std::vector<std::string> strings2 = GetStrings(str_length);

  EqualityPredicate eq;
  for (auto _ : state) {  // NOLINT
    for (size_t j = 0; j < strings1.size(); j++) {
      // Thwart compiler optimizations (strings::memeq can be inlined).
      bool res = eq(strings1[j], strings2[j]);
      benchmark::DoNotOptimize(res);
    }
  }
}

// This is just like StringEq, but it calls memcmp instead of
// std::equal.
struct StringEqWithMemcmp {
  template <class T, class U>
  bool operator()(const T& t, const U& u) const {
    const char* t_data = t.data();
    const char* u_data = u.data();

    if (t.size() != u.size()) {
      return false;
    }

    if (t_data == u_data) {
      return true;
    }

    return memcmp(t_data, u_data, t.size()) == 0;
  }
};

BENCHMARK_TEMPLATE(BM_StringEq, util_hash::StringEq)
    ->Arg(4)
    ->Arg(6)
    ->Arg(8)
    ->Arg(12)
    ->Arg(16)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Arg(2048)
    ->Arg(4 * 1024)
    ->Arg(8 * 1024);

BENCHMARK_TEMPLATE(BM_StringEq, StringEqWithMemcmp)
    ->Arg(4)
    ->Arg(6)
    ->Arg(8)
    ->Arg(12)
    ->Arg(16)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Arg(2048)
    ->Arg(4 * 1024)
    ->Arg(8 * 1024);

}  // anonymous namespace
