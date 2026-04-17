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

#include "absl/base/call_once.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/bundle.h"
#include "gtest/gtest.h"

namespace {

static void Sleeper() { absl::SleepFor(absl::Milliseconds(1)); }

TEST(Fiber, CallOnce) {
  static absl::once_flag absl_once;
  constexpr int kNumFibers = 800;
  thread::Bundle bundle;

  for (int i = 0; i < kNumFibers; i++) {
    bundle.Add([] { absl::call_once(absl_once, &Sleeper); });
  }

  bundle.JoinAll();
}

}  // namespace
