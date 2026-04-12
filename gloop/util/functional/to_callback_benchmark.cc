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

#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/gloop_test.h"
#include "gloop/util/functional/to_callback.h"

namespace util::functional {
namespace {

void BM_ToClosureWithBackgroundContext(benchmark::State& state) {
  const base::WithContext wc(base::BackgroundContext());
  for (auto _ : state) {
    Closure* closure = ToCallback([] {});
    closure->Run();
  }
}
BENCHMARK(BM_ToClosureWithBackgroundContext);

}  // namespace
}  // namespace util::functional
