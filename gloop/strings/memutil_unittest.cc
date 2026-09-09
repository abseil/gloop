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

// Unit test for memutil.cc

#include "gloop/strings/memutil.h"

#include <stdlib.h>

#include <algorithm>
#include <cstdint>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "benchmark/benchmark.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

using testing::AllOf;
using testing::Ge;
using testing::Le;

// We fill the haystack with aaaaaaaaaaaaaaaaaa...aaaab.
// That gives us:
// - an easy search: 'b'
// - a medium search: 'ab'.  That means every letter is a possible match.
// - a pathological search: 'aaaaaa.......aaaaab' (half as many a's as haystack)
// We benchmark case-sensitive and case-sensitive versions of
// three memmem implementations:
// - memmem() from memutil.h
// - search() from STL
// - memmatch(), a custom implementation using memchr and memcmp.
// Here are sample results:
//
// Run on strel.cam (12 X 3501 MHz CPUs); 2017-03-22T15:55:34.504697517-04:00
// CPU: Intel Haswell with HyperThreading (6 cores) dL1:32KB dL2:256KB dL3:15MB
// Benchmark                   Time(ns)       CPU(ns)   Iterations
// ---------------------------------------------------------------
// BM_Memmem                       5512          5507       100000  1691.0  MB/s
// BM_MemmemMedium                 9434          9427        73628  1011.657MB/s
// BM_MemmemPathological       10231266      10221809           68     0.955MB/s
// BM_Memcasemem                   6375          6370       100000  1462.0  MB/s
// BM_MemcasememMedium            13454         13441        51783   709.539MB/s
// BM_MemcasememPathological   16113193      16098234           43     0.607MB/s
// BM_Search                       1813          1812       388904  5141.0  MB/s
// BM_SearchMedium                28481         28453        24502   335.176MB/s
// BM_SearchPathological       10107329      10098296           69   967.057MB/s
// BM_Searchcase                  16744         16730        41686   570.048MB/s
// BM_SearchcaseMedium            55100         55049        10000   173.240MB/s
// BM_SearchcasePathological   40202418      40168618           17     0.243MB/s
// BM_Memmatch                      181           180      3862008 51625.0  MB/s
// BM_MemmatchMedium              56108         56058        10000   170.123MB/s
// BM_MemmatchPathological       650911        650355         1000    14.664MB/s
// BM_Memcasematch                 4275          4271       164000  2181.0  MB/s
// BM_MemcasematchMedium          34806         34775        20148   274.241MB/s
// BM_MemcasematchPathological 15002196      14989356           47   651.504MB/s
// BM_MemmemStartup                  10.5          10.4   67250049
// BM_SearchStartup                   6.48          6.47 100000000
// BM_MemmatchStartup                 7.50          7.49  93238033
//
// Conclusions:
//
// If you need case-insensitive, memmem is always the best, except for
// the pathological case.
//
// Case-sensitive is more subtle, and all three implementations show promise:
// Custom memmatch is _very_ fast at scanning, so if you have
// very few possible matches in your haystack, that's the way to go.
//
// memcasemem is very fast in the medium benchmark, since it doesn't
// have to reinvoke memchr on every possible match.
//
// STL search is sort of in-between, and it might be faster for certain
// things; it calls STL find() on every possible match, but STL find()
// has lower startup cost than memchr().

static const int kHaystackSize = 10000;
static const int64_t kHaystackSize64 = kHaystackSize;
static char* haystack;

static void BM_Memmem(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmem(haystack, kHaystackSize, "b", 1));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Memmem);

static void BM_MemmemMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmem(haystack, kHaystackSize, "ab", 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemmemMedium);

static void BM_MemmemPathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmem(haystack, kHaystackSize,
                                    haystack + kHaystackSize / 2,
                                    kHaystackSize - kHaystackSize / 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemmemPathological);

static void BM_Memcasemem(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasemem(haystack, kHaystackSize, "b", 1));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Memcasemem);

static void BM_MemcasememMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasemem(haystack, kHaystackSize, "ab", 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemcasememMedium);

