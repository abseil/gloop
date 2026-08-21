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

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/strings/split.h"

namespace strings {
namespace {

bool ParseToString(absl::string_view in, std::string* out) {
  *out = in;
  return true;
}

void BM_SplitStringAndParseToList_String(benchmark::State& state) {
  std::string source("The quick brown fox jumped over the lazy dog.");
  std::vector<std::string> output;
  for (auto _ : state) {
    output.clear();
    SplitStringAndParseToList(source, " ", &ParseToString, &output);
  }
}
BENCHMARK(BM_SplitStringAndParseToList_String);

void BM_SplitStringAndParseToList_SizedString(benchmark::State& state) {
  const int len = state.range(0);
  std::string source("a");
  while (source.size() < len) {
    source.push_back(' ');
    source += std::string(source.size(), 'a');
  }
  source.resize(len);
  std::vector<std::string> output;
  for (auto _ : state) {
    output.clear();
    SplitStringAndParseToList(source, " ", &ParseToString, &output);
  }
}
BENCHMARK(BM_SplitStringAndParseToList_SizedString)->Range(8, 256);

}  // namespace
}  // namespace strings
