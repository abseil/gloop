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

#ifndef THIRD_PARTY_GLOOP_THREAD_MOCK_EXECUTOR_H_
#define THIRD_PARTY_GLOOP_THREAD_MOCK_EXECUTOR_H_

#include "absl/functional/any_invocable.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "gloop/thread/executor.h"
#include "gmock/gmock.h"

namespace thread {

class MockExecutor : public Executor {
 public:
  MOCK_METHOD(void, Schedule, (absl::AnyInvocable<void() &&> callback),
              (override));
  MOCK_METHOD(bool, TrySchedule, (absl::AnyInvocable<void() &&>), (override));
  MOCK_METHOD(void, ScheduleAfterForMigration,
              (absl::Duration delay, absl::AnyInvocable<void() &&> closure),
              (override));
  MOCK_METHOD(void, ScheduleAt,
              (absl::Time when, absl::AnyInvocable<void() &&> callback),
              (override));
  MOCK_METHOD(int, num_pending_closures, (), (const, override));
  MOCK_METHOD(absl::Clock*, clock, (), (override));
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_MOCK_EXECUTOR_H_
