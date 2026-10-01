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
#include <type_traits>
#include <vector>

#include "absl/base/casts.h"
#include "absl/container/fixed_array.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/base/arena.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/util/random/distributions.h"
#include "gloop/util/random/mt_random.h"
#include "gloop/util/random/random_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, test_size, 100, "Number of strings to test");
ABSL_FLAG(int32_t, log_max_length, 16, "N, where maximum string length is 2^N");

namespace strings {

class ArenaStringAccess {
 public:
  static char* EncodeLen(char* buf, uint32_t len) {
    return ArenaString::EncodeLen(buf, len);
  }
};

namespace {

using ::benchmark::DoNotOptimize;
using ::testing::IsEmpty;

TEST(ArenaStringTest, Simple) {
  const std::vector<size_t> sizes = {0,     1,     2,     3,     63,
                                     64,    127,   128,   255,   256,
                                     16201, 32767, 32768, 65535, 65536};

  UnsafeArena arena(1 << 20);
  for (const size_t size : sizes) {
    SCOPED_TRACE(absl::StrCat("size=", size));
    const std::string s(size, 'a');
    const ArenaString a(s, &arena);
    EXPECT_EQ(a.str(), s);
    EXPECT_EQ(a.size(), size);
    EXPECT_EQ(a.empty(), s.empty());
  }
}

// Traits so we can run with BaseArena, its derived classes, and C++
// allocators.
struct UnsafeArenaTraits {
  using AllocatorType = UnsafeArena*;
  UnsafeArena arena{1 << 20};
  AllocatorType GetAllocator() { return &arena; }
};

struct SafeArenaTraits {
  using AllocatorType = SafeArena*;
  SafeArena arena{1 << 20};
  AllocatorType GetAllocator() { return &arena; }
};

struct BaseArenaTraits {
  using AllocatorType = BaseArena*;
  UnsafeArena arena{1 << 20};
  AllocatorType GetAllocator() { return &arena; }
};

struct ArenaTypeNameGenerator {
  template <typename T>
  static std::string GetName(int) {
    if constexpr (std::is_same_v<T, UnsafeArenaTraits>) {
      return "UnsafeArena";
    } else if constexpr (std::is_same_v<T, SafeArenaTraits>) {
      return "SafeArena";
    } else if constexpr (std::is_same_v<T, BaseArenaTraits>) {
      return "BaseArena";
    }
    return "Unknown";
  }
};

using ArenaTypes =
    ::testing::Types<UnsafeArenaTraits, SafeArenaTraits, BaseArenaTraits>;

template <typename T>
class ArenaStringTypedTest : public ::testing::Test {
 protected:
  typename T::AllocatorType GetAllocator() { return traits_.GetAllocator(); }

