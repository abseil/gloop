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

#include "gloop/strings/ostringstream.h"

#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using ::benchmark::DoNotOptimize;

TEST(OStringStream, TypeCompliance) {
  // OStringStream must be a subclass of std::ostream
  static_assert(std::is_base_of<std::ostream, OStringStream>(), "");

  // Its API should match std::ostringstream wherever possible.  Since
  // ostringstream makes these 6 types publicly available, OStringStream
  // does, too.
  using std::ostringstream;
  static_assert(std::is_same<OStringStream::allocator_type,
                             ostringstream::allocator_type>::value,
                "");
  static_assert(
      std::is_same<OStringStream::char_type, ostringstream::char_type>::value,
      "");
  static_assert(std::is_same<OStringStream::traits_type,
                             ostringstream::traits_type>::value,
                "");
  static_assert(
      std::is_same<OStringStream::int_type, ostringstream::int_type>::value,
      "");
  static_assert(
      std::is_same<OStringStream::pos_type, ostringstream::pos_type>::value,
      "");
  static_assert(
      std::is_same<OStringStream::off_type, ostringstream::off_type>::value,
      "");
}

TEST(OStringStream, ConstructDestroy) {
  {
    OStringStream strm(nullptr);
    EXPECT_EQ(nullptr, strm.str());
  }
  {
    std::string s = "abc";
    {
      OStringStream strm(&s);
      EXPECT_EQ(&s, strm.str());
    }
    EXPECT_EQ("abc", s);
  }
  {
    std::unique_ptr<std::string> s = std::make_unique<std::string>();
    OStringStream strm(s.get());
    s.reset();
  }
}

TEST(OStringStream, Str) {
  std::string s1;
  OStringStream strm(&s1);
  const OStringStream& c_strm(strm);

  static_assert(std::is_same<decltype(strm.str()), std::string*>(), "");
  static_assert(std::is_same<decltype(c_strm.str()), const std::string*>(), "");

  EXPECT_EQ(&s1, strm.str());
  EXPECT_EQ(&s1, c_strm.str());

  strm.str(&s1);
  EXPECT_EQ(&s1, strm.str());
  EXPECT_EQ(&s1, c_strm.str());

  std::string s2;
  strm.str(&s2);
  EXPECT_EQ(&s2, strm.str());
  EXPECT_EQ(&s2, c_strm.str());

  strm.str(nullptr);
  EXPECT_EQ(nullptr, strm.str());
  EXPECT_EQ(nullptr, c_strm.str());
}

TEST(OStringStream, WriteToLValue) {
  std::string s = "abc";
  {
    OStringStream strm(&s);
    EXPECT_EQ("abc", s);
    strm << "";
    EXPECT_EQ("abc", s);
    strm << 42;
    EXPECT_EQ("abc42", s);
    strm << 'x' << 'y';
    EXPECT_EQ("abc42xy", s);
  }
  EXPECT_EQ("abc42xy", s);
}

TEST(OStringStream, WriteToRValue) {
  std::string s = "abc";
  OStringStream(&s) << "";
  EXPECT_EQ("abc", s);
  OStringStream(&s) << 42;
  EXPECT_EQ("abc42", s);
  OStringStream(&s) << 'x' << 'y';
  EXPECT_EQ("abc42xy", s);
}

TEST(OStringStream, Imbue) {
  // Just verify that it compiles.
  std::string s;
  OStringStream strm(&s);
  strm.imbue(strm.getloc());
}

// Benchmarks for std::ostringstream.
void BM_StdStreamWithExtract(benchmark::State& state) {
  const int num_writes = state.range(0);
  const int bytes_per_write = state.range(1);
  const std::string payload(bytes_per_write, 'x');
  for (auto _ : state) {
    std::ostringstream strm;
    DoNotOptimize(strm);
    for (int i = 0; i != num_writes; ++i) strm << payload;
    std::string s = std::move(strm).str();
    DoNotOptimize(s);
  }
}

void BM_StdStreamWithoutExtract(benchmark::State& state) {
  const int num_writes = state.range(0);
  const int bytes_per_write = state.range(1);
  const std::string payload(bytes_per_write, 'x');
  for (auto _ : state) {
    std::ostringstream strm;
    DoNotOptimize(strm);
    for (int i = 0; i != num_writes; ++i) strm << payload;
    DoNotOptimize(strm);
  }
}

// Benchmarks for OStringStream.
void BM_OStringStream(benchmark::State& state) {
  const int num_writes = state.range(0);
  const int bytes_per_write = state.range(1);
  const std::string payload(bytes_per_write, 'x');
  for (auto _ : state) {
    std::string out;
    OStringStream strm(&out);
    DoNotOptimize(strm);
    for (int i = 0; i != num_writes; ++i) {
      strm << payload;
    }
    DoNotOptimize(out);
  }
}

void Configure(benchmark::Benchmark* mark) {
  mark->ArgPair(0, 0)
      ->ArgPair(1, 16)   // 16 bytes is small enough for SSO
      ->ArgPair(1, 256)  // 256 bytes requires heap allocation
      ->ArgPair(1024, 256);
}

BENCHMARK(BM_StdStreamWithExtract)->Apply(Configure);
BENCHMARK(BM_StdStreamWithoutExtract)->Apply(Configure);
BENCHMARK(BM_OStringStream)->Apply(Configure);

// Run on ikuf12 (256 X 2250 MHz CPUs); 2020-09-16T19:14:04.62787453-07:00
// CPU: AMD Rome (128 cores) dL1:32KB dL2:512KB dL3:256MB
// Benchmark                           Time(ns)
// --------------------------------------------
// BM_StdStreamWithExtract/0/0               52
// BM_StdStreamWithExtract/1/16             154
// BM_StdStreamWithExtract/1/256            459
// BM_StdStreamWithExtract/1k/256         82381
// BM_StdStreamWithoutExtract/0/0            43
// BM_StdStreamWithoutExtract/1/16          116
// BM_StdStreamWithoutExtract/1/256         389
// BM_StdStreamWithoutExtract/1k/256      78001
// BM_OStringStream/0/0                      46
// BM_OStringStream/1/16                     91
// BM_OStringStream/1/256                   120
// BM_OStringStream/1k/256                56076
// --------------------------------------------
//
// By subtracting numbers from different benchmarks we can compute the
// performance of individual operations. For example, the difference between
// StdStreamWithExtract/1k/256 and StdStreamWithoutExtract/1k/256 tells us how
// long it takes to get a 256-kilobytes-long string out of a std::ostringstream.
//
// Operation                   std::ostringstream    OStringStream
// ---------------------------------------------------------------
// Create and destroy                          43               46
// Write 1 x 16 bytes                          73               45
// Write 1 x 256 bytes                        346               74
// Write 1024 x 256 bytes                   77958            56076
// Get std::string (16 bytes)                  38             free
// Get std::string (256 bytes)                 70             free
// Get std::string (256 kilobytes)           4380             free
// ---------------------------------------------------------------
//
// Summary: compared to std::ostringstream, creating and destroying
// OStringStream is about the same; writing is ~39% faster, and getting
// the string out is infinitely faster (free).

}  // namespace
}  // namespace strings
