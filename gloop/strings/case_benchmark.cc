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

// This file tests string processing functions related to case:
// uppercase, lowercase, etc.

#include <string>

#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gloop/strings/case.h"

static void BM_ToLower(benchmark::State& state) {
  const int size = state.range(0);
  std::string s(size, 'X');
  std::string r;
  for (auto _ : state) {
    r = absl::AsciiStrToLower(s);
  }
  CHECK(!r.empty());
}
BENCHMARK(BM_ToLower)->Range(1, 1 << 20);

static void BM_AsciiCaseInsensitiveCompare(benchmark::State& state) {
  const int size = state.range(0);
  std::string l = absl::StrCat(std::string(size - 1, 'x'), "Y");
  std::string r = absl::StrCat(std::string(size - 1, 'x'), "y");
  int result;
  for (auto _ : state) {
    result = strings::AsciiCaseInsensitiveCompare(l, r);
  }
  CHECK_EQ(result, 0);
}
BENCHMARK(BM_AsciiCaseInsensitiveCompare)->Range(1, 1 << 20);

static void BM_PlainTextCompare(benchmark::State& state) {
  const int size = state.range(0);
  std::string l = absl::StrCat(std::string(size - 1, 'x'), "Y");
  std::string r = absl::StrCat(std::string(size - 1, 'x'), "y");
  bool result;
  for (auto _ : state) {
    result = l == r;
  }
  CHECK(!result);
}
BENCHMARK(BM_PlainTextCompare)->Range(1, 1 << 20);
