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

#include "gloop/perftools/tracing/multiplex_trace_event_listener.h"

#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/test_only_access.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perftools::tracing {
namespace {

using ::perftools::tracing::testing::TestOnlyAccess;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Le;
using ::testing::Return;
using ::testing::StrictMock;

TraceSourceLocation SourceLocation(const char* file, int line) {
  return TraceSourceLocation(
      TestOnlyAccess::Create<TraceSourceLocation::Access>(), file, line);
}

MATCHER_P2(EqSourceLocation, file, line, "Matches source location") {
  return arg.file_name() == absl::string_view(file) && arg.line() == line;
}

// Minimum default TraceEventListener implementation.
class DefaultTraceEventListener : public TraceEventListener {
 public:
  TraceEventListener* GetEventListener(SyncId) override { return this; }
  void ReleaseEventListener() override {}
};

TEST(MultiplexTraceEventListener, Basics) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> first, second;

  auto& listener = *MultiplexTraceEventListener(&first, &second);

  EXPECT_CALL(first, OnTraceSpawn(SyncId{632}, Eq("Hello world")));
  EXPECT_CALL(second, OnTraceSpawn(SyncId{632}, Eq("Hello world")));

  EXPECT_CALL(first, OnTraceBeginSync(SyncId{412}, Eq("Begin")));
  EXPECT_CALL(second, OnTraceBeginSync(SyncId{412}, Eq("Begin")));
  EXPECT_CALL(second, OnTraceEndSync(SyncId{721}));
  EXPECT_CALL(first, OnTraceEndSync(SyncId{721}));

  EXPECT_CALL(second, OnTraceSuspendSync(SyncId{721}));
  EXPECT_CALL(first, OnTraceSuspendSync(SyncId{721}));
  EXPECT_CALL(first, OnTraceResumeSync(SyncId{721}));
  EXPECT_CALL(second, OnTraceResumeSync(SyncId{721}));

  EXPECT_CALL(first, OnTraceEnterSync(SyncId{722}, Eq("Enter")));
  EXPECT_CALL(second, OnTraceEnterSync(SyncId{722}, Eq("Enter")));

  EXPECT_CALL(first, OnTraceWait(BarrierId{313131}, Eq("Wait wait")));
  EXPECT_CALL(second, OnTraceWait(BarrierId{313131}, Eq("Wait wait")));
  EXPECT_CALL(second, OnTraceContinue(BarrierId{3412442}));
  EXPECT_CALL(first, OnTraceContinue(BarrierId{3412442}));

  EXPECT_CALL(first, OnTraceObserved(BarrierId{3412442}, Eq("Peekaboo")));
  EXPECT_CALL(second, OnTraceObserved(BarrierId{3412442}, Eq("Peekaboo")));
  EXPECT_CALL(first, OnTraceSignal(BarrierId{7648223}, Eq("Ping")));
  EXPECT_CALL(second, OnTraceSignal(BarrierId{7648223}, Eq("Ping")));

  EXPECT_CALL(first, OnTraceSend(Eq("Send it!"), MsgOrigin::kClient,
                                 MsgId{3332}, MsgSequence{1}));
  EXPECT_CALL(second, OnTraceSend(Eq("Send it!"), MsgOrigin::kClient,
                                  MsgId{3332}, MsgSequence{1}));
  EXPECT_CALL(first, OnTraceReceive(Eq("Send it!"), MsgOrigin::kServer,
                                    MsgId{3332}, MsgSequence{1}));
  EXPECT_CALL(second, OnTraceReceive(Eq("Send it!"), MsgOrigin::kServer,
                                     MsgId{3332}, MsgSequence{1}));

  EXPECT_CALL(first, OnTraceSessionStart(Eq("Start"), MsgId{842},
                                         EndPoint::kStreamingClient));
  EXPECT_CALL(second, OnTraceSessionStart(Eq("Start"), MsgId{842},
                                          EndPoint::kStreamingClient));
  EXPECT_CALL(second, OnTraceSessionEnd(Eq("End"), MsgId{842},
                                        EndPoint::kStreamingServer));
  EXPECT_CALL(first, OnTraceSessionEnd(Eq("End"), MsgId{842},
                                       EndPoint::kStreamingServer));

