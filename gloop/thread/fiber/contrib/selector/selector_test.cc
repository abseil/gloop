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

#include "gloop/thread/fiber/contrib/selector/selector.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/contrib/selector/handlers.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread {

struct SelectorExampleTest : public testing::Test {
  // Variables for test orchestration.
  absl::Notification int_channel_closed;
  absl::Notification string_channel_closed;

  // Tests whether all actions were executed.
  std::vector<std::string> log;

  // Channels for ReadHandler(s).
  thread::Channel<int> int_channel{0};
  thread::Channel<std::string> string_channel{0};

  // Writes to channels, cancels and join <select_fiber>
  // and tests <log> correctness.
  void RunSelectExample(thread::Fiber* select_fiber) {
    // Each write blocks until select_function in Selector reads out of it.
    string_channel.writer()->Write("Foo");
    int_channel.writer()->Write(12);
    int_channel.writer()->Write(42);
    string_channel.writer()->Write("Bar");
    // Close() is non blocking so we have to wait explicitly.
    string_channel.writer()->Close();
    string_channel_closed.WaitForNotification();
    int_channel.writer()->Close();
    int_channel_closed.WaitForNotification();
    select_fiber->Cancel();
    // Waits for Cancel.
    select_fiber->Join();

    std::vector<std::string> expected_log{"string: Foo",
                                          "int: 12",
                                          "int: 42",
                                          "string: Bar",
                                          "string_channel closed",
                                          "int_channel closed",
                                          "cancelled"};

    EXPECT_THAT(log, testing::UnorderedElementsAreArray(expected_log));
  }
};

// Note: Keep documentation and tests in sync.
TEST_F(SelectorExampleTest, SelectorUsageExample) {
  thread::Fiber selector_fiber([&] {
    bool cancelled = false;
    auto selector = MakeSelector(
        ReadHandler<int>(  //
            int_channel.reader(),
            [&](int i) { log.push_back(absl::StrCat("int: ", i)); },
            [&] {
              log.push_back(absl::StrCat("int_channel closed"));
              int_channel_closed.Notify();
            }),
        ReadHandler<std::string>(
            string_channel.reader(),
            [&](std::string s) { log.push_back(absl::StrCat("string: ", s)); },
            [&] {
              log.push_back(absl::StrCat("string_channel closed"));
              string_channel_closed.Notify();
            }),
        CancelHandler([&] {
          log.push_back("cancelled");
          cancelled = true;
        }));

    while (!cancelled) {
      selector.Execute();
    }
  });
  RunSelectExample(&selector_fiber);
}

// Note: Keep documentation and tests in sync.
TEST_F(SelectorExampleTest, EquivalentSelectUsageExample) {
  thread::Fiber select_fiber([&] {
    bool cancelled = false;

    int i;
    std::string s;
    bool read_ok;
    thread::CaseArray cases = {
        int_channel.reader()->OnRead(&i, &read_ok),     // case 0
        string_channel.reader()->OnRead(&s, &read_ok),  // case 1
        thread::OnCancel()                              // case 2
    };
    while (!cancelled) {
      int case_no = thread::Select(cases);
      switch (case_no) {
        case 0:  // int_channel read
          if (read_ok) {
            log.push_back(absl::StrCat("int: ", i));
          } else {
            cases[0] = thread::NonSelectableCase();
            log.push_back(absl::StrCat("int_channel closed"));
            int_channel_closed.Notify();
          }
          break;
        case 1:  // string_channel read
          if (read_ok) {
            log.push_back(absl::StrCat("string: ", s));
          } else {
            cases[1] = thread::NonSelectableCase();
            log.push_back(absl::StrCat("string_channel closed"));
            string_channel_closed.Notify();
          }
          break;
        case 2:  // on cancel
          log.push_back("cancelled");
          cancelled = true;
          break;
        default:
          LOG(FATAL) << "Can't happen";
      }
    }
  });

  RunSelectExample(&select_fiber);
}

TEST(SelectorTest, WriteHandlerUseCase) {
  Channel<int> channel(0);
  Fiber select_fiber([&] {
    int value = 0;
    for (int i = 0; i < 3; ++i) {
      auto selector = MakeSelector(
          WriteHandler<int>(channel.writer(), &value, [&] { ++value; }));
      selector.Execute();
    }
    channel.writer()->Close();
  });
  int result = -1;
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(channel.reader()->Read(&result));
    EXPECT_EQ(i, result);
  }
  EXPECT_FALSE(channel.reader()->Read(&result));
  select_fiber.Join();
}

TEST(SelectorTest, ChannelHandlerTemplateTypeDeductionWorks) {
  Channel<int> channel(0);
  int value;
  WriteHandler writer(channel.writer(), &value, [] {});
  static_assert(std::is_same_v<WriteHandler<int>, decltype(writer)>,
                "Deduced type of writer is incorrect");

  ReadHandler reader(channel.reader(), [](int) {}, [] {});
  static_assert(std::is_same_v<ReadHandler<int>, decltype(reader)>,
                "Deduced type of reader is incorrect");
}

