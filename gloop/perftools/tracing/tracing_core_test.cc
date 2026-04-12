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

#include "gloop/perftools/tracing/tracing_core.h"

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/tracing.h"
#include "absl/log/check.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gloop/gloop_test.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing::core {
namespace {

using ::testing::Eq;
using ::testing::InSequence;
using ::testing::StrictMock;

// This should not be needed b/356628268
using absl::base_internal::AbslInternalTraceWait;  // NOLINT
using ::absl::base_internal::ObjectKind;           // NOLINT

// Helper class to check thread state to be clean and install the mock.
struct WithMock {
  explicit WithMock(MockTraceEventListener& mock) {
    CHECK_EQ(active_sync_id(), kNoSyncId);
    CHECK_EQ(internal::active_event_listener(), nullptr);
    internal::set_active_sync_id(SyncId{1});
    internal::set_active_event_listener(&mock);
  }

  ~WithMock() {
    internal::set_active_sync_id(kNoSyncId);
    internal::set_active_event_listener(nullptr);
  }
};

TEST(TracingCore, ApiWithoutActiveSync) {
  TraceSpawn(SyncId{4}, "Spam");
  TraceWait(BarrierId{123}, "a while");
  TraceContinue(BarrierId{123});
  TraceObserved(BarrierId{123}, "Peekaboo");
  TraceSignal(BarrierId{123}, "Ping");
  TraceSend("Send it", MsgOrigin::kClient, MsgId{3232}, MsgSequence{1});
  TraceReceive("praise", MsgOrigin::kServer, MsgId{3232}, MsgSequence{1});
  TraceSessionStart("Start", MsgId{842}, EndPoint::kStreamingClient);
  TraceSessionEnd("End", MsgId{842}, EndPoint::kStreamingServer);
  TraceStreamingSend(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                     MsgFlags::kDefault);
  TraceStreamingSend(MsgOrigin::kServer, MsgId{842}, MsgSequence{17},
                     MsgFlags::kHalfClose);
  TraceStreamingReceive(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                        MsgFlags::kDefault);
  TraceStreamingReceive(MsgOrigin::kServer, MsgId{842}, MsgSequence{13},
                        MsgFlags::kHalfClose);
  TraceMark("Not Marc");
  TraceBeginRegion("Here");
  TraceEndRegion();
  TraceControlFlow("Flow", ControlFlowType::kSchedule, ControlFlowId{635412},
                   ControlFlowSequence{1});
}

TEST(TracingCore, ApiWithActiveSync) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithMock with(mock);

  EXPECT_CALL(mock, OnTraceSpawn(SyncId{4}, Eq("Spam")));
  EXPECT_CALL(mock, OnTraceWait(BarrierId{123}, Eq("a while")));
  EXPECT_CALL(mock, OnTraceContinue(BarrierId{123}));
  EXPECT_CALL(mock, OnTraceObserved(BarrierId{123}, Eq("Peekaboo")));
  EXPECT_CALL(mock, OnTraceSignal(BarrierId{123}, Eq("Ping")));

  EXPECT_CALL(mock, OnTraceSend(Eq("Send it"), MsgOrigin::kClient, MsgId{3232},
                                MsgSequence{1}));
  EXPECT_CALL(mock, OnTraceReceive(Eq("praise"), MsgOrigin::kServer,
                                   MsgId{3232}, MsgSequence{1}));

  EXPECT_CALL(mock, OnTraceSessionStart(Eq("Start"), MsgId{842},
                                        EndPoint::kStreamingClient));
  EXPECT_CALL(mock, OnTraceSessionEnd(Eq("End"), MsgId{842},
                                      EndPoint::kStreamingServer));

  EXPECT_CALL(mock, OnTraceStreamingSend(MsgOrigin::kClient, MsgId{842},
                                         MsgSequence{0}, MsgFlags::kDefault));
  EXPECT_CALL(mock,
              OnTraceStreamingSend(MsgOrigin::kServer, MsgId{842},
                                   MsgSequence{17}, MsgFlags::kHalfClose));
  EXPECT_CALL(mock,
              OnTraceStreamingReceive(MsgOrigin::kClient, MsgId{842},
                                      MsgSequence{0}, MsgFlags::kDefault));
  EXPECT_CALL(mock,
              OnTraceStreamingReceive(MsgOrigin::kServer, MsgId{842},
                                      MsgSequence{13}, MsgFlags::kHalfClose));

