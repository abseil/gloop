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

#include "gloop/thread/fiber/fiber-options.h"

#include <cstddef>

#include "absl/strings/str_cat.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"

namespace {

TEST(FiberOptionsTest, StackSize) {
  thread::FiberOptions opts;

  // We should default to zero, which is used to indicate that the default
  // stack size should be used.
  EXPECT_EQ(opts.GetStackSize(), 0);

  // Setting a stack size smaller than the minimum supported should cause us to
  // silently round up the request to the minimum.
  constexpr size_t kMinStackSize = 1 << thread::internal::kMinStackSizeLog2;
  opts.SetStackSize(kMinStackSize / 2);
  EXPECT_EQ(opts.GetStackSize(), kMinStackSize);

  // Requests for the minimum stack size or higher should persist.
  opts.SetStackSize(kMinStackSize);
  EXPECT_EQ(opts.GetStackSize(), kMinStackSize);
  opts.SetStackSize(kMinStackSize * 2);
  EXPECT_EQ(opts.GetStackSize(), kMinStackSize * 2);

  // Setting a zero stack size should restore us to the zeroed value indicating
  // again that the default stack size should be used.
  opts.SetStackSize(0);
  EXPECT_EQ(opts.GetStackSize(), 0);
}

void BM_SetInternedName(benchmark::State& state) {
  const int name_limit = state.range(0);
  int name_suffix = 0;

  thread::FiberOptions options;
  for (auto s : state) {
    options.SetInternedName(absl::StrCat("fiber", name_suffix));
    name_suffix = (name_suffix + 1) % name_limit;
  }
}

BENCHMARK(BM_SetInternedName)->Range(1, 1000)->ThreadRange(1, 64);

}  // namespace
