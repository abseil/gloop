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

// Tests for Tracer and related portions of TraceContext.

#include "gloop/base/tracer.h"

#include <string.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/base/context_access.h"
#include "gloop/base/mock_tracer.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/tracing_types.h"
#include "gloop/base/walltime.h"
#include "gloop/perftools/tracing/test_only_access.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/refcount/blocking_refcount.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Outside the anonymous namespace so that TracerTest can be a friend of
// MutableCurrentContext.
class TracerTest {
 public:
  static TraceContext* TraceContextMutableCurrent() {
    return ::base::internal::MutableCurrentContext::MutableCurrentTrace();
  }
};

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::base::Tracer;
using ::perftools::tracing::channels::ChannelID;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::Le;
using ::testing::Lt;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Ref;

const ChannelID TEST_CHANNEL_UNDEFINED = -1;
const ChannelID TEST_CHANNEL_1 = 1;

// The following two values are hard-coded to match
// //perftools/tracing/proto/channel_id.proto.  There are tests in
// //perftools/tracing/internal/tracebuffer_test that these constants
// are correct, but here we can't access the Tracer private fields and
// a dependency on //perftools/tracing/proto is unwelcome, so we
// hard-code these values.
const ChannelID TEST_CHANNEL_TEXT = 0;
const ChannelID TEST_CHANNEL_VERBOSE = 10;

// A mock Tracer that verifies printed annotations and provides
// deletion notification. If TestTracer is stack-allocated, calling
// SetStartTime() or SetStartTimeNow() is not necessary.
class TestTracer : public Tracer {
 public:
  // Sets *deleted to true when destructor runs.
  explicit TestTracer() {
    InternalInit(nullptr, nullptr, nullptr, nullptr, nullptr);
  }
  explicit TestTracer(bool* deleted) {
    CHECK(deleted);
    InternalInit(nullptr, deleted, nullptr, nullptr, nullptr);
  }

  // Sets *unref_time_ for testing that Unref() sets stop_time_, if not
  // NULL.  Sets *deleted to true when destructor runs.
  TestTracer(bool* deleted, absl::Time* unref_time) {
    CHECK(deleted);
    InternalInit(nullptr, deleted, nullptr, nullptr, nullptr);
    unref_time_ = unref_time;
  }

  // Sets *finished to true when the trace is finished.
  // Sets *deleted to true when destructor runs.
  // Initializes base Tracer class using the given TraceContext |tc|
  // and sets the trace probability to 1.
  TestTracer(bool* finished, bool* deleted, const TraceContext& tc)
      : Tracer(tc) {
    set_inverse_sampling_probability(1);
    CHECK(finished);
    CHECK(deleted);
    InternalInit(finished, deleted, nullptr, nullptr, nullptr);
  }

  // Sets *deleted to true when destructor runs.
  // If printed_literal is not NULL, sets *printed_literal to the channel iff
  // PrintLiteral() has been correctly called.
  // If printed_formatted is not NULL, sets *printed_formatted to the
  // channel iff PrintFormattedString() has been correctly called.
  TestTracer(bool* deleted, ChannelID* printed_literal,
             ChannelID* printed_string, ChannelID* printed_formatted) {
    CHECK(deleted);
    InternalInit(nullptr, deleted, printed_literal, printed_string,
                 printed_formatted);
  }

  ~TestTracer() override {
    if (deleted_) *deleted_ = true;
    if (unref_time_ != nullptr) {
      *unref_time_ = get_unref_time();
    }
  }

  using base::Tracer::kAdoptedInitiatorId;
  using base::Tracer::kHighValueTrace;
  using base::Tracer::kInitiatedByLinkContexts;
  using base::Tracer::kInitiatorTypeProdUid;
  using base::Tracer::kInitiatorValueMask;
  using base::Tracer::kNotSampled;
  using base::Tracer::kTraceInitiatingSpan;
  using base::Tracer::Ref;
  using base::Tracer::set_initiator_id;
  using base::Tracer::set_tracer_attributes;
  using base::Tracer::SetStartTime;
  using base::Tracer::SetStartTimeNow;
  using base::Tracer::SetUnrefTime;
  using base::Tracer::SetUnrefTimeNow;
  using base::Tracer::Unref;
  using base::Tracer::UnrefNoDelete;
  using base::Tracer::UpdateMask;

