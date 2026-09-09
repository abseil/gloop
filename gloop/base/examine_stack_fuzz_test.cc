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

#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/base/examine_stack.h"
#include "gtest/gtest.h"

namespace base {
namespace {

void MockWriter(const char* data, void* /*arg*/) {
  // Make sure `data` is a valid C string, but ignore otherwise.
  benchmark::DoNotOptimize(strlen(data));
}

bool ShouldMatch(const absl::string_view filename) {
  for (const std::string p : {"-fastbuild", "-opt", "-dbg"}) {
    for (const std::string q : {"", "-asan", "-msan", "-tsan"}) {
      std::string suffix = absl::StrCat(p, q, "/");
      if (absl::StrContains(filename, suffix)) return true;
    }
  }
  return false;
}

void FuzzCollapseBuildPrefixFields(std::string filename, size_t dir_size,
                                   size_t out_buf_size) {
  // We only care about filenames longer than $build/.
  // Something like "-dbg/" can not happen in practice.
  constexpr absl::string_view kBuild = "$build/";
  if (filename.size() < kBuild.size()) return;

  // Limit sizes to avoid OOMs and slow tests
  if (filename.size() > 1000) return;
  if (dir_size == 0 || dir_size > 1000) return;
  if (out_buf_size == 0 || out_buf_size > 1000) return;

  std::string original_filename = filename;

  auto dir_buf = std::make_unique<char[]>(dir_size);
  dir_buf[0] = '\0';

  auto out_buf = std::make_unique<char[]>(out_buf_size);
  out_buf[0] = '\0';

  CollapseBuildPrefix(filename.data(), dir_buf.get(), dir_size, out_buf.get(),
                      out_buf_size, MockWriter, nullptr);

  if (ShouldMatch(original_filename)) {
    ASSERT_TRUE(absl::StartsWith(filename, kBuild))
        << "Original: " << original_filename << "\nResult: " << filename;
  }
}

FUZZ_TEST(ExamineStackFuzz, FuzzCollapseBuildPrefixFields)
    .WithDomains(fuzztest::String(), fuzztest::InRange<size_t>(1, 1000),
                 fuzztest::InRange<size_t>(1, 1000))
    .WithSeeds({{"path/to/bin/k8-dbg/filename", 100, 100},
                {"another/path/arm-opt-asan/filename", 100, 100},
                {"/abs/path/haswell-fastbuild/filename", 100, 100}});

}  // namespace
}  // namespace base