static void BM_MemcasememPathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasemem(haystack, kHaystackSize,
                                        haystack + kHaystackSize / 2,
                                        kHaystackSize - kHaystackSize / 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemcasememPathological);

bool case_eq(const char a, const char b) {
  return absl::ascii_tolower(a) == absl::ascii_tolower(b);
}

void BM_Search(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(haystack, haystack + kHaystackSize,
                                         haystack + kHaystackSize - 1,
                                         haystack + kHaystackSize));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Search);

void BM_SearchMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(haystack, haystack + kHaystackSize,
                                         haystack + kHaystackSize - 2,
                                         haystack + kHaystackSize));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_SearchMedium);

void BM_SearchPathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(haystack, haystack + kHaystackSize,
                                         haystack + kHaystackSize / 2,
                                         haystack + kHaystackSize));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_SearchPathological);

void BM_Searchcase(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(haystack, haystack + kHaystackSize,
                                         haystack + kHaystackSize - 1,
                                         haystack + kHaystackSize, case_eq));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Searchcase);

void BM_SearchcaseMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(haystack, haystack + kHaystackSize,
                                         haystack + kHaystackSize - 2,
                                         haystack + kHaystackSize, case_eq));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_SearchcaseMedium);

void BM_SearchcasePathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(haystack, haystack + kHaystackSize,
                                         haystack + kHaystackSize / 2,
                                         haystack + kHaystackSize, case_eq));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_SearchcasePathological);

static char* memcasechr(const char* s, int c, size_t slen) {
  c = absl::ascii_tolower(c);
  for (; slen; ++s, --slen) {
    if (absl::ascii_tolower(*s) == c) return const_cast<char*>(s);
  }
  return nullptr;
}

static const char* memcasematch(const char* phaystack, size_t haylen,
                                const char* pneedle, size_t neelen) {
  if (0 == neelen) {
    return phaystack;  // even if haylen is 0
  }
  if (haylen < neelen) return nullptr;

  const char* match;
  const char* hayend = phaystack + haylen - neelen + 1;
  while ((match = static_cast<char*>(
              memcasechr(phaystack, pneedle[0], hayend - phaystack)))) {
    if (memcasecmp(match, pneedle, neelen) == 0)
      return match;
    else
      phaystack = match + 1;
  }
  return nullptr;
}

void BM_Memmatch(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmatch(haystack, kHaystackSize, "b", 1));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Memmatch);

void BM_MemmatchMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmatch(haystack, kHaystackSize, "ab", 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemmatchMedium);

void BM_MemmatchPathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmatch(haystack, kHaystackSize,
                                      haystack + kHaystackSize / 2,
                                      kHaystackSize - kHaystackSize / 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemmatchPathological);

void BM_Memcasematch(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasematch(haystack, kHaystackSize, "b", 1));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Memcasematch);

void BM_MemcasematchMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasematch(haystack, kHaystackSize, "ab", 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemcasematchMedium);

void BM_MemcasematchPathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasematch(haystack, kHaystackSize,
                                          haystack + kHaystackSize / 2,
                                          kHaystackSize - kHaystackSize / 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemcasematchPathological);

void BM_MemmemStartup(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmem(haystack + kHaystackSize - 10, 10,
                                    haystack + kHaystackSize - 1, 1));
  }
}
BENCHMARK(BM_MemmemStartup);

void BM_SearchStartup(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        std::search(haystack + kHaystackSize - 10, haystack + kHaystackSize,
                    haystack + kHaystackSize - 1, haystack + kHaystackSize));
  }
}
BENCHMARK(BM_SearchStartup);

void BM_MemmatchStartup(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memmatch(haystack + kHaystackSize - 10, 10,
                                      haystack + kHaystackSize - 1, 1));
  }
}
BENCHMARK(BM_MemmatchStartup);