TEST(SelectorTest, JoinHandlerUseCase) {
  absl::Notification notification1;
  absl::Notification notification2;
  bool done1 = false;
  bool done2 = false;
  Fiber to_join1([&] { notification1.WaitForNotification(); });
  Fiber to_join2([&] { notification2.WaitForNotification(); });
  // JoinHandler will only ever be selectable once, after which it returns
  // thread::NonSelectableCase.  (Compare Fiber::OnJoinable which would require
  // us to manually remove/replace cases from the CaseArray when they're done.)
  auto selector = MakeSelector(JoinHandler(&to_join1, [&] { done1 = true; }),
                               JoinHandler(&to_join2, [&] { done2 = true; }));
  notification2.Notify();
  selector.Execute();
  EXPECT_FALSE(done1);
  EXPECT_TRUE(done2);

  notification1.Notify();
  selector.Execute();
  EXPECT_TRUE(done1);
  EXPECT_TRUE(done2);

  // After OnJoinable returns we still need to call Join().
  to_join1.Join();
  to_join2.Join();
}

// A version of JoinHandlerUseCase that doesn't use Notifications to order the
// fibers.
TEST(SelectorTest, JoinHandlerUseCaseUnordered) {
  Fiber to_join1([] {});
  Fiber to_join2([] {});
  bool done1 = false;
  bool done2 = false;
  auto selector = MakeSelector(JoinHandler(&to_join1, [&] { done1 = true; }),
                               JoinHandler(&to_join2, [&] { done2 = true; }));
  // This could also be done via
  //   while (!done1 || !done2) {selector.Execute();}
  // However, for this unit test, we're a little more strict and check that the
  // selector only needs to be called twice.
  selector.Execute();
  selector.Execute();
  EXPECT_TRUE(done1);
  EXPECT_TRUE(done2);

  // After OnJoinable returns we still need to call Join().
  to_join1.Join();
  to_join2.Join();
}

// Test that we can use moveable objects in ReadHandler.
TEST(SelectorTest, MoveableReadHandler) {
  thread::Channel<std::unique_ptr<int>> channel(1);
  channel.writer()->Write(std::make_unique<int>(42));
  channel.writer()->Close();
  std::unique_ptr<int> result;
  MakeSelector(
      ReadHandler<std::unique_ptr<int>>(
          channel.reader(),
          [&](std::unique_ptr<int> value) { result = std::move(value); },
          [] {}))
      .Execute();
  EXPECT_TRUE(result);
  EXPECT_EQ(42, *result);
  EXPECT_FALSE(channel.reader()->Read(&result));
}

// Test CaseHandler.
TEST(SelectorTest, CaseHandler) {
  thread::PermanentEvent notification1;
  thread::PermanentEvent notification2;
  bool done1 = false;
  bool done2 = false;
  // JoinHandler will only ever be selectable once, after which it returns
  // thread::NonSelectableCase.  (Compare Fiber::OnJoinable which would require
  // us to manually remove/replace cases from the CaseArray when they're done.)
  auto selector = MakeSelector(CaseHandler(notification1.OnEvent(),
                                           [&] {
                                             done1 = true;
                                             return false;  // Do not select
                                                            // again.
                                           }),
                               CaseHandler(notification2.OnEvent(), [&] {
                                 done2 = true;
                                 return false;  // Do not select
                                                // again.
                               }));
  notification2.Notify();
  selector.Execute();
  EXPECT_FALSE(done1);
  EXPECT_TRUE(done2);

  notification1.Notify();
  selector.Execute();
  EXPECT_TRUE(done1);
  EXPECT_TRUE(done2);
}

TEST(SelectorTest, WithDeadlineInfinitePast) {
  thread::Channel<int> channel(1);

  auto selector =
      MakeSelector(ReadHandler<int>(channel.reader(), [](int) {}, [] {}));

  // Expect the selector to fail since the channel is not readable.
  EXPECT_FALSE(selector.ExecuteWithDeadline(absl::InfinitePast()));

  channel.writer()->Write(0);

  // Expect the selector to succeed.
  EXPECT_TRUE(selector.ExecuteWithDeadline(absl::InfinitePast()));
}

TEST(SelectorTest, WithDeadline) {
  thread::Channel<int> channel(1);

  auto selector =
      MakeSelector(ReadHandler<int>(channel.reader(), [](int) {}, [] {}));

  auto deadline = absl::Now() + absl::Milliseconds(10);
  EXPECT_FALSE(selector.ExecuteWithDeadline(deadline));
  EXPECT_LT(deadline, absl::Now());
}

TEST(SelectorTest, WithDeadlineViaSimulatedClock) {
  thread::Channel<int> channel(1);

  auto selector =
      MakeSelector(ReadHandler<int>(channel.reader(), [](int) {}, [] {}));

  absl::SimulatedClock simulated_clock;
  simulated_clock.SetTime(absl::FromUnixSeconds(517984800));  // arbitrary time
  auto deadline = simulated_clock.TimeNow() + absl::Milliseconds(50);

  thread::Detach(/* tree_options = */ {}, [&simulated_clock] {
    simulated_clock.AdvanceTime(absl::Milliseconds(51));
  });

  EXPECT_FALSE(selector.ExecuteWithDeadline(&simulated_clock, deadline));
  EXPECT_LT(deadline, simulated_clock.TimeNow());
}

}  // namespace thread
