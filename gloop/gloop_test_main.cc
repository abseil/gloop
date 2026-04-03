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

#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>

#include "absl/debugging/internal/symbolize.h"
#include "absl/flags/flag.h"
#include "benchmark/benchmark.h"
#include "gloop/base/init_google.h"
#include "gloop/base/process_state.h"
#include "gtest/gtest.h"

GTEST_API_ int main(int argc, char** argv) {
  printf("Running main() from %s\n", __FILE__);

  // Disables core allocations for sandbox natively
  struct rlimit limit;
  limit.rlim_cur = 0;
  limit.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &limit);

  // Emulate Google3DeathTestChildSetup.  We should probably inject this into
  // child processes in death tests though.
  absl::debugging_internal::SetSymbolDecoratorFactory(nullptr);
  absl::SetFlag(&FLAGS_suppress_failure_output, true);

  testing::InitGoogleTest(&argc, argv);
  InitGoogleExceptChangeRootAndUser(/*usage=*/nullptr, &argc, &argv,
                                    /*remove_flags=*/true);

  // TODO Use benchmark::RunSpecifiedBenchmarksThenExit() once
  // the migration is complete.
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  return RUN_ALL_TESTS();
}