TEST(MemUtilTest, AllTests) {
  // check memutil functions
  const char a[1000] = "hello there";

  CHECK_EQ(memcasecmp(a, "h", 1), 0);                         // 'h' == 'h'
  EXPECT_THAT(memcasecmp(a, "g", 1), AllOf(Ge(1), Le(2)));    // 'h' > 'g'
  EXPECT_THAT(memcasecmp(a, "i", 1), AllOf(Ge(-2), Le(-1)));  // 'h' < 'i'
  CHECK_EQ(memcasecmp(a, "heLLO there", sizeof("hello there") - 1), 0);
  EXPECT_THAT(memcasecmp(a, "heLLO therf", sizeof("hello there") - 1),
              AllOf(Ge(-12), Le(-11)));
  EXPECT_THAT(memcasecmp(a, "heLLO therd", sizeof("hello there") - 1),
              AllOf(Ge(11), Le(12)));
  CHECK_EQ(memcasecmp(a, "heLLO therf", sizeof("hello there") - 2), 0);
  CHECK_EQ(memcasecmp(a, "whatever", 0), 0);

  char* p = memdup("hello", 5);
  free(p);

  p = memrchr("hello there", 'e', sizeof("hello there") - 1);
  CHECK(p && p[-1] == 'r');
  p = memrchr("hello there", 'e', sizeof("hello there") - 2);
  CHECK(p && p[-1] == 'h');
  p = memrchr("hello there", 'u', sizeof("hello there") - 1);
  CHECK(p == nullptr);

  size_t len = memspn("hello there", sizeof("hello there") - 1, "hole");
  CHECK_EQ(len, sizeof("hello") - 1);
  len = memspn("hello there", sizeof("hello there") - 1, "u");
  CHECK_EQ(len, 0);
  len = memspn("hello there", sizeof("hello there") - 1, "");
  CHECK_EQ(len, 0);
  len = memspn("hello there", sizeof("hello there") - 1, "trole h");
  CHECK_EQ(len, sizeof("hello there") - 1);
  len = memspn("hello there!", sizeof("hello there!") - 1, "trole h");
  CHECK_EQ(len, sizeof("hello there") - 1);
  len = memspn("hello there!", sizeof("hello there!") - 2, "trole h!");
  CHECK_EQ(len, sizeof("hello there!") - 2);
  const char ch = 'h';
  len = memspn(&ch, 1, "h");
  CHECK_EQ(len, 1);
  len = memspn(nullptr, 0, "h");
  CHECK_EQ(len, 0);

  len = memcspn("hello there", sizeof("hello there") - 1, "leho");
  CHECK_EQ(len, 0);
  len = memcspn("hello there", sizeof("hello there") - 1, "u");
  CHECK_EQ(len, sizeof("hello there") - 1);
  len = memcspn("hello there", sizeof("hello there") - 1, "");
  CHECK_EQ(len, sizeof("hello there") - 1);
  len = memcspn("hello there", sizeof("hello there") - 1, " ");
  CHECK_EQ(len, 5);

  p = mempbrk("hello there", sizeof("hello there") - 1, "leho");
  CHECK(p && p[1] == 'e' && p[2] == 'l');
  p = mempbrk("hello there", sizeof("hello there") - 1, "nu");
  CHECK(p == nullptr);
  p = mempbrk("hello there!", sizeof("hello there!") - 2, "!");
  CHECK(p == nullptr);
  p = mempbrk("hello there", sizeof("hello there") - 1, " t ");
  CHECK(p && p[-1] == 'o' && p[1] == 't');

  CHECK(memprefix("hello there", sizeof("hello there") - 1, "hello"));
  CHECK(memprefix("hello there", sizeof("hello there") - 1, "hello there"));
  CHECK(!memprefix("hello there", sizeof("hello there") - 1, "hllo there"));
  CHECK(!memprefix("hello there", sizeof("hello there") - 1, "hello there!"));
  CHECK(!memprefix("hello there!", sizeof("hello there!") - 2, "hello there!"));
  CHECK(memcaseprefix("hello there", sizeof("hello there") - 1, "heLlo"));
  CHECK(memcaseprefix("hello there", sizeof("hello there") - 1, "heLLO there"));

  CHECK(memsuffix("hello there", sizeof("hello there") - 1, "there"));
  CHECK(memsuffix("hello there", sizeof("hello there") - 1, "hello there"));
  CHECK(!memsuffix("hello there", sizeof("hello there") - 1, "hello thee"));
  CHECK(!memsuffix("hello there", sizeof("hello there") - 1, "*hello there"));
  CHECK(!memsuffix("hello there!", sizeof("hello there!") - 2, "hello there!"));

  CHECK(memcasesuffix("hello there", sizeof("hello there") - 1, "there"));
  CHECK(memcasesuffix("hello there", sizeof("hello there") - 1, "hello there"));
  CHECK(!memcasesuffix("hello there", sizeof("hello there") - 1, "hello thee"));
  CHECK(
      !memcasesuffix("hello there", sizeof("hello there") - 1, "*hello there"));
  CHECK(!memcasesuffix("hello there!", sizeof("hello there!") - 2,
                       "hello there!"));

  CHECK(memcasesuffix("hello there", sizeof("hello there") - 1, "thEre"));
  CHECK(memcasesuffix("hello there", sizeof("hello there") - 1, "heLLO there"));

  CHECK(memis("hello there", sizeof("hello there") - 1, "hello there"));
  CHECK(!memis("hello there", sizeof("hello there") - 1, "Hello There"));
  CHECK(memcaseis("hello there", sizeof("hello there") - 1, "Hello There"));
  CHECK(!memis("hello ther", sizeof("hello ther") - 1, "hello there"));
  CHECK(!memis("hello there!", sizeof("hello there!") - 1, "hello there"));
  CHECK(!memis("hello there", sizeof("hello there"), "hello there"));

  {
    const char kHaystack[] = "0123456789";
    CHECK_EQ(memmem(kHaystack, 0, "", 0), kHaystack);
    CHECK_EQ(memmem(kHaystack, 10, "012", 3), kHaystack);
    CHECK_EQ(memmem(kHaystack, 10, "0xx", 1), kHaystack);
    CHECK_EQ(memmem(kHaystack, 10, "789", 3), kHaystack + 7);
    CHECK_EQ(memmem(kHaystack, 10, "9xx", 1), kHaystack + 9);
    CHECK(memmem(kHaystack, 10, "9xx", 3) == nullptr);
    CHECK(memmem(kHaystack, 10, "xxx", 1) == nullptr);
  }
  {
    const char kHaystack[] = "aBcDeFgHiJ";
    CHECK_EQ(memcasemem(kHaystack, 0, "", 0), kHaystack);
    CHECK_EQ(memcasemem(kHaystack, 10, "Abc", 3), kHaystack);
    CHECK_EQ(memcasemem(kHaystack, 10, "Axx", 1), kHaystack);
    CHECK_EQ(memcasemem(kHaystack, 10, "hIj", 3), kHaystack + 7);
    CHECK_EQ(memcasemem(kHaystack, 10, "jxx", 1), kHaystack + 9);
    CHECK(memcasemem(kHaystack, 10, "jxx", 3) == nullptr);
    CHECK(memcasemem(kHaystack, 10, "xxx", 1) == nullptr);
  }
  {
    const char kHaystack[] = "0123456789";
    CHECK_EQ(memmatch(kHaystack, 0, "", 0), kHaystack);
    CHECK_EQ(memmatch(kHaystack, 10, "012", 3), kHaystack);
    CHECK_EQ(memmatch(kHaystack, 10, "0xx", 1), kHaystack);
    CHECK_EQ(memmatch(kHaystack, 10, "789", 3), kHaystack + 7);
    CHECK_EQ(memmatch(kHaystack, 10, "9xx", 1), kHaystack + 9);
    CHECK(memmatch(kHaystack, 10, "9xx", 3) == nullptr);
    CHECK(memmatch(kHaystack, 10, "xxx", 1) == nullptr);
  }
}

}  // namespace strings

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);

  using strings::haystack;
  using strings::kHaystackSize;
  haystack = new char[kHaystackSize];
  for (int i = 0; i < kHaystackSize - 1; ++i) haystack[i] = 'a';
  haystack[kHaystackSize - 1] = 'b';

  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  return RUN_ALL_TESTS();
}