 private:
  T traits_;
};

TYPED_TEST_SUITE(ArenaStringTypedTest, ArenaTypes, ArenaTypeNameGenerator);

TYPED_TEST(ArenaStringTypedTest, AssignAndConstruct) {
  auto allocator = this->GetAllocator();
  MTRandom rng(GTEST_FLAG_GET(random_seed));
  std::vector<std::string> data;
  std::vector<ArenaString> arena_str;

  const int32_t test_size = absl::GetFlag(FLAGS_test_size);
  const int32_t log_max_length = absl::GetFlag(FLAGS_log_max_length);
  data.reserve(test_size);
  arena_str.reserve(test_size);

  for (int i = 0; i < test_size; ++i) {
    // using a skewed distribution over string lengths focuses the test on short
    // strings, including length 0.
    data.push_back(std::string(
        util_random::SkewedLow<int32_t>(rng, 0, (1 << log_max_length) - 1),
        'a' + rng.Uniform(26)));
    const auto& str = data.back();

    VLOG(1) << "data[" << i << "] size=" << str.size() << ": "
            << str.substr(0, 3) << "...";

    // test both assign() and constructor.
    if (absl::Bernoulli(rng, 1.0 / 2)) {
      arena_str.resize(i + 1);
      arena_str[i].assign(str, allocator);
    } else {
      arena_str.push_back(ArenaString(str, allocator));
    }
  }

  for (int i = 0; i < test_size; ++i) {
    SCOPED_TRACE(absl::StrCat("i=", i, ", size=", data[i].size()));

    EXPECT_EQ(arena_str[i].size(), data[i].size());
    EXPECT_EQ(arena_str[i].empty(), data[i].empty());
    EXPECT_EQ(arena_str[i].str(), data[i]);
    if (!data[i].empty()) {
      EXPECT_EQ(
          std::memcmp(arena_str[i].data(), data[i].data(), data[i].size()), 0);
    }
  }
}

TYPED_TEST(ArenaStringTypedTest, Clear) {
  auto allocator = this->GetAllocator();

  // Test clearing a non-empty string.
  ArenaString str("hello world", allocator);
  EXPECT_FALSE(str.empty());
  EXPECT_EQ(str.size(), 11);
  EXPECT_EQ(str.str(), "hello world");

  str.clear();
  EXPECT_THAT(str.str(), IsEmpty());
  EXPECT_TRUE(str.empty());
  EXPECT_EQ(str.size(), 0);

  // Test clearing an empty string.
  ArenaString empty_str("", allocator);
  EXPECT_TRUE(empty_str.empty());
  EXPECT_EQ(empty_str.size(), 0);

  empty_str.clear();
  EXPECT_THAT(empty_str.str(), IsEmpty());
  EXPECT_TRUE(empty_str.empty());
  EXPECT_EQ(empty_str.size(), 0);

  // Test clearing randomized strings of varying lengths.
  MTRandom rng(GTEST_FLAG_GET(random_seed));
  const int32_t test_size = absl::GetFlag(FLAGS_test_size);
  const int32_t log_max_length = absl::GetFlag(FLAGS_log_max_length);
  for (int i = 0; i < test_size; ++i) {
    SCOPED_TRACE(absl::StrCat("i=", i));
    const std::string data(
        util_random::SkewedLow<int32_t>(rng, 0, (1 << log_max_length) - 1),
        'a' + rng.Uniform(26));
    ArenaString arena_str(data, allocator);
    arena_str.clear();
    EXPECT_THAT(arena_str.str(), IsEmpty());
    EXPECT_TRUE(arena_str.empty());
    EXPECT_EQ(arena_str.size(), 0);
  }
}

// Test static encode and decode methods.
TEST(ArenaStringTest, EncodeDecode) {
  MTRandom rng(GTEST_FLAG_GET(random_seed));

  // allocate a few extra characters so we can check for buffer overruns.
  constexpr int kBufExtra = 16;
  const int buf_size =
      ArenaString::EncSize(1 << absl::GetFlag(FLAGS_log_max_length)) +
      kBufExtra;
  absl::FixedArray<char, 0> buf(buf_size);
  memset(buf.data(), 0, buf.size());

  const int32_t test_size = absl::GetFlag(FLAGS_test_size);
  const int32_t log_max_length = absl::GetFlag(FLAGS_log_max_length);
  for (int i = 0; i < test_size; ++i) {
    SCOPED_TRACE(absl::StrCat("iteration=", i));
    // using a skewed distribution over string lengths focuses the test on short
    // strings, including length 0.
    std::string raw = rng.RandString(
        util_random::SkewedLow<int32_t>(rng, 0, (1 << log_max_length) - 1));
    absl::string_view str(raw);

    // encode the string; it should return data.
    char* enc = ArenaString::Encode(str, buf.data());
    if (raw.size() < 128) {
      EXPECT_EQ(enc, &buf[1]);
    } else {
      EXPECT_EQ(enc, &buf[4]);
    }

    // ensure we didn't write past the end of the encoding
    for (int j = ArenaString::EncSize(str.size()); j < kBufExtra; ++j) {
      EXPECT_EQ(buf[j], 0) << "size=" << str.size() << " j=" << j;
    }

    // decode and verify the string.
    absl::string_view dec = ArenaString::Decode(enc);
    EXPECT_EQ(dec, str);

    // clear buf
    memset(buf.data(), 0, ArenaString::EncSize(str.size()));
  }
}

TEST(ArenaStringTest, Empty) {
  UnsafeArena arena(1 << 10);
  EXPECT_TRUE(ArenaString().empty());
  EXPECT_THAT(ArenaString().str(), IsEmpty());
  EXPECT_EQ(ArenaString().size(), 0);
  EXPECT_EQ(ArenaString().data(), nullptr);

  const ArenaString empty_arena_str("", &arena);
  EXPECT_TRUE(empty_arena_str.empty());
  EXPECT_THAT(empty_arena_str.str(), IsEmpty());
  EXPECT_EQ(empty_arena_str.size(), 0);
  EXPECT_EQ(empty_arena_str.data(), nullptr);

  const ArenaString non_empty_arena_str("a", &arena);
  EXPECT_FALSE(non_empty_arena_str.empty());
  EXPECT_EQ(non_empty_arena_str.size(), 1);
  EXPECT_EQ(non_empty_arena_str.str(), "a");
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
  CHECK_EQ(std::memcmp(a.data(), x.data(), len), 0);
  CHECK_EQ(a.size(), len);
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
  CHECK_EQ(std::memcmp(a.data(), x.data(), len), 0);
  CHECK_EQ(a.size(), len);
}
BENCHMARK(BM_copy_stringpiece)->Range(0, 4 << 10);

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
    DoNotOptimize(ArenaString::Decode(buf.data()));
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
  CHECK_EQ(st.size(), x.size());
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
  CHECK_EQ(size, len);
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
  CHECK_NE(d, x.data());
}
BENCHMARK(BM_arenastring_data)->Range(0, 1 << 16);

}  // namespace
}  // namespace strings

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  LOG(INFO) << "--test_random_seed=" << GTEST_FLAG_GET(random_seed);

  return RUN_ALL_TESTS();
}
