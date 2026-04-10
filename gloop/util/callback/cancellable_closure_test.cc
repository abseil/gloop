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

// A test for CancellableClosure

#include "gloop/util/callback/cancellable_closure.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/base/init_google.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"

// Set *x to n.
static void SetToNWithDelayMS(int n, int* x, int delay_ms) {
  if (delay_ms > 0) {
    absl::SleepFor(absl::Milliseconds(delay_ms));
  }
  *x = n;
}

static void TestSimpleRun() {
  LOG(INFO) << "=== TestSimpleRun";
  util::callback::CancellableClosure* cc;
  int x;

  // Use Run() before Unref()
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { SetToNWithDelayMS(1, &x, 0); }));
  cc->Run();
  cc->Unref();
  CHECK_EQ(x, 1);

  // Use Unref() before Run()
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { SetToNWithDelayMS(1, &x, 0); }));
  cc->Unref();  // should be able to call Run after the last Unref().
  cc->Run();
  CHECK_EQ(x, 1);
}

static void TestRefUnref() {
  LOG(INFO) << "=== TestRefUnref";
  util::callback::CancellableClosure* cc;
  int x;
  static const int kReferences = 100;

  // Ensure that it takes one more Unref() than we've done Ref()s
  // to keep the heap checker happy.
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { SetToNWithDelayMS(1, &x, 0); }));
  cc->Run();
  CHECK_EQ(x, 1);
  for (int i = 0; i != kReferences; i++) {
    cc->Ref();
  }
  for (int i = 0; i != kReferences + 1; i++) {
    cc->Unref();
  }
}

int main(int argc, char* argv[]) {
  InitGoogle(argv[0], &argc, &argv, true);

  TestSimpleRun();
  TestRefUnref();

  printf("PASS\n");
  return 0;
}
