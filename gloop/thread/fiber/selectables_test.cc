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

#include "gloop/thread/fiber/selectables.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/clock_interface.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "gloop/base/cancellation_coloring.h"
#include "gloop/base/context.h"
#include "gloop/base/walltime.h"
#include "gloop/gloop_test.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gloop/perftools/tracing/with_trace_event_listener.h"
#include "gloop/thread/fiber/probabilistic_test_util.h"
#include "gloop/thread/fiber/select.h"

namespace thread {

namespace t = ::testing;
using thread::probabilistic_test::RunTestMultipleTimes;

// Matches that `arg` contains is a label holding a source location.
MATCHER(HoldsSourceLocation, "Holds a source location") {
  return arg.IsSourceLocation();
}

class SelectablesTest : public ::testing::Test {};

// Matches `arg` to hold a source location value of {__FILE__, line}
MATCHER_P(IsSourceLocation, line, "Matches source location") {
  return arg.IsSourceLocation() &&
         arg.file_name() == absl::string_view(__FILE__) && arg.line() == line;
}

TEST_F(SelectablesTest, BasicPermanentEvent) {
  PermanentEvent event;

  EXPECT_EQ(-1, TrySelect({event.OnEvent()}));
  event.Notify();
  // Selecting against an already signalled event.
  EXPECT_EQ(0, TrySelect({event.OnEvent()}));
}

TEST_F(SelectablesTest, SelectPermutes) {
  PermanentEvent e1, e2, e3;

  e1.Notify();
  e2.Notify();
  e3.Notify();

  // While no guarantees are made regarding the distribution for Select, we
  // should at least observe all three possible values here.
  bool saw[3] = {false, false, false};
  for (int i = 0; i < 10000; i++) {
    saw[Select({e1.OnEvent(), e2.OnEvent(), e3.OnEvent()})] = true;
  }
  EXPECT_TRUE(saw[0]);
  EXPECT_TRUE(saw[1]);
  EXPECT_TRUE(saw[2]);
}

TEST_F(SelectablesTest, NonSelectableNotReady) {
  // NonSelectableCase cases are eponymous.
  EXPECT_EQ(-1, TrySelect({NonSelectableCase()}));
}

TEST_F(SelectablesTest, AlwaysSelectableIsReady) {
  EXPECT_EQ(0, TrySelect({AlwaysSelectableCase()}));
}

TEST_F(SelectablesTest, AlwaysSelectableDoesNotMindHavingMultipleUses) {
  EXPECT_LE(0, Select({AlwaysSelectableCase(), AlwaysSelectableCase()}));
}

TEST_F(SelectablesTest, SelectInitializerList) {
  PermanentEvent e1, e2, e3;
  e2.Notify();

  EXPECT_EQ(1, Select({e1.OnEvent(), e2.OnEvent(), e3.OnEvent()}));
}

TEST_F(SelectablesTest, SelectWithSpecifiedClockAlreadyPastDeadline) {
  absl::SimulatedClock c;
  absl::Time start = c.TimeNow();
  c.AdvanceTime(absl::Milliseconds(1));
  EXPECT_EQ(-1, SelectUntil(&c, start, {NonSelectableCase()}));
}

// TODO: It's probably time to refactor from this and channel_test to
// create a select specific unit test.
TEST(SelectDeathTest, EmptyCaseList) {
  GLOOP_ASSERT_DEATH_IF_SUPPORTED(Select({}), "No cases provided");
}

static void BM_SelectPerm(benchmark::State& state) {
  PermanentEvent event;
  event.Notify();
  for (auto _ : state) {
    thread::Select({event.OnEvent()});
  }
}
BENCHMARK(BM_SelectPerm);

static void BM_SelectPermFull(benchmark::State& state) {
  for (auto _ : state) {
    PermanentEvent event;
    event.Notify();
    thread::Select({event.OnEvent()});
  }
}
BENCHMARK(BM_SelectPermFull);

static void BM_SelectManyCases(benchmark::State& state) {
  std::vector<PermanentEvent> events(state.range(0));
  events[0].Notify();
  thread::CaseArray cases;
  for (size_t i = 0; i < events.size(); i++) {
    cases.push_back(events[i].OnEvent());
  }
  for (auto _ : state) {
    thread::Select(cases);
  }
}
BENCHMARK(BM_SelectManyCases)->Arg(10)->Arg(100)->Arg(1000);

// Benchmarks
// . PermanentEvent + concurrent notification?

}  // namespace thread