  // Sets the printed_formatted flag if we're given the right
  // PrintFormattedString() call.
  void ChannelPrintFormattedStringImpl(
      perftools::tracing::channels::ChannelID channel_id,
      base::TraceStringFormatter formatter, absl::SourceLocation) override {
    const int kMaxMessageLength = 200;
    char buf[kMaxMessageLength + 1];
    int n = formatter(buf, kMaxMessageLength);
    CHECK(n > 0 && n <= kMaxMessageLength);
    absl::string_view sv(buf, n);
    LOG(INFO) << "Printf(): " << sv;
    if (printed_formatted_ && sv == "here's a biscuit") {
      *printed_formatted_ = channel_id;
    }
  }

  // Sets the printed_literal flag if we're given the right literal.
  void ChannelPrintLiteralImpl(
      perftools::tracing::channels::ChannelID channel_id, const char* literal,
      absl::SourceLocation) override {
    LOG(INFO) << "PrintLiteral(): " << literal;
    if (printed_literal_ && !strcmp(literal, "this is a test")) {
      *printed_literal_ = channel_id;
    }
  }

  // Sets the printed_literal flag if we're given the right literal.
  void ChannelPrintStringViewImpl(
      perftools::tracing::channels::ChannelID channel_id, absl::string_view s,
      absl::SourceLocation) override {
    LOG(INFO) << "PrintString(): " << s;
    if (printed_string_ && s == "this is a string") {
      *printed_string_ = channel_id;
    }
  }

  void Attach(base::TraceEntrySource* source) override {}
  void Detach(base::TraceEntrySource* source, bool save_clone) override {}
  void EmitTraceEntrySources(base::TraceEntrySink* /*sink*/,
                             bool /*skip_unowned_sources*/) const override {}

  void SetMaxBytesToKeep(int n) override {}
  void SetMaxBytesToKeepPerEntry(int n) override {}

  std::string ToString() const override { return ""; }

  // Because this is an abstract class which does not implement or
  // manage the name in any way, we have nothing to test in this file.
  std::string name() const override { return ""; }

  perftools::tracing::TraceBuffer* GetAnnotationMap() override {
    return nullptr;
  }

  void TestSetSpanType(int32_t span_type) { set_span_type(span_type); }

  int32_t TestGetNumChildren() { return num_children(); }

  void TestAddChild() { add_child(); }

  void TestSetInverseSamplingProbability(double p) {
    set_inverse_sampling_probability(p);
  }

  bool TestGetTracingInitiator() const { return tracing_initiator(); }

  void TestSetInitiatorId() { set_initiator_id(); }

  void TestSetInvalidInheritedInitiatorId() {
    set_invalid_inherited_initiator_id();
  }

  void TestSetInheritedInitiatorId(uint64_t id) {
    set_inherited_initiator_id(id);
  }

  void TestSetInitiatorIdOnChildTrace(uint64_t id) {
    set_initiator_id_on_child_trace(id);
  }

  void TestEnableQueryCostTracing() { enable_query_cost_tracing(); }

  void TestSetHighValueTrace() { set_high_value_trace(); }

  void TestSetInitiatorIdString(uint32_t hash) {
    set_initiator_id_string(hash);
  }

  void AttachTraceConsumer(base::TraceConsumer* c) override {}

  void SetChildSampler(std::unique_ptr<base::ChildSampler> child_sampler,
                       ChildSamplerAccess) override {
    child_sampler_ = std::move(child_sampler);
  }
  base::ChildSampler* GetChildSampler() const override {
    return child_sampler_.get();
  }

 protected:
  void NotifyTraceConsumers() override {
    if (finished_) {
      *finished_ = true;
    }
  }

