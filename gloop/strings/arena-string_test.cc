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

#include "gloop/strings/arena-string.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "absl/base/casts.h"
#include "absl/container/fixed_array.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/base/arena.h"
#include "gloop/base/init_google.h"
#include "gloop/util/random/distributions.h"
#include "gloop/util/random/mt_random.h"
#include "gloop/util/random/random_base.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, test_size, 100, "Number of strings to test");
ABSL_FLAG(int32_t, log_max_length, 16, "N, where maximum string length is 2^N");

namespace strings {

TEST(ArenaStringTest, Simple) {
  std::vector<size_t> sizes = {0,   1,   2,     3,     63,    64,    127,  128,
                               255, 256, 16201, 32767, 32768, 65535, 65536};

  UnsafeArena arena(1 << 20);
  for (auto size : sizes) {
    SCOPED_TRACE(size);
    std::string s(size, 'a');
    ArenaString a(s, &arena);
    EXPECT_EQ(a.str(), s);
  }
}

// Templatized so we can run with both BaseArena, its derived classes and C++
// allocators.
template <class A>
void TestAssign(A a) {
  MTRandom rng(testing::GTEST_FLAG(random_seed));
  std::vector<std::string> data;
  std::vector<ArenaString> arena_str;

  data.reserve(absl::GetFlag(FLAGS_test_size));
  arena_str.reserve(absl::GetFlag(FLAGS_test_size));

  for (int i = 0; i < absl::GetFlag(FLAGS_test_size); i++) {
    // using a skewed distribution over string lengths focuses the test on short
    // strings, including length 0.
    data.push_back(
        std::string(util_random::SkewedLow<int32_t>(
                        rng, 0, (1 << absl::GetFlag(FLAGS_log_max_length)) - 1),
                    'a' + rng.Uniform(26)));
    const auto& str = data.back();

    VLOG(1) << "data[" << i << "] size=" << str.size() << ": "
            << str.substr(0, 3) << "...";

    // test both assign() and constructor.
    if (absl::Bernoulli(rng, 1.0 / 2)) {
      arena_str.resize(i + 1);
      arena_str[i].assign(str, a);
    } else {
      arena_str.push_back(ArenaString(str, a));
    }
  }

  for (int i = 0; i < absl::GetFlag(FLAGS_test_size); i++) {
    SCOPED_TRACE(i);

    EXPECT_EQ(data[i].size(), arena_str[i].size());
    EXPECT_EQ(data[i].empty(), arena_str[i].empty());
    EXPECT_EQ(0, memcmp(data[i].data(), arena_str[i].data(), data[i].size()));

    absl::string_view str(data[i]);
    EXPECT_EQ(str, arena_str[i].str());

    arena_str[i].clear();
    EXPECT_TRUE(arena_str[i].str().empty());
    EXPECT_TRUE(arena_str[i].empty());
    EXPECT_EQ(0, arena_str[i].size());
  }
}

TEST(ArenaStringTest, AssignUnsafeArena) {
  UnsafeArena arena(1 << 20);
  TestAssign(&arena);
}

TEST(ArenaStringTest, AssignSafeArena) {
  SafeArena arena(1 << 20);
  TestAssign(&arena);
}

TEST(ArenaStringTest, AssignBaseArena) {
  UnsafeArena arena(1 << 20);
  TestAssign(absl::implicit_cast<BaseArena*>(&arena));
}

// Test static encode and decode methods.
TEST(ArenaStringTest, EncodeDecode) {
  MTRandom rng(testing::GTEST_FLAG(random_seed));

  // allocate a few extra characters so we can check for buffer overruns.
  int buf_extra = 16;
  int buf_size =
      ArenaString::EncSize(1 << absl::GetFlag(FLAGS_log_max_length)) +
      buf_extra;
  absl::FixedArray<char, 0> buf(buf_size);
  memset(buf.data(), 0, buf.size());

  for (int i = 0; i < absl::GetFlag(FLAGS_test_size); i++) {
    // using a skewed distribution over string lengths focuses the test on short
    // strings, including length 0.
    std::string raw = rng.RandString(util_random::SkewedLow<int32_t>(
        rng, 0, (1 << absl::GetFlag(FLAGS_log_max_length)) - 1));
    absl::string_view str(raw);

    // encode the string; it should return data.
    char* enc = ArenaString::Encode(str, buf.data());
    if (raw.size() < 128) {
      EXPECT_EQ(&buf[1], enc);
    } else {
      EXPECT_EQ(&buf[4], enc);
    }

    // ensure we didn't write past the end of the encoding
    for (int j = ArenaString::EncSize(str.size()); j < buf_extra; j++) {
      EXPECT_EQ(0, buf[j]) << "size=" << str.size() << " j=" << j;
    }

    // decode and verify the string.
    absl::string_view dec = ArenaString::Decode(enc);
    EXPECT_EQ(str, dec);

    // clear buf
    memset(buf.data(), 0, ArenaString::EncSize(str.size()));
  }
}

TEST(ArenaStringTest, Empty) {
  UnsafeArena arena(1 << 10);
  EXPECT_TRUE(ArenaString().empty());
  EXPECT_TRUE(ArenaString("", &arena).empty());
  EXPECT_FALSE(ArenaString("a", &arena).empty());
}

//////////////////////////////// Benchmarks ////////////////////////////////

// The BM_alloc benchmarks allocate many ArenaStrings on an arena.  Most of the
// time, particularly for longer strings, is spent in arena overhead and memcpy.
// BM_alloc_cstring serves as a control, so we can see how ArenaString compares
// to its overhead.
//
// The BM_copy benchmarks encode a string repeatedly in the same chunk of
// memory.  This eliminates the arena overhead, but keeps the memcpy overhead.
// Because memcpy is faster when copying to a word-aligned chunk of memory, we
// continually rotate the destination buffer.  We compare copying an ArenaString
// to copying a string_view.
//
// Next we have BM_encode and BM_decode, which measure the overhead of the
// static ArenaString::Encode and ::Decode methods, without the memcpy.  This is
// essentially the cost of encoding and decoding a varint.
//
// Finally, we have BM_arenastring_str, _size, and _data, which measure these
// three ArenaString accessor methods.

static void BM_alloc_cstring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  UnsafeArena arena(1 << 20);

