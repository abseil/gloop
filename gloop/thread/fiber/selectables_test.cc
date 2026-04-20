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
#include "benchmark/benchmark.h"
#include "gloop/base/cancellation_coloring.h"
#include "gloop/base/context.h"
#include "gloop/base/walltime.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gloop/perftools/tracing/with_trace_event_listener.h"
#include "gloop/thread/fiber/fiber-internal.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/probabilistic_test_util.h"
#include "gloop/thread/fiber/select.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread {

namespace t = ::testing;
using thread::probabilistic_test::RunTestMultipleTimes;

static const int kMillisecondsPerTick = 500;

static void Delay(int n) {
  absl::SleepFor(absl::Milliseconds(n * kMillisecondsPerTick));
}

static int ToTicks(WallTime start, WallTime finish) {
  return round((finish - start) * 1000.0 / kMillisecondsPerTick);
}

static void WaitForPermanentEvent(PermanentEvent* event, int* measured_delay) {
  absl::Time start = absl::Now();
  Select({event->OnEvent()});
  CHECK(event->HasBeenNotified());
  *measured_delay =
      ToTicks(base::ToWallTime(start), base::ToWallTime(absl::Now()));
}

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

// This test fails rarely (b/144519907) due to the lag being longer than
// expected in rare cases, so we run it multiple times.
TEST_F(SelectablesTest, MultiplyEnqueuedPermanentEvent) {
  static const int kRunsPerTest = 20;
  static const int kRunsToPass = 16;

  /* From probabilistic_test_util.h: "test_action should not contain
   * asserts as those would cause it to exit before it is run enough times in
   * some cases, rather test_action should LOG(WARNING) in the place of unmet
   * expectations." */

  auto runner = []() {
    PermanentEvent event;

    int f1_lag, f2_lag;
    Fiber f1([&event, &f1_lag] { WaitForPermanentEvent(&event, &f1_lag); });
    Fiber f2([&event, &f2_lag] { WaitForPermanentEvent(&event, &f2_lag); });
    Delay(1);

    event.Notify();
    f1.Join();
    f2.Join();

    int got_select = Select({event.OnEvent()});

    bool passed = true;
    if (1 != f1_lag) {
      passed &= false;
      LOG(WARNING) << "expected f1_lag = 1, got " << f1_lag << ".";
    }

    if (1 != f2_lag) {
      passed &= false;
      LOG(WARNING) << "expected f2_lag = 1, got " << f2_lag << ".";
    }

    if (0 != got_select) {
      passed &= false;
      LOG(WARNING) << "expected Select({event.OnEvent()}) = 0, got "
                   << got_select << ".";
    }
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
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

TEST_F(SelectablesTest, PermanentEventCanSynchronizeItsOwnDeletion) {
  for (int i = 0; i < 10000; ++i) {
    std::unique_ptr<Fiber> f;
    // We heap-allocate the PermanentEvent so that when we delete it, the debug
    // malloc will overwrite it with garbage.
    std::unique_ptr<PermanentEvent> e(new PermanentEvent);
    f = std::make_unique<Fiber>([&] { e->Notify(); });
    Select({e->OnEvent()});
    e.reset();
    f->Join();
  }
}

TEST_F(SelectablesTest, SelectInitializerList) {
  PermanentEvent e1, e2, e3;
  e2.Notify();

  EXPECT_EQ(1, Select({e1.OnEvent(), e2.OnEvent(), e3.OnEvent()}));
}

TEST_F(SelectablesTest, SelectWithClockAndDeadline) {
  absl::SimulatedClock c;

  Fiber f([&c] {
    absl::SleepFor(absl::Seconds(0.5));
    c.AdvanceTime(absl::Milliseconds(300));
  });

  PermanentEvent e1;
  EXPECT_EQ(-1, SelectUntil(&c, c.TimeNow() + absl::Milliseconds(250),
                            {e1.OnEvent()}));

  f.Join();
}

TEST_F(SelectablesTest,
       SelectWithClockAndDeadline_EventReadiesBeforeDeadlinePasses) {
  absl::SimulatedClock c;

  PermanentEvent e1;
  Fiber f([&c, &e1] {
    c.AdvanceTime(absl::Milliseconds(300));
    e1.Notify();
    c.AdvanceTime(absl::Milliseconds(100));
  });

  // The deadline is now after the event firing time. We should get zero back
  // from SelectUntil.
  EXPECT_EQ(0, SelectUntil(&c, c.TimeNow() + absl::Milliseconds(350),
                           {e1.OnEvent()}));
  f.Join();
}

TEST_F(SelectablesTest, SelectUsesSpecifiedClock) {
  absl::SimulatedClock c;

  Fiber f([&c] {
    EXPECT_EQ(-1, SelectUntil(&c, c.TimeNow() + absl::Milliseconds(1),
                              {NonSelectableCase()}));
  });

  absl::SleepFor(absl::Milliseconds(100));
  EXPECT_EQ(-1, TrySelect({f.OnJoinable()}));
  // Even though 100ms of real time has passed, f should remain incomplete until
  // our simulated clock advances.
  c.AdvanceTime(absl::Milliseconds(1));
  f.Join();
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
  ASSERT_DEATH_IF_SUPPORTED(Select({}), "No cases provided");
}

TEST(SelectTest, EmptyCaseListSelectUntil) {
  // Checking for no crash implicitly too.
  PermanentEvent selected;
  Fiber f([&selected] {
    EXPECT_EQ(
        -1, SelectUntil(absl::Now() + absl::Milliseconds(kMillisecondsPerTick),
                        {}));
    selected.Notify();
  });

  EXPECT_FALSE(selected.HasBeenNotified());
  // We could try to measure the time here, but all it would really tell us
  // about would be the scheduling delay of testrunner machines. Limiting the
  // time with SelectUntil() doesn't help; the Join() will ensure that the test
  // takes as long as it takes.
  EXPECT_EQ(0, Select({selected.OnEvent()}));
  f.Join();
}

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST)
// If the active cancellation color is not kUnknown or kFibers, we shouldn't be
// able to select on or query cancellation of a fiber.
TEST(SelectablesDeathTest, UseCancellationEventUnderOtherCancellationColor) {
  base::internal::WithCancellationColor wcc(
      base::internal::CancellationColor::kFake);

  EXPECT_DEATH(
      { TrySelect({thread::OnCancel()}); }, t::AllOfArray({
                                                t::HasSubstr("<link>"),
                                                t::HasSubstr("kFake"),
                                            }));

  EXPECT_DEATH(
      { thread::Cancelled(); }, t::AllOfArray({
                                    t::HasSubstr("<link>"),
                                    t::HasSubstr("kFake"),
                                }));

  EXPECT_DEATH(
      { thread::Fiber::Current()->Cancelled(); }, t::AllOfArray({
                                                      t::HasSubstr("<link>"),
                                                      t::HasSubstr("kFake"),
                                                  }));
}

// In contrast to the situations covered by the death test above, it should
// still be legal to do several operations under non-fiber cancellation colors.
TEST_F(SelectablesTest, LegalUsesUnderOtherCancellationColor) {
  // Calling under another function color should work for non-cancellation
  // events.
  {
    base::internal::WithCancellationColor wcc(
        base::internal::CancellationColor::kFake);

    PermanentEvent event;
    TrySelect({event.OnEvent()});
    event.HasBeenNotified();
  }

  // It should also work with an explicit color of kFibers, even for
  // cancellation events.
  {
    base::internal::WithCancellationColor wcc(
        base::internal::CancellationColor::kFibers);

    TrySelect({thread::OnCancel()});
    thread::Cancelled();
    thread::Fiber::Current()->Cancelled();
  }

  // And with the default of kUnknown.
  {
    ASSERT_EQ(base::internal::CancellationColor::kUnknown,
              base::internal::GetActiveCancellationColor());

    TrySelect({thread::OnCancel()});
    thread::Cancelled();
    thread::Fiber::Current()->Cancelled();
  }

  // It should also be fine to create, cancel, and join a fiber tree under
  // another function color.
  {
    base::internal::WithCancellationColor wcc(
        base::internal::CancellationColor::kFake);

    const std::unique_ptr<thread::Fiber> f = thread::NewTree(
        thread::TreeOptions(), [] { thread::Select({thread::OnCancel()}); });

    f->Cancel();
    f->Join();
  }

  // Detaching a new fiber tree should be fine, even with a deadline configured.
  {
    base::internal::WithCancellationColor wcc(
        base::internal::CancellationColor::kFake);

    thread::Detach(thread::TreeOptions().set_context(
                       base::ContextBuilder(base::CurrentContext())
                           .set_deadline(absl::Now() + absl::Hours(1))
                           .BuildValue()),
                   [] {});
  }
}
#endif

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

static void BM_SelectPermOrCancel(benchmark::State& state) {
  PermanentEvent event;
  event.Notify();
  for (auto _ : state) {
    thread::Select({event.OnEvent(), OnCancel()});
  }
}
BENCHMARK(BM_SelectPermOrCancel);

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