 private:
  void InternalInit(bool* finished, bool* deleted, ChannelID* printed_literal,
                    ChannelID* printed_string, ChannelID* printed_formatted) {
    finished_ = finished;
    deleted_ = deleted;
    printed_literal_ = printed_literal;
    printed_string_ = printed_string;
    printed_formatted_ = printed_formatted;
    unref_time_ = nullptr;

    if (finished_) CHECK(!(*finished_));
    if (deleted_) CHECK(!(*deleted_));
    if (printed_literal_) {
      CHECK_EQ(TEST_CHANNEL_UNDEFINED, *printed_literal_);
    }
    if (printed_string_) {
      CHECK_EQ(TEST_CHANNEL_UNDEFINED, *printed_string_);
    }
    if (printed_formatted_) {
      CHECK_EQ(TEST_CHANNEL_UNDEFINED, *printed_formatted_);
    }
  }

  bool* finished_;
  bool* deleted_;
  ChannelID* printed_literal_;
  ChannelID* printed_string_;
  ChannelID* printed_formatted_;
  absl::Time* unref_time_;
  std::unique_ptr<base::ChildSampler> child_sampler_;
};

TEST(Tracer, Size) {
#ifdef NDEBUG
  EXPECT_EQ(sizeof(Tracer), 112);
#endif
}

TEST(Tracer, SetStartTime) {
  bool deleted = false;
  TestTracer tracer(&deleted);

  absl::Time time = absl::Now();
  absl::Time min_time = time - absl::Microseconds(1);
  absl::Time max_time = time + absl::Microseconds(1);

  tracer.SetStartTime(time);
  EXPECT_TRUE(tracer.has_start_time());
  EXPECT_GE(tracer.get_start_time(), min_time);
  EXPECT_LE(tracer.get_start_time(), max_time);
}

TEST(Tracer, SetStartTimeNow) {
  bool deleted = false;
  TestTracer tracer(&deleted);

  absl::Time before = absl::Now();

  absl::SleepFor(absl::Microseconds(1));
  tracer.SetStartTimeNow();
  absl::SleepFor(absl::Microseconds(1));

  absl::Time after = absl::Now();

  EXPECT_TRUE(tracer.has_start_time());
  EXPECT_GE(tracer.get_start_time(), before);
  EXPECT_LE(tracer.get_start_time(), after);
}

TEST(Tracer, ManualStartSetUnrefTime) {
  bool deleted = false;
  absl::Time unref_time;
  TestTracer t(&deleted, &unref_time);

  EXPECT_FALSE(t.has_stop_time());
  EXPECT_FALSE(t.has_start_time());
  EXPECT_FALSE(t.has_unref_time());

  t.SetStartTimeNow();
  t.SetUnrefTimeNow();

  EXPECT_TRUE(t.has_stop_time());
  EXPECT_TRUE(t.has_start_time());
  EXPECT_TRUE(t.has_unref_time());

  EXPECT_GE(t.get_stop_time(), t.get_start_time());
}

TEST(Tracer, AutoStartStopUnrefTimeNow) {
  bool deleted = false;
  TestTracer tracer(&deleted);

  tracer.SetStartTimeNow();

  EXPECT_FALSE(tracer.has_stop_time());
  EXPECT_FALSE(tracer.has_unref_time());

  absl::Time before_stop_time = absl::Now();
  tracer.SetStopTimeNow();
  absl::Time after_stop_time = absl::Now();
  EXPECT_TRUE(tracer.has_stop_time());
  absl::Time stop_time = tracer.get_stop_time();

  // The cycle time set by SetStopTimeNow is truncated. get_stop_time returned
  // absl::Time can be slightly smaller, and more so when
  // CycleClock::Frequency() is low.
  EXPECT_GE(stop_time, before_stop_time - absl::Microseconds(1));
  EXPECT_LE(stop_time, after_stop_time);

  // No effect for second call to SetStopTimeNow()
  tracer.SetStopTimeNow();
  tracer.SetStopTime(absl::Now());
  EXPECT_EQ(tracer.get_stop_time(), stop_time);
}

