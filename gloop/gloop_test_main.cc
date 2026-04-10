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

#include <cstdio>
#include <cstdlib>

#include "benchmark/benchmark.h"
#include "gloop/base/init_google.h"
#include "gtest/gtest.h"

GTEST_API_ int main(int argc, char** argv) {
  printf("Running main() from %s\n", __FILE__);
  InitGoogleExceptChangeRootAndUser(/*usage=*/nullptr, &argc, &argv,
                                    /*remove_flags=*/true);
  testing::InitGoogleTest(&argc, argv);

  // TODO Use benchmark::RunSpecifiedBenchmarksThenExit() once
  // the migration is complete.
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  return RUN_ALL_TESTS();
}