  EXPECT_CALL(mock, OnTraceMark(Eq("Not Marc")));
  EXPECT_CALL(mock, OnTraceBeginRegion(Eq("Here")));
  EXPECT_CALL(mock, OnTraceEndRegion());
  EXPECT_CALL(
      mock, OnTraceControlFlow(Eq("Flow"), ControlFlowType::kSchedule,
                               ControlFlowId{635412}, ControlFlowSequence{1}));

  TraceSpawn(SyncId{4}, "Spam");
  TraceWait(BarrierId{123}, "a while");
  TraceContinue(BarrierId{123});
  TraceObserved(BarrierId{123}, "Peekaboo");
  TraceSignal(BarrierId{123}, "Ping");

  TraceSend("Send it", MsgOrigin::kClient, MsgId{3232}, MsgSequence{1});
  TraceReceive("praise", MsgOrigin::kServer, MsgId{3232}, MsgSequence{1});

  TraceSessionStart("Start", MsgId{842}, EndPoint::kStreamingClient);
  TraceSessionEnd("End", MsgId{842}, EndPoint::kStreamingServer);

  TraceStreamingSend(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                     MsgFlags::kDefault);
  TraceStreamingSend(MsgOrigin::kServer, MsgId{842}, MsgSequence{17},
                     MsgFlags::kHalfClose);
  TraceStreamingReceive(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                        MsgFlags::kDefault);
  TraceStreamingReceive(MsgOrigin::kServer, MsgId{842}, MsgSequence{13},
                        MsgFlags::kHalfClose);

  TraceMark("Not Marc");
  TraceBeginRegion("Here");
  TraceEndRegion();
  TraceControlFlow("Flow", ControlFlowType::kSchedule, ControlFlowId{635412},
                   ControlFlowSequence{1});
}

#if ABSL_HAVE_ATTRIBUTE_WEAK

TEST(TracingCore, AbslNotification) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithMock with(mock);

  absl::Notification notification;
  auto barrier_id = ToBarrierId(&notification);

  // Not signaled / timed out --> kNoBarrier
  EXPECT_CALL(mock, OnTraceWait(barrier_id, Eq("absl::Notification")));
  EXPECT_CALL(mock, OnTraceContinue(kNoBarrierId));
  notification.WaitForNotificationWithTimeout(absl::ZeroDuration());

  EXPECT_CALL(mock, OnTraceSignal(barrier_id, Eq("absl::Notification")));
  notification.Notify();

  // Notified state results in observation event.
  EXPECT_CALL(mock, OnTraceObserved(barrier_id, Eq("absl::Notification")));
  EXPECT_TRUE(notification.HasBeenNotified());

  // Signaled --> barrier_id
  EXPECT_CALL(mock, OnTraceWait(barrier_id, Eq("absl::Notification")));
  EXPECT_CALL(mock, OnTraceContinue(barrier_id));
  notification.WaitForNotification();
}

TEST(TracingCore, AbslNotificationObserveNotNotified) {
  StrictMock<MockTraceEventListener> mock;
  WithMock with(mock);

  // Observing a non notified state should  not result in an event.
  absl::Notification notification;
  EXPECT_FALSE(notification.HasBeenNotified());
}

TEST(TracingCore, AbslBlockingCounter) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithMock with(mock);

  absl::BlockingCounter counter{2};
  auto barrier_id = ToBarrierId(&counter);

  // Should emit only one signal going from 1 -> 0
  EXPECT_CALL(mock, OnTraceSignal(barrier_id, Eq("absl::BlockingCounter")));
  counter.DecrementCount();
  counter.DecrementCount();

  // Signaled --> barrier_id
  EXPECT_CALL(mock, OnTraceWait(barrier_id, Eq("absl::BlockingCounter")));
  EXPECT_CALL(mock, OnTraceContinue(barrier_id));
  counter.Wait();
}

TEST(TracingCore, AbslBlockingCounterObserveNotZero) {
  StrictMock<MockTraceEventListener> mock;
  WithMock with(mock);

  // Decrementing not reaching zero should not result in an event.
  absl::BlockingCounter counter{2};
  counter.DecrementCount();
}

TEST(TracingCore, UnknownAndBadObjectKind) {
  StrictMock<MockTraceEventListener> mock;
  WithMock with(mock);
  auto barrier_id = ToBarrierId(&mock);
  auto bad_kind = static_cast<ObjectKind>(-1);
  auto unknown_kind = ObjectKind::kUnknown;

  EXPECT_CALL(mock, OnTraceWait(barrier_id, Eq("absl::Unknown"))).Times(2);
  ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceWait)(&mock, unknown_kind);
  ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceWait)(&mock, bad_kind);
}

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK

}  // namespace
}  // namespace perftools::tracing::core