TEST(Tracer, TraceChannels) {
  bool tracer_deleted = false;
  ChannelID printed_literal = TEST_CHANNEL_UNDEFINED;
  ChannelID printed_string = TEST_CHANNEL_UNDEFINED;
  ChannelID printed_formatted = TEST_CHANNEL_UNDEFINED;
  TestTracer tracer(&tracer_deleted, &printed_literal, &printed_string,
                    &printed_formatted);

  // default (legacy) methods
  tracer.PrintLiteral("this is a test");
  EXPECT_EQ(TEST_CHANNEL_TEXT, printed_literal);
  // NOLINTNEXTLINE: intentionally using PrintString with a literal
  tracer.PrintString("this is a string");
  EXPECT_EQ(TEST_CHANNEL_TEXT, printed_string);
  tracer.Printf("here's a %s", "biscuit");
  EXPECT_EQ(TEST_CHANNEL_TEXT, printed_formatted);

  // text() methods
  tracer.text().PrintLiteral("this is a test");
  EXPECT_EQ(TEST_CHANNEL_TEXT, printed_literal);
  tracer.text().PrintString("this is a string");
  EXPECT_EQ(TEST_CHANNEL_TEXT, printed_string);
  tracer.text().Printf("here's a %s", "biscuit");
  EXPECT_EQ(TEST_CHANNEL_TEXT, printed_formatted);

  // verbose() methods
  tracer.verbose().PrintLiteral("this is a test");
  EXPECT_EQ(TEST_CHANNEL_VERBOSE, printed_literal);
  tracer.verbose().PrintString("this is a string");
  EXPECT_EQ(TEST_CHANNEL_VERBOSE, printed_string);
  tracer.verbose().Printf("here's a %s", "biscuit");
  EXPECT_EQ(TEST_CHANNEL_VERBOSE, printed_formatted);

  // channel() methods
  tracer.channel(TEST_CHANNEL_1).PrintLiteral("this is a test");
  EXPECT_EQ(TEST_CHANNEL_1, printed_literal);
  tracer.channel(TEST_CHANNEL_1).PrintString("this is a string");
  EXPECT_EQ(TEST_CHANNEL_1, printed_string);
  tracer.channel(TEST_CHANNEL_1).Printf("here's a %s", "biscuit");
  EXPECT_EQ(TEST_CHANNEL_1, printed_formatted);
}

#ifndef NDEBUG
// These death tests are only enabled for debug builds, because they
// are RAW_DCHECKS.
TEST(Tracer, MultipleSetUnrefTimeDeath) {
  bool deleted = false;
  TestTracer t(&deleted);

  t.SetStartTimeNow();
  t.SetUnrefTimeNow();

  EXPECT_DEATH({ t.SetUnrefTime(absl::Now()); }, "Unref time already set");
  EXPECT_DEATH({ t.SetUnrefTimeNow(); }, "Unref time already set");
}

TEST(Tracer, NoStartStopDeath) {
  bool deleted = false;
  TestTracer t(&deleted);

  EXPECT_DEATH(
      { VLOG(0) << base::ToWallTime(t.get_start_time()); },
      "Start time is not set");

  EXPECT_DEATH({ VLOG(0) << t.get_start_time(); }, "Start time is not set");

  t.SetStartTimeNow();

  EXPECT_DEATH(
      { VLOG(0) << base::ToWallTime(t.get_stop_time()); },
      "Stop time is not set");

  EXPECT_DEATH({ VLOG(0) << t.get_stop_time(); }, "Stop time is not set");
}
#endif

class ErrorTracer : public TestTracer {
 public:
  using TestTracer::TestTracer;

  void SetErrorStatus() override {
    error_status_.store(true, std::memory_order_relaxed);
  }