  EXPECT_CALL(first,
              OnTraceStreamingSend(MsgOrigin::kClient, MsgId{842},
                                   MsgSequence{1}, MsgFlags::kHalfClose));
  EXPECT_CALL(second,
              OnTraceStreamingSend(MsgOrigin::kClient, MsgId{842},
                                   MsgSequence{1}, MsgFlags::kHalfClose));
  EXPECT_CALL(first,
              OnTraceStreamingReceive(MsgOrigin::kClient, MsgId{842},
                                      MsgSequence{2}, MsgFlags::kControl));
  EXPECT_CALL(second,
              OnTraceStreamingReceive(MsgOrigin::kClient, MsgId{842},
                                      MsgSequence{2}, MsgFlags::kControl));

  EXPECT_CALL(first,
              OnTraceMark(Eq("Not Marc"), EqSourceLocation("foo.cc", 11)));
  EXPECT_CALL(second,
              OnTraceMark(Eq("Not Marc"), EqSourceLocation("foo.cc", 11)));
  EXPECT_CALL(first, OnTraceBeginRegion(Eq("Here, not there"),
                                        EqSourceLocation("foo.cc", 12)));
  EXPECT_CALL(second, OnTraceBeginRegion(Eq("Here, not there"),
                                         EqSourceLocation("foo.cc", 12)));
  EXPECT_CALL(second, OnTraceEndRegion());
  EXPECT_CALL(first, OnTraceEndRegion());

  EXPECT_CALL(first,
              OnTraceControlFlow(Eq("Flow"), ControlFlowType::kSchedule,
                                 ControlFlowId{3421}, ControlFlowSequence(1)));
  EXPECT_CALL(second,
              OnTraceControlFlow(Eq("Flow"), ControlFlowType::kSchedule,
                                 ControlFlowId{3421}, ControlFlowSequence(1)));

  listener.OnTraceSpawn(SyncId{632}, "Hello world");
  listener.OnTraceBeginSync(SyncId{412}, "Begin");
  listener.OnTraceEndSync(SyncId{721});
  listener.OnTraceSuspendSync(SyncId{721});
  listener.OnTraceResumeSync(SyncId{721});
  listener.OnTraceEnterSync(SyncId{722}, "Enter");
  listener.OnTraceWait(BarrierId{313131}, "Wait wait");
  listener.OnTraceContinue(BarrierId{3412442});
  listener.OnTraceObserved(BarrierId{3412442}, "Peekaboo");
  listener.OnTraceSignal(BarrierId{7648223}, "Ping");
  listener.OnTraceSend("Send it!", MsgOrigin::kClient, MsgId{3332},
                       MsgSequence{1});
  listener.OnTraceReceive("Send it!", MsgOrigin::kServer, MsgId{3332},
                          MsgSequence{1});

  listener.OnTraceSessionStart("Start", MsgId{842}, EndPoint::kStreamingClient);
  listener.OnTraceSessionEnd("End", MsgId{842}, EndPoint::kStreamingServer);

  listener.OnTraceStreamingSend(MsgOrigin::kClient, MsgId{842}, MsgSequence{1},
                                MsgFlags::kHalfClose);
  listener.OnTraceStreamingReceive(MsgOrigin::kClient, MsgId{842},
                                   MsgSequence{2}, MsgFlags::kControl);

  listener.OnTraceMark("Not Marc", SourceLocation("foo.cc", 11));
  listener.OnTraceBeginRegion("Here, not there", SourceLocation("foo.cc", 12));
  listener.OnTraceEndRegion();
  listener.OnTraceControlFlow("Flow", ControlFlowType::kSchedule,
                              ControlFlowId{3421}, ControlFlowSequence(1));

  EXPECT_CALL(second, ReleaseEventListener());
  EXPECT_CALL(first, ReleaseEventListener());
  listener.ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, MultiplexNullptr) {
  StrictMock<MockTraceEventListener> first, second;
  EXPECT_THAT(MultiplexTraceEventListener(&first, nullptr), Eq(&first));
  EXPECT_THAT(MultiplexTraceEventListener(nullptr, &second), Eq(&second));
  EXPECT_THAT(MultiplexTraceEventListener(nullptr, nullptr), Eq(nullptr));
}