  for (auto s : state) {
    char* buf = arena.Alloc(len + 1);
    memcpy(buf, x.data(), x.size());
    buf[len] = '\0';
  }
}
BENCHMARK(BM_alloc_cstring)->Range(0, 1 << 10);

// Benchmarks assign(string_view str, UnsafeArena* arena)
static void BM_alloc_unsafearenastring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view str(x.data(), x.size());
  UnsafeArena arena(1 << 20);
  ArenaString a;

  for (auto s : state) {
    a.assign(str, &arena);
  }
}
BENCHMARK(BM_alloc_unsafearenastring)->Range(0, 1 << 10);

// Benchmarks assign(string_view str, BaseArena* arena) with an
// UnsafeArena; this measures the virtual function overhead from passing
// BaseArena instead of UnsafeArena.
static void BM_alloc_basearenastring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view str(x.data(), x.size());
  UnsafeArena uarena(1 << 20);
  BaseArena* arena = &uarena;
  ArenaString a;

  for (auto s : state) {
    a.assign(str, arena);
  }
}
BENCHMARK(BM_alloc_basearenastring)->Range(0, 1 << 10);

// Benchmarks assign(string_view str, BaseArena* arena) with a SafeArena;
// this measures the additional overhead of a SafeArena over UnsafeArena.
static void BM_alloc_safearenastring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view str(x.data(), x.size());
  SafeArena arena(1 << 20);
  ArenaString a;

  for (auto s : state) {
    a.assign(str, &arena);
  }
}
BENCHMARK(BM_alloc_safearenastring)->Range(0, 1 << 10);

