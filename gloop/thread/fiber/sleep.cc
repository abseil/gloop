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

#include "gloop/thread/fiber/sleep.h"

#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"

namespace thread {

bool CancellableSleepFor(absl::Duration d) {
  return CancellableSleepFor(nullptr, d);
}

bool CancellableSleepUntil(absl::Time t) {
  return CancellableSleepUntil(nullptr, t);
}

bool CancellableSleepFor(absl::Clock* clock, absl::Duration d) {
  absl::Time timeout = (clock ? clock->TimeNow() : absl::Now()) + d;
  LOG(INFO) << "CancellableSleepFor: " << d << " (Sleep start)"
            << " timeout: " << timeout;
  return CancellableSleepUntil(clock, timeout);
}

bool CancellableSleepUntil(absl::Clock* clock, absl::Time t) {
  if (clock) {
    if (clock == &absl::Clock::GetRealClock()) {
      LOG(INFO) << "[Real Clock] CancellableSleepUntil: " << t
                << " (Sleep start), current time: " << clock->TimeNow();
    } else {
      LOG(INFO) << "[Simulated Clock] CancellableSleepUntil: " << t
                << " (Sleep start), current time: " << clock->TimeNow();
    }
  } else {
    LOG(INFO) << "[Default Clock] CancellableSleepUntil: " << t
              << " (Sleep start), current time: " << absl::Now();
  }
  int i = thread::SelectUntil(clock, t, {thread::OnCancel()});
  if (clock) {
    LOG(INFO) << "CancellableSleepUntil: " << t
              << " (Sleep done), current time: " << clock->TimeNow();
  } else {
    LOG(INFO) << "CancellableSleepUntil: " << t
              << " (Sleep done), current time: " << absl::Now();
  }
  return i == -1;
}

}  // namespace thread
