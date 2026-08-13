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

#ifndef THIRD_PARTY_GLOOP_BASE_MOCK_TRACER_H_
#define THIRD_PARTY_GLOOP_BASE_MOCK_TRACER_H_

#include <string>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/tracer.h"
#include "gloop/base/tracing_types.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gmock/gmock.h"

namespace base {

// A mock implementation for base::Tracer.
// The mock implementation does not invoke `Ref` and `Unref`, meaning a mock
// can simply be created on the local stack. For example:
//
//   TEST(MyTest, MockExample) {
//     base::MockTracer tracer;
//     {
//       TraceContext tc;
//       perftools::tracing::testing::AdoptTracerForTesting(&tc, &tracer);
//       EXPECT_CALL(tracer.OnRefCountZero());
//     }
//   }
//
class MockTracer : public Tracer {
 public:
  // Constructs a `MockTracer` instance.
  // This constructor invokes `SetStartTimeNow()`, and installs the
  // following class method defaults:
  //   name()           --> Return("MockTracer");
  //   ToString()       --> Return("MockTracer");
  //   OnRefCountZero() --> Invoke(SetUnrefTimeNow())
  // To create a MockTracer that does not perform any initialization,
  // use the constructor accepting a `Raw` typed argument.
  MockTracer();

  // Raw constructor. This constructor performs none of the
  // initializations performed by the default constructor.
  struct Raw {};
  explicit MockTracer(Raw) noexcept {}

  MOCK_METHOD(std::string, name, (), (const, override));
  MOCK_METHOD(std::string, ToString, (), (const, override));

  MOCK_METHOD(void, SetMaxBytesToKeep, (int), (override));
  MOCK_METHOD(void, SetMaxBytesToKeepPerEntry, (int), (override));

  MOCK_METHOD(void, Attach, (base::TraceEntrySource*), (override));
  MOCK_METHOD(void, Detach, (base::TraceEntrySource*, bool), (override));
  MOCK_METHOD(void, EmitTraceEntrySources, (base::TraceEntrySink*, bool),
              (const, override));

  MOCK_METHOD(void, AttachTraceConsumer, (base::TraceConsumer*), (override));
  MOCK_METHOD(perftools::tracing::TraceBuffer*, GetAnnotationMap, (),
              (override));

  MOCK_METHOD(void, ChannelPrintFormattedStringImpl,
              (perftools::tracing::channels::ChannelID,
               base::TraceStringFormatter, absl::SourceLocation),
              (override));
  MOCK_METHOD(void, ChannelPrintLiteralImpl,
              (perftools::tracing::channels::ChannelID, const char*,
               absl::SourceLocation),
              (override));
  MOCK_METHOD(void, ChannelPrintStringViewImpl,
              (perftools::tracing::channels::ChannelID, absl::string_view,
               absl::SourceLocation),
              (override));
  MOCK_METHOD(void, ChannelPrintProtoImpl,
              (perftools::tracing::channels::ChannelID,
               const ::google::protobuf::Message&, absl::Time,
               perftools::tracing::TraceSourceLocation),
              (override));

  MOCK_METHOD(void, NotifyTraceConsumers, (), (override));
  MOCK_METHOD(void, OnRefCountZero, (), (override));

  // Make protected methods public for testing / mock purposes.
  using Tracer::add_child;
  using Tracer::add_children;
  using Tracer::enable_query_cost_tracing;
  using Tracer::num_children;
  using Tracer::Ref;
  using Tracer::set_high_value_trace;
  using Tracer::set_inherited_initiator_id;
  using Tracer::set_initiated_by_bucketed_sampling;
  using Tracer::set_initiated_by_tracing_cookie;
  using Tracer::set_initiator_id;
  using Tracer::set_initiator_id_string;
  using Tracer::set_invalid_inherited_initiator_id;
  using Tracer::set_inverse_sampling_probability;
  using Tracer::set_span_type;
  using Tracer::set_speculative_root;
  using Tracer::SetStartTime;
  using Tracer::SetStartTimeNow;
  using Tracer::SetStopTime;
  using Tracer::SetStopTimeNow;
  using Tracer::SetUnrefTime;
  using Tracer::SetUnrefTimeNow;
  using Tracer::tracing_initiator;
  using Tracer::Unref;
  using Tracer::UpdateMask;
};

inline MockTracer::MockTracer() {
  SetStartTimeNow();
  ON_CALL(*this, name()).WillByDefault(::testing::Return("MockTracer"));
  ON_CALL(*this, ToString()).WillByDefault(::testing::Return("MockTracer"));
  ON_CALL(*this, OnRefCountZero()).WillByDefault(::testing::Invoke([this] {
    SetUnrefTimeNow();
  }));
}

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_MOCK_TRACER_H_
