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

#include "gloop/thread/fiber/fiber-options.h"

#include <cstddef>

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"
#include "gtest/gtest.h"

namespace {

void validate_fiber_name(thread::FiberOptions opts) {
  // Note: const char* pointers are compared below.
  EXPECT_EQ(opts.name(), thread::Fiber::Current()->options().name());
}

TEST(FiberOptionsTest, Basic) {
  using thread::FiberOptions;

  FiberOptions optsFoo;
  FiberOptions optsBar;
  FiberOptions optsNil;

  optsFoo.SetInternedName("foo");
  optsBar.SetInternedName("bar");

  auto root_fiber = [=](FiberOptions root_opts) {
    thread::NewTree(thread::TreeOptions().set_fiber_options(root_opts), [=]() {
      validate_fiber_name(root_opts);

      auto child_fiber = [](FiberOptions child_opts) {
        thread::Fiber fiber(child_opts,
                            [=]() { validate_fiber_name(child_opts); });
        fiber.Join();
      };

      child_fiber(optsFoo);
      child_fiber(optsBar);
      child_fiber(optsNil);

      // Check that the name is inherited if options are not explicitly set.
      thread::Fiber fiber([root_opts]() { validate_fiber_name(root_opts); });
      fiber.Join();
    })->Join();
  };

  root_fiber(optsFoo);
  root_fiber(optsBar);
  root_fiber(optsNil);
}

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