TEST(MultiplexTraceEventListener, GetEventListener) {
  StrictMock<MockTraceEventListener> first1, second1, first2, second2;
  InSequence in_sequence;

  auto* listener1 = MultiplexTraceEventListener(&first1, &second1);

  EXPECT_CALL(first1, GetEventListener(SyncId{3})).WillOnce(Return(&first2));
  EXPECT_CALL(second1, GetEventListener(SyncId{3})).WillOnce(Return(&second2));
  auto* listener2 = listener1->GetEventListener(SyncId{3});

  EXPECT_CALL(second2, ReleaseEventListener());
  EXPECT_CALL(first2, ReleaseEventListener());
  listener2->ReleaseEventListener();

  EXPECT_CALL(second1, ReleaseEventListener());
  EXPECT_CALL(first1, ReleaseEventListener());
  listener1->ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, GetEventListenerReturningNull) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> first1, second1;

  auto* listener1 = MultiplexTraceEventListener(&first1, &second1);

  StrictMock<MockTraceEventListener> first2, second2;

  EXPECT_CALL(first1, GetEventListener(SyncId{3})).WillOnce(Return(nullptr));
  EXPECT_CALL(second1, GetEventListener(SyncId{3})).WillOnce(Return(&second2));
  auto* listener2 = listener1->GetEventListener(SyncId{3});
  EXPECT_THAT(listener2, Eq(&second2));

  EXPECT_CALL(first1, GetEventListener(SyncId{3})).WillOnce(Return(&first2));
  EXPECT_CALL(second1, GetEventListener(SyncId{3})).WillOnce(Return(nullptr));
  listener2 = listener1->GetEventListener(SyncId{3});
  EXPECT_THAT(listener2, Eq(&first2));

  EXPECT_CALL(first1, GetEventListener(SyncId{3})).WillOnce(Return(nullptr));
  EXPECT_CALL(second1, GetEventListener(SyncId{3})).WillOnce(Return(nullptr));
  listener2 = listener1->GetEventListener(SyncId{3});
  EXPECT_THAT(listener2, Eq(nullptr));

  EXPECT_CALL(second1, ReleaseEventListener());
  EXPECT_CALL(first1, ReleaseEventListener());
  listener1->ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, Contains) {
  StrictMock<MockTraceEventListener> first, second, other;
  auto* listener = MultiplexTraceEventListener(&first, &second);

  // nullptr returns true and never invokes 'first' or 'second'
  EXPECT_FALSE(listener->Contains(nullptr));

  // 'self' always returns true and never invokes 'first' or 'second'
  EXPECT_TRUE(listener->Contains(listener));

  // Contains for `second` is a direct hit in O(1) because LIFO
  EXPECT_CALL(second, Contains(&second)).WillOnce(Return(true));
  EXPECT_TRUE(listener->Contains(&second));

  // Contains for `first` hits `second` first (and thus O(n))
  EXPECT_CALL(second, Contains(&first)).WillOnce(Return(false));
  EXPECT_CALL(first, Contains(&first)).WillOnce(Return(true));
  EXPECT_TRUE(listener->Contains(&first));

  // Validate `false' responses for both finding a non contained value.
  EXPECT_CALL(second, Contains(&other)).WillOnce(Return(false));
  EXPECT_CALL(first, Contains(&other)).WillOnce(Return(false));
  EXPECT_FALSE(listener->Contains(&other));

  EXPECT_CALL(second, ReleaseEventListener());
  EXPECT_CALL(first, ReleaseEventListener());
  listener->ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, ExtractSelf) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> first, second;
  auto* listener = MultiplexTraceEventListener(&first, &second);
  EXPECT_CALL(second, ReleaseEventListener());
  EXPECT_CALL(first, ReleaseEventListener());
  EXPECT_THAT(listener->Extract(listener), Eq(std::make_pair(nullptr, true)));
  listener->ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, ExtractSecond) {
  StrictMock<MockTraceEventListener> first, second;
  auto& listener = *MultiplexTraceEventListener(&first, &second);
  EXPECT_CALL(second, Extract(&second))
      .WillOnce(Return(std::make_pair(nullptr, true)));
  auto [res, success] = listener.Extract(&second);
  EXPECT_TRUE(success);
  EXPECT_THAT(res, Eq(&first));
}

TEST(MultiplexTraceEventListener, ExtractSecondFolds) {
  StrictMock<MockTraceEventListener> first, second, other;
  auto& listener = *MultiplexTraceEventListener(&first, &second);
  EXPECT_CALL(second, Extract(&second))
      .WillOnce(Return(std::make_pair(&other, true)));
  auto [res, success] = listener.Extract(&second);
  EXPECT_TRUE(success);
  EXPECT_THAT(res, Eq(&listener));

  EXPECT_CALL(other, ReleaseEventListener());
  EXPECT_CALL(first, ReleaseEventListener());
  listener.ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, ExtractFirst) {
  StrictMock<MockTraceEventListener> first, second, ignored;
  auto& listener = *MultiplexTraceEventListener(&first, &second);
  EXPECT_CALL(second, Extract(&first))
      .WillOnce(Return(std::make_pair(&ignored, false)));
  EXPECT_CALL(first, Extract(&first))
      .WillOnce(Return(std::make_pair(nullptr, true)));
  auto [res, success] = listener.Extract(&first);
  EXPECT_TRUE(success);
  EXPECT_THAT(res, Eq(&second));
}

