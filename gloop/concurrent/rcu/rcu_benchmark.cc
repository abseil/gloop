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

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gloop/concurrent/rcu/rcu.h"

namespace base {
namespace rcu {
namespace {

void BM_BlockedDomainQueueOverhead(benchmark::State& state) {
  Domain d;
  Domain::EnableCleanup();

  // Permanently pin the domain to force Gloop's background thread
  // to keep it in the active cleanup list.
  ReaderLockHolder holder(&d);

  // In the buggy code, calling Call() repeatedly on a blocked domain
  // causes duplicates to pile up in the background thread's active list,
  // leading to exponential CPU burn in the background thread.
  for (auto s : state) {
    d.Call([]() {});
  }
  state.SetLabel(absl::StrCat("num_queues:", DomainTestPeer::num_queues(&d)));
}
BENCHMARK(BM_BlockedDomainQueueOverhead)->ThreadRange(1, 4);

}  // namespace
}  // namespace rcu
}  // namespace base
