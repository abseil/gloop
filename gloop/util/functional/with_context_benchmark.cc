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

#include "benchmark/benchmark.h"
#include "gloop/base/context.h"
#include "gloop/util/functional/with_context.h"

namespace util::functional {
namespace {

void BM_WithCurrentContext(benchmark::State& state) {
  const base::WithContext wc(base::BackgroundContext());
  for (auto _ : state) {
    WithCurrentContext([] {})();
  }
}
BENCHMARK(BM_WithCurrentContext);

}  // namespace
}  // namespace util::functional