  bool GetErrorStatus() const override {
    return error_status_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> error_status_ = false;
};

TEST(Tracer, DefaultStatusIsOkWhenGetErrorStatusIsFalse) {
  ErrorTracer tracer;
  EXPECT_THAT(tracer.status(), IsOk());
}

TEST(Tracer, DefaultStatusIsUnknownWhenErrorStatusIsTrue) {
  ErrorTracer tracer;
  tracer.SetErrorStatus();
  EXPECT_THAT(tracer.status(), StatusIs(absl::StatusCode::kUnknown));
}

TEST(Tracer, DefaultSetStatusOk) {
  ErrorTracer tracer;
  tracer.set_status(absl::OkStatus());
  EXPECT_THAT(tracer.status(), IsOk());
  EXPECT_FALSE(tracer.GetErrorStatus());
}

TEST(Tracer, SetErrorStatus) {
  ErrorTracer tracer;
  tracer.set_status(absl::DeadlineExceededError("too slow"));
  // The default implementation can only return OK or kUnknown based on the
  // result of `GetErrorStatus()`.
  EXPECT_THAT(tracer.status(), StatusIs(absl::StatusCode::kUnknown));
  EXPECT_TRUE(tracer.GetErrorStatus());
}

TEST(Tracer, InitiatorIdOfChildTrace) {
  TestTracer tracer;
  // Set a lot of bits but not the most-significant one. Clear the "initiating
  // span" and "linked trace" bits to see that they are set by
  // `TestSetInitiatorIdOnChildTrace`.
  constexpr uint64_t kInitiatorBits = 0x7FFFFFFF00012345 &
                                      ~TestTracer::kTraceInitiatingSpan &
                                      ~TestTracer::kInitiatedByLinkContexts;

  tracer.TestSetInitiatorIdOnChildTrace(kInitiatorBits);
  EXPECT_EQ(tracer.initiator_id(), kInitiatorBits |
                                       TestTracer::kTraceInitiatingSpan |
                                       TestTracer::kInitiatedByLinkContexts);
}

TEST(Tracer, UnrefNoDeleteRefcountGreaterThanOne) {
  bool deleted = false;
  TestTracer* tracer = new TestTracer(&deleted);
  tracer->SetStartTimeNow();

  int owners[2] = {0, 0};
  void* owner1 = &owners[0];
  void* owner2 = &owners[1];

  tracer->Ref(owner1);
  tracer->Ref(owner2);

  std::unique_ptr<Tracer> returned_tracer = tracer->UnrefNoDelete(owner1);

  EXPECT_THAT(returned_tracer, IsNull());
  EXPECT_FALSE(deleted);
  EXPECT_THAT(tracer->RefCountForTesting(), Eq(1));

  tracer->Unref(owner2);
  EXPECT_TRUE(deleted);
}

TEST(Tracer, UnrefNoDeleteRefcountEqualsOneWithUnrefTime) {
  bool deleted = false;
  TestTracer* tracer = new TestTracer(&deleted);
  tracer->SetStartTimeNow();

  int owner_val = 0;
  void* owner = &owner_val;
  tracer->Ref(owner);
  tracer->SetUnrefTimeNow();

  std::unique_ptr<Tracer> returned_tracer = tracer->UnrefNoDelete(owner);

  ASSERT_THAT(returned_tracer, NotNull());
  EXPECT_THAT(returned_tracer.get(), Eq(tracer));
  EXPECT_FALSE(deleted);

  returned_tracer.reset();
  EXPECT_TRUE(deleted);
}

void BM_ElapsedSeconds(benchmark::State& state) {
  bool deleted = false;
  TestTracer tracer(&deleted);
  tracer.SetStartTimeNow();
  for (auto _ : state) {
    tracer.ElapsedSeconds();
  }
}
BENCHMARK(BM_ElapsedSeconds);

void BM_TracerInit(benchmark::State& state) {
  for (auto s : state) {
    bool deleted = false;
    TestTracer tracer(&deleted);
    benchmark::DoNotOptimize(tracer);
  }
}
BENCHMARK(BM_TracerInit)->ThreadPerCpu();

}  // namespace

// standard main() provided by gunit runs all tests and benchmarks.
