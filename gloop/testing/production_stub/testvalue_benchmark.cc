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

// Benchmarks for testvalue::Adjust() once testvalues have been enabled. This is
// the cost paid by every Adjust() call site in production code under test.

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/testing/production_stub/testvalue.h"

namespace testing {
namespace testvalue {
namespace {

constexpr absl::string_view kRegisteredLabel =
    "testvalue_benchmark::registered";
constexpr absl::string_view kUnregisteredLabel =
    "testvalue_benchmark::unregistered";

// Enable testvalues and register a single label before any benchmark runs, so
// that the map is non-empty, as it typically is in tests.
const bool kInitialized = [] {
  Enable();
  Force(kRegisteredLabel, 1);
  return true;
}();

// Adjust() on a label that has no adjuster. This is by far the most common
// case: production code is full of Adjust() calls that most tests never use.
void BM_AdjustUnregistered(::benchmark::State& state) {
  int value = 0;
  for (auto s : state) {
    Adjust(kUnregisteredLabel, &value);
    ::benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_AdjustUnregistered)->ThreadRange(1, 32)->UseRealTime();

// Adjust() on a label with a registered adjuster.
void BM_AdjustRegistered(::benchmark::State& state) {
  int value = 0;
  for (auto s : state) {
    Adjust(kRegisteredLabel, &value);
    ::benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_AdjustRegistered)->ThreadRange(1, 32)->UseRealTime();

}  // namespace
}  // namespace testvalue
}  // namespace testing