TEST(MultiplexTraceEventListener, ExtractFirstFolds) {
  StrictMock<MockTraceEventListener> first, second, ignored, other;
  auto& listener = *MultiplexTraceEventListener(&first, &second);
  EXPECT_CALL(second, Extract(&first))
      .WillOnce(Return(std::make_pair(&ignored, false)));
  EXPECT_CALL(first, Extract(&first))
      .WillOnce(Return(std::make_pair(&other, true)));
  auto [res, success] = listener.Extract(&first);
  EXPECT_TRUE(success);
  EXPECT_THAT(res, Eq(&listener));

  EXPECT_CALL(second, ReleaseEventListener());
  EXPECT_CALL(other, ReleaseEventListener());
  listener.ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, ExtractNotFound) {
  StrictMock<MockTraceEventListener> first, second, ignored, other;
  auto& listener = *MultiplexTraceEventListener(&first, &second);
  EXPECT_CALL(second, Extract(&other))
      .WillOnce(Return(std::make_pair(&ignored, false)));
  EXPECT_CALL(first, Extract(&other))
      .WillOnce(Return(std::make_pair(&ignored, false)));
  auto [res, success] = listener.Extract(&other);
  EXPECT_FALSE(success);
  EXPECT_THAT(res, Eq(&listener));

  EXPECT_CALL(second, ReleaseEventListener());
  EXPECT_CALL(first, ReleaseEventListener());
  listener.ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, Depth) {
  DefaultTraceEventListener first, second, third;
  auto* listener = MultiplexTraceEventListener(&first, &second);
  EXPECT_THAT(listener->Depth(), Eq(2));
  listener = MultiplexTraceEventListener(listener, &third);
  EXPECT_THAT(listener->Depth(), Eq(3));

  listener = listener->Extract(&third).first;
  EXPECT_THAT(listener->Depth(), Eq(2));
  listener = MultiplexTraceEventListener(listener, &third);
  EXPECT_THAT(listener->Depth(), Eq(3));

  listener = listener->Extract(&second).first;
  EXPECT_THAT(listener->Depth(), Eq(2));
  listener = MultiplexTraceEventListener(listener, &second);
  EXPECT_THAT(listener->Depth(), Eq(3));

  listener = listener->Extract(&first).first;
  EXPECT_THAT(listener->Depth(), Eq(2));
  listener = MultiplexTraceEventListener(listener, &first);
  EXPECT_THAT(listener->Depth(), Eq(3));

  listener->ReleaseEventListener();

  // Whitebox: MultiplexTraceEventListener will not re-balance
  // the tree at all until the depth exceeds 20.
  listener = nullptr;
  int depth = 1;
  std::vector<DefaultTraceEventListener> listeners(20);
  for (TraceEventListener& elem : listeners) {
    listener = MultiplexTraceEventListener(listener, &elem);
    EXPECT_THAT(listener->Depth(), Eq(depth));
    ++depth;
  }
  listener->ReleaseEventListener();
}

TEST(MultiplexTraceEventListener, ExtractAll) {
  TraceEventListener* listener = nullptr;
  std::vector<DefaultTraceEventListener> listeners(20);
  std::vector<TraceEventListener*> expected;
  for (TraceEventListener& elem : listeners) {
    expected.push_back(&elem);
    listener = MultiplexTraceEventListener(listener, &elem);
  }
  std::vector<TraceEventListener*> extracted;
  listener->ExtractAll(extracted);
  EXPECT_THAT(extracted, ElementsAreArray(expected));
}

TEST(MultiplexTraceEventListener, DepthRemainsAtOrBelow25) {
  TraceEventListener* listener = nullptr;
  std::vector<DefaultTraceEventListener> listeners(2000);
  std::vector<TraceEventListener*> expected;
  for (TraceEventListener& elem : listeners) {
    expected.push_back(&elem);
    listener = MultiplexTraceEventListener(listener, &elem);
    EXPECT_THAT(listener->Depth(), Le(25));
  }
  std::vector<TraceEventListener*> extracted;
  listener->ExtractAll(extracted);
  EXPECT_THAT(extracted, ElementsAreArray(expected));
}

}  // namespace
}  // namespace perftools::tracing
