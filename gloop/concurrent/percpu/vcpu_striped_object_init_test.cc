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

// Tests that concurrent first calls to VcpuStripedObject::get() from multiple
// threads correctly initialize RSEQ.
//
// This lives in its own test binary so that nothing else in the process has
// initialized RSEQ before the worker threads race on their first get() call.

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "gloop/concurrent/percpu/vcpu_striped_object.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gtest/gtest.h"

namespace concurrent {
namespace percpu {
namespace {

TEST(VcpuStripedObjectInitTest, ConcurrentFirstTouchRseqInit) {
  VcpuStripedObject<int64_t> obj;
  constexpr int kNumThreads = 8;
  absl::Notification start;
  thread::Options options;
  options.set_joinable(true);
  std::vector<std::unique_ptr<::ClosureThread>> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::make_unique<::ClosureThread>(
        options, absl::StrCat("init_", i), [&] {
          start.WaitForNotification();
          *obj.get() += 1;
        }));
    threads.back()->Start();
  }
  start.Notify();
  for (std::unique_ptr<::ClosureThread>& t : threads) {
    t->Join();
  }
  int64_t total = 0;
  obj.ForEach([&](int64_t& v) { total += v; });
  EXPECT_EQ(total, kNumThreads);
}

}  // namespace
}  // namespace percpu
}  // namespace concurrent