// Benchmarks copying an ArenaString repeatedly into the same chunk of memory.
static void BM_copy_arenastring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view a(x.data(), x.size());
  absl::FixedArray<char, 0> buf(ArenaString::EncSize(len) + 7);
  char* data;

  size_t n = state.max_iterations;
  for (auto s : state) {
    // rotate the buffer to avoid word-alignment effects in memcpy
    data = ArenaString::Encode(a, &buf[(--n & 7)]);
  }
  a = ArenaString::Decode(data);
  CHECK_EQ(0, memcmp(x.data(), a.data(), len));
  CHECK_EQ(len, a.size());
}
BENCHMARK(BM_copy_arenastring)->Range(0, 4 << 10);

// Benchmarks copying a string_view repeatedly into the same chunk of memory.
static void BM_copy_stringpiece(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  const char* data = x.data();
  absl::FixedArray<char, 0> buf(len + 7);
  absl::string_view a;

  size_t n = state.max_iterations;
  for (auto s : state) {
    // rotate the buffer to avoid word-alignment effects in memcpy
    memcpy(&buf[(n & 7)], data, len);
    a = absl::string_view(&buf[(--n & 7)], len);
  }
  CHECK_EQ(0, memcmp(x.data(), a.data(), len));
  CHECK_EQ(len, a.size());
}
BENCHMARK(BM_copy_stringpiece)->Range(0, 4 << 10);

class ArenaStringAccess {
 public:
  static char* EncodeLen(char* buf, uint32_t len) {
    return ArenaString::EncodeLen(buf, len);
  }
};

// Benchmarks encoding an ArenaString without memcpy; this just consists of
// encoding the length as a varint.
static void BM_encode_arenastring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view a(x.data(), x.size());
  absl::FixedArray<char, 0> buf(ArenaString::EncSize(len) + 7);
  ArenaString::Encode(a, buf.data());

  size_t n = state.max_iterations;
  for (auto s : state) {
    // rotate the buffer to avoid word-alignment effects in memcpy
    ArenaStringAccess::EncodeLen(&buf[(--n & 7)], len);
  }
}
BENCHMARK(BM_encode_arenastring)->Range(0, 1 << 16);

// Benchmarks decoding an ArenaString; this basically consists of decoding the
// varint length.
static void BM_decode_arenastring(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::FixedArray<char, 0> buf(ArenaString::EncSize(len));
  ArenaString::Encode(x, buf.data());

  for (auto s : state) {
    benchmark::DoNotOptimize(ArenaString::Decode(buf.data()));
  }
}
BENCHMARK(BM_decode_arenastring)->Range(0, 1 << 16);

// Benchmarks ArenaString::str()
static void BM_arenastring_str(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view str(x.data(), x.size());
  UnsafeArena arena(1 << 16);
  ArenaString a(str, &arena);

  absl::string_view st;
  for (auto s : state) {
    st = a.str();
  }
  CHECK(st.size() == x.size());
}
BENCHMARK(BM_arenastring_str)->Range(0, 1 << 16);

// Benchmarks ArenaString::size()
static void BM_arenastring_size(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view str(x.data(), x.size());
  UnsafeArena arena(1 << 16);
  ArenaString a(str, &arena);

  int size = 0;
  for (auto s : state) {
    size = a.size();
  }
  CHECK(size == len);
}
BENCHMARK(BM_arenastring_size)->Range(0, 1 << 16);

// Benchmarks ArenaString::data()
static void BM_arenastring_data(benchmark::State& state) {
  const int len = state.range(0);

  std::string x(len, 'a');
  absl::string_view str(x.data(), x.size());
  UnsafeArena arena(1 << 16);
  ArenaString a(str, &arena);

  const char* d = x.data();
  for (auto s : state) {
    d = a.data();
  }
  CHECK(d != x.data());
}
BENCHMARK(BM_arenastring_data)->Range(0, 1 << 16);

}  // namespace strings

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  LOG(INFO) << "--test_random_seed=" << testing::GTEST_FLAG(random_seed);

  return RUN_ALL_TESTS();
}
