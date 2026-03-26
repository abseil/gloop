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

// This class tests tracecontext.{cc,h}
// It also tests the tracecontext-specific parts of NewCallback().
// Tracer-related tests are in tracer_test.cc.

#include "gloop/base/tracecontext.h"

#include <stdlib.h>

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>  // IWYU pragma: keep

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"      // IWYU pragma: keep
#include "absl/debugging/symbolize.h"  // IWYU pragma: keep
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/flags.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"  // IWYU pragma: keep
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/base/context_access.h"
#include "gloop/base/context_access_for_testing.h"
#include "gloop/base/context_origin.h"
#include "gloop/base/init_google.h"
#include "gloop/base/reference_tracker.h"  // IWYU pragma: keep
#include "gloop/base/tracer.h"
#include "gloop/base/tracing_types.h"
#include "gloop/perftools/tracing/internal/skeletal_tracing_access.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/public/adopt_tracer_for_testing.h"
#include "gloop/perftools/tracing/public/test_utils.h"
#include "gloop/perftools/tracing/public/tracecontext_util.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/gtl/unique_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stats/census/public/census-interface.h"
#include "stats/census/public/maybe_key_id.h"
#include "stats/census/public/tagger.h"
#include "util/functional/to_callback.h"

namespace {
using perftools::tracing::MockTraceEventListener;
using perftools::tracing::SkeletalTracingAccess;
using perftools::tracing::TraceContextSwitcher;
using perftools::tracing::testing::TestBaseTracer;

using ::testing::_;
using ::testing::ElementsAreArray;  // NOLINT: keep
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Optional;  // NOLINT: keep
using ::testing::Return;
using ::testing::UnorderedElementsAreArray;
using ::testing::status::StatusIs;

static uint64_t random64() {
  uint64_t x = random();
  x = (x << 30) ^ random();
  x = (x << 30) ^ random();
  return x;
}

TEST(TraceContextTest, DefaultConstruct) {
  TraceContext default_tc;
  EXPECT_THAT(default_tc.rpc_id(), Eq(0));
}

TEST(TraceContextTest, ConstructFromCurrentThread) {
  stats_census::Tagger tagger({{"k1", "v1"}});
  TraceContext thread_tc(TraceContext::kThread);
  EXPECT_THAT(stats_census::GetTagValue(thread_tc.census_handle(),
                                        stats_census::MaybeKeyId("k1")),
              Eq("v1"));
}

TEST(TraceContextTest, CreateAccessors) {
  for (int i = 0; i != 1000; i++) {
    uint64_t x = random64();
    uint64_t y = random64();
    uint64_t g = random64();
    uint32_t m = random();
    TraceContext tc(x, y, g, m);
    EXPECT_EQ(x, tc.rpc_id());
    EXPECT_EQ(y, tc.parent_rpc_id());
    EXPECT_EQ(g, tc.global_id());
    EXPECT_EQ(m, tc.mask());
    EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
    TraceContext c(TraceContext::kThread);
    EXPECT_EQ(0, c.rpc_id());
    EXPECT_EQ(0, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(0, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_mask(m);
    EXPECT_EQ(0, c.rpc_id());
    EXPECT_EQ(0, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_rpc_id(x);
    EXPECT_EQ(x, c.rpc_id());
    EXPECT_EQ(0, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_parent_rpc_id(y);
    EXPECT_EQ(x, c.rpc_id());
    EXPECT_EQ(y, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_global_id(g);
    EXPECT_EQ(x, c.rpc_id());
    EXPECT_EQ(y, c.parent_rpc_id());
    EXPECT_EQ(g, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
  }
}

TEST(TraceContextTest, CreateAccessorsLegacy) {
  for (int i = 0; i != 1000; i++) {
    uint64_t x = random64();
    uint64_t y = random64();
    uint64_t g = random64();
    uint32_t m = random();
    TraceContext tc(x, y, g, m);
    EXPECT_EQ(x, tc.rpc_id());
    EXPECT_EQ(y, tc.parent_rpc_id());
    EXPECT_EQ(g, tc.global_id());
    EXPECT_EQ(m, tc.mask());
    EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
    TraceContext c(TraceContext::kThread);
    EXPECT_EQ(0, c.rpc_id());
    EXPECT_EQ(0, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(0, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_mask(m);
    EXPECT_EQ(0, c.rpc_id());
    EXPECT_EQ(0, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_rpc_id(x);
    EXPECT_EQ(x, c.rpc_id());
    EXPECT_EQ(0, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_parent_rpc_id(y);
    EXPECT_EQ(x, c.rpc_id());
    EXPECT_EQ(y, c.parent_rpc_id());
    EXPECT_EQ(0, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
    c.set_global_id(g);
    EXPECT_EQ(x, c.rpc_id());
    EXPECT_EQ(y, c.parent_rpc_id());
    EXPECT_EQ(g, c.global_id());
    EXPECT_EQ(m, c.mask());
    EXPECT_THAT(c.initiator_id(), testing::Eq(std::nullopt));
  }
}

TEST(TraceContextTest, PerThread) {
  for (int i = 0; i != 1000; i++) {
    uint64_t x = random64();
    uint64_t y = random64();
    uint64_t g = random64();
    uint32_t m = random();
    TraceContext tc(x, y, g, m);
    EXPECT_EQ(x, tc.rpc_id());
    EXPECT_EQ(y, tc.parent_rpc_id());
    EXPECT_EQ(g, tc.global_id());
    EXPECT_EQ(m, tc.mask());
    EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
    TraceContextSwitcher with(tc);
    EXPECT_EQ(x, TraceContext::Current()->rpc_id());
    EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
    EXPECT_EQ(g, TraceContext::Current()->global_id());
    EXPECT_EQ(m, TraceContext::Current()->mask());
    EXPECT_THAT(TraceContext::Current()->initiator_id(),
                testing::Eq(std::nullopt));
    tc.set_rpc_id(0);
    tc.set_parent_rpc_id(0);
    tc.set_global_id(0);
    tc.set_mask(0);
    EXPECT_EQ(0, tc.rpc_id());
    EXPECT_EQ(0, tc.parent_rpc_id());
    EXPECT_EQ(0, tc.global_id());
    EXPECT_EQ(0, tc.mask());
    EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
    tc.FromThread();
    EXPECT_EQ(x, tc.rpc_id());
    EXPECT_EQ(y, tc.parent_rpc_id());
    EXPECT_EQ(g, tc.global_id());
    EXPECT_EQ(m, tc.mask());
    EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
  }
}

class O {
 public:
  O() {}
  ~O() {}

  void Ov10(uint64_t x) { EXPECT_EQ(x, TraceContext::Current()->rpc_id()); }
  void Ov20(uint64_t x, uint64_t y) {
    EXPECT_EQ(x, TraceContext::Current()->rpc_id());
    EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
  }
  void Ov30(uint64_t x, uint64_t y, uint32_t m) {
    EXPECT_EQ(x, TraceContext::Current()->rpc_id());
    EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
    EXPECT_EQ(m, TraceContext::Current()->mask());
  }
  void Ov50(uint64_t x, uint64_t y, uint32_t m, uint64_t g, uint32_t b) {
    EXPECT_EQ(x, TraceContext::Current()->rpc_id());
    EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
    EXPECT_EQ(m, TraceContext::Current()->mask());
    EXPECT_EQ(g, TraceContext::Current()->global_id());
    EXPECT_EQ(b, ~m);
  }
  void Ov60(uint64_t x, uint64_t y, uint32_t m, uint64_t g) {
    EXPECT_EQ(x, TraceContext::Current()->rpc_id());
    EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
    EXPECT_EQ(m, TraceContext::Current()->mask());
    EXPECT_EQ(g, TraceContext::Current()->global_id());
  }
};

static void v10(uint64_t x) { EXPECT_EQ(x, TraceContext::Current()->rpc_id()); }
static void v20(uint64_t x, uint64_t y) {
  EXPECT_EQ(x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
}
static void v30(uint64_t x, uint64_t y, uint32_t m) {
  EXPECT_EQ(x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(m, TraceContext::Current()->mask());
}
static void v50(uint64_t x, uint64_t y, uint32_t m, uint64_t g, uint32_t b) {
  EXPECT_EQ(x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(m, TraceContext::Current()->mask());
  EXPECT_EQ(g, TraceContext::Current()->global_id());
  EXPECT_EQ(b, ~m);
}
static void v60(uint64_t x, uint64_t y, uint32_t m, uint64_t g) {
  EXPECT_EQ(x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(m, TraceContext::Current()->mask());
  EXPECT_EQ(g, TraceContext::Current()->global_id());
}

static void Do(Closure* cb, uint32_t m, uint64_t x, uint64_t y, uint64_t g) {
  // check precondition
  EXPECT_EQ(m, TraceContext::Current()->mask());
  EXPECT_EQ(x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(g, TraceContext::Current()->global_id());
  // modify state
  TraceContext tc;  // All zero values
  TraceContextSwitcher with(std::move(tc));
  // check that the non-permanent callback holds the right stored context.
  EXPECT_TRUE(!cb->IsRepeatable());
  EXPECT_EQ(m, cb->context_ptr()->trace()->mask());
  EXPECT_EQ(x, cb->context_ptr()->trace()->rpc_id());
  EXPECT_EQ(y, cb->context_ptr()->trace()->parent_rpc_id());
  EXPECT_EQ(g, cb->context_ptr()->trace()->global_id());
  // run callback -- it checks its own internal conditions
  cb->Run();
  // check modified state
  EXPECT_EQ(0, TraceContext::Current()->mask());
  EXPECT_EQ(0, TraceContext::Current()->rpc_id());
  EXPECT_EQ(0, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(0, TraceContext::Current()->global_id());
}

static void DoPerm(Closure* cb, uint32_t m, uint64_t x, uint64_t y,
                   uint64_t g) {
  // check precondition
  EXPECT_EQ(m, TraceContext::Current()->mask());
  EXPECT_EQ(x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(y, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(g, TraceContext::Current()->global_id());
  // modify state
  TraceContext tc;
  tc.set_mask(~m);
  tc.set_rpc_id(~x);
  tc.set_parent_rpc_id(~y);
  tc.set_global_id(~g);
  TraceContextSwitcher with(std::move(tc));
  // check that a permanent callback always holds an empty trace context.
  EXPECT_TRUE(cb->IsRepeatable());
  EXPECT_EQ(0, cb->context_ptr()->trace()->mask());
  EXPECT_EQ(0, cb->context_ptr()->trace()->rpc_id());
  EXPECT_EQ(0, cb->context_ptr()->trace()->parent_rpc_id());
  EXPECT_EQ(0, cb->context_ptr()->trace()->global_id());
  // run callback -- it checks its own internal conditions
  cb->Run();
  delete cb;
  // check modified state
  EXPECT_EQ(~m, TraceContext::Current()->mask());
  EXPECT_EQ(~x, TraceContext::Current()->rpc_id());
  EXPECT_EQ(~y, TraceContext::Current()->parent_rpc_id());
  EXPECT_EQ(~g, TraceContext::Current()->global_id());
}

TEST(TraceContextTest, Callback) {
  O o;
  for (int i = 0; i != 1000; i++) {
    uint64_t x = random64();
    uint64_t y = random64();
    uint64_t g = random64();
    uint32_t m = random();
    TraceContext tc;
    tc.set_mask(m);
    tc.set_global_id(g);
    tc.set_rpc_id(x);
    tc.set_parent_rpc_id(y);
    TraceContextSwitcher with(std::move(tc));
    Do(::util::functional::ToCallback([x] { v10(x); }), m, x, y, g);
    Do(::util::functional::ToCallback(absl::bind_front(&v20, x, y)), m, x, y,
       g);
    Do(::util::functional::ToCallback(absl::bind_front(&v30, x, y, m)), m, x, y,
       g);
    Do(::util::functional::ToCallback(absl::bind_front(&v50, x, y, m, g, ~m)),
       m, x, y, g);
    Do(::util::functional::ToCallback(absl::bind_front(&v60, x, y, m, g)), m, x,
       y, g);
    Do(::util::functional::ToCallback(absl::bind_front(&O::Ov10, &o, x)), m, x,
       y, g);
    Do(::util::functional::ToCallback(absl::bind_front(&O::Ov20, &o, x, y)), m,
       x, y, g);
    Do(::util::functional::ToCallback(absl::bind_front(&O::Ov30, &o, x, y, m)),
       m, x, y, g);
    Do(::util::functional::ToCallback(
           absl::bind_front(&O::Ov50, &o, x, y, m, g, ~m)),
       m, x, y, g);
    Do(::util::functional::ToCallback(
           absl::bind_front(&O::Ov60, &o, x, y, m, g)),
       m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(absl::bind_front(&v10, ~x)),
           m, x, y, g);
    DoPerm(
        ::util::functional::ToPermanentCallback(absl::bind_front(&v20, ~x, ~y)),
        m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(
               absl::bind_front(&v30, ~x, ~y, ~m)),
           m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(
               absl::bind_front(&v50, ~x, ~y, ~m, ~g, m)),
           m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(
               absl::bind_front(&O::Ov10, &o, ~x)),
           m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(
               absl::bind_front(&O::Ov20, &o, ~x, ~y)),
           m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(
               absl::bind_front(&O::Ov30, &o, ~x, ~y, ~m)),
           m, x, y, g);
    DoPerm(::util::functional::ToPermanentCallback(
               absl::bind_front(&O::Ov50, &o, ~x, ~y, ~m, ~g, m)),
           m, x, y, g);
  }
}

/////////////////////////////////////////////////////////////////////////
// Test that the signal-safe accessors can still get the data they need.

void CheckSafeContextAvailable() {
  // The moment this function is called, the callback propagation should
  // make sure that the thread's context is available, even if we're already
  // in a signal handler.
  const TraceContext* c = base::CurrentTraceContextNoAlloc();
  ASSERT_TRUE(c != nullptr);
}

// Don't use a ClosureThread for this -- they want permanent callbacks, which
// have the wrong propagation properties for us
class TestSafeAccessThread : public Thread {
 public:
  TestSafeAccessThread()
      : Thread(thread::Options().set_joinable(true), "TestSafeAccessThread"),
        closure_(::util::functional::ToCallback(&CheckSafeContextAvailable)) {
    Start();
    Join();
  }

 protected:
  void Run() override { closure_->Run(); }

 private:
  Closure* closure_;
};

TEST(TraceContextTest, SafeAccess) {
  // Set this off in a separate thread, using the callback mechanism so
  // that our context should propagate
  TestSafeAccessThread t;
}

// Wraps a permanent callback with a TraceContext.
class MyClosure : public Closure {
 public:
  // Takes ownership of "perm_callback".
  explicit MyClosure(Closure* perm_callback)
      : Closure(base::Context::kThread), perm_callback_(perm_callback) {}

  ~MyClosure() override { delete perm_callback_; }

  void Run() {
    TraceContextSwitcher with(*context_ptr()->trace());
    perm_callback_->Run();
  }

 private:
  Closure* perm_callback_;
};

TEST(TraceContextTest, UserDefinedCallback) {
  O o;
  CurrentTraceContext current;
  for (int i = 0; i != 1000; i++) {
    uint64_t x = random64();
    uint64_t y = random64();
    uint64_t g = random64();
    uint32_t m = random();
    auto perm = absl::bind_front(&v60, x, y, m, g);
    TraceContext tc;
    tc.set_mask(m);
    tc.set_global_id(g);
    tc.set_rpc_id(x);
    tc.set_parent_rpc_id(y);
    std::optional<TraceContextSwitcher> with(std::move(tc));
    MyClosure myc(::util::functional::ToPermanentCallback(perm));
    with = std::nullopt;
    myc.Run();
  }
}

TEST(TraceContextTest, DebugString) {
  TraceContext tc(1, 2, 3, 4);
  EXPECT_EQ(
      "[rpc_id: 0x0000000000000001, parent_rpc_id: 0x0000000000000002, "
      "global_id: 0x0000000000000003, mask: 0x00000004]",
      tc.DebugString());
}

TEST(TraceContextTest, SignalSafeDebugString) {
  TraceContext tc(1, 2, 3, 4);
  char short_out[16];
  int size = tc.SignalSafeDebugString(short_out, 16);
  EXPECT_LT(16, size);
  EXPECT_STREQ("[rpc_id: 0x0000", short_out);
  auto long_out = gtl::MakeUniqueArrayForOverwrite<char>(size + 1);
  int new_size = tc.SignalSafeDebugString(long_out.data(), size + 1);
  EXPECT_EQ(size, new_size);
  EXPECT_STREQ(
      "[rpc_id: 0x0000000000000001, parent_rpc_id: 0x0000000000000002, "
      "global_id: 0x0000000000000003, mask: 0x00000004]",
      long_out.data());
}

// The TraceLevel test is friended by TraceContext, so bring things back to the
// global namespace.
}  // namespace

TEST(TraceContextTest, TraceLevel) {
  TraceContext tc(1, 2, 3,
                  TraceContext::kTraceMaskLayerLocalSamplingOn |
                      TraceContext::kTraceMaskSkeletalTracingOn);
  EXPECT_EQ(TraceContext::kNoTracing, tc.GetTraceLevel());
  // Change from kNoTracing to kTransientTracing.
  tc.SetTraceLevel(TraceContext::kTransientTracing);
  EXPECT_EQ(TraceContext::kTransientTracing, tc.GetTraceLevel());
  // Change from kTransientTracing to kPersistentTracing.
  tc.SetTraceLevel(TraceContext::kPersistentTracing);
  EXPECT_EQ(TraceContext::kPersistentTracing, tc.GetTraceLevel());
  // Change from kPersistentTracing to kTransientTracing.
  tc.SetTraceLevel(TraceContext::kTransientTracing);
  EXPECT_EQ(TraceContext::kTransientTracing, tc.GetTraceLevel());
  // Change from kTransientTracing to kNoTracing.
  tc.SetTraceLevel(TraceContext::kNoTracing);
  EXPECT_EQ(TraceContext::kNoTracing, tc.GetTraceLevel());
  // Change from kNoTracing to kPersistentTracing.
  tc.SetTraceLevel(TraceContext::kNoTracing);
  tc.SetTraceLevel(TraceContext::kPersistentTracing);
  // Change from kPersistentTracing to kNoTracing.
  tc.SetTraceLevel(TraceContext::kNoTracing);
  EXPECT_EQ(TraceContext::kNoTracing, tc.GetTraceLevel());
}

TEST(TraceContextTest, AbslParseFlagAndUnparseFlagForTraceLevel) {
  TraceContext::TraceLevel level;
  AbslParseFlag("NO_TRACING", &level, nullptr);
  EXPECT_EQ(level, TraceContext::kNoTracing);
  AbslParseFlag("TRANSIENT_TRACING", &level, nullptr);
  EXPECT_EQ(level, TraceContext::kTransientTracing);
  AbslParseFlag("PERSISTENT_TRACING", &level, nullptr);
  EXPECT_EQ(level, TraceContext::kPersistentTracing);

  EXPECT_THAT(AbslUnparseFlag(TraceContext::kNoTracing),
              testing::StrEq("NO_TRACING"));
  EXPECT_THAT(AbslUnparseFlag(TraceContext::kTransientTracing),
              testing::StrEq("TRANSIENT_TRACING"));
  EXPECT_THAT(AbslUnparseFlag(TraceContext::kPersistentTracing),
              testing::StrEq("PERSISTENT_TRACING"));
}

namespace {
// context: b/133460096
TEST(TraceContextTest,
     MaskInForkedTraceContextNotChangedAfterAbandoningTracer) {
  // Use a bit that's currently not being used for anything, so we can test that
  // the mask is properly preserved across adoption/abandonment of tracers.
  constexpr int kUnusedMask = 0x20;
  TraceContext tc(1, 2, 3, kUnusedMask);
  perftools::tracing::testing::AdoptTracer(&tc, new TestBaseTracer);

  ASSERT_EQ(tc.mask(), kUnusedMask);

  // Each context has its own mask_, but shares the same tracer.trace_mask_.
  TraceContext copy(tc);

  tc.UpdateMask(0x1, 0x0);
  ASSERT_EQ(tc.mask(), kUnusedMask | 0x1);
  perftools::tracing::testing::AbandonTracer(&tc);
  ASSERT_EQ(tc.mask(), kUnusedMask | 0x1);

  perftools::tracing::testing::AbandonTracer(&copy);
  ASSERT_EQ(copy.mask(), kUnusedMask | 0x1);
}

TEST(TraceContextTest, TraceInitiatorId) {
  constexpr int kUnusedMask = 0x20;
  TraceContext tc(1, 2, 3, kUnusedMask);
  auto* test_tracer = new TestBaseTracer;
  test_tracer->set_inherited_initiator_id(1234);
  perftools::tracing::testing::AdoptTracer(&tc, test_tracer);
  ASSERT_EQ(tc.mask(), kUnusedMask);
  EXPECT_FALSE(tc.is_traced());
  EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));

  // The copy context shares the tracer.
  TraceContext copy(tc);

  tc.UpdateMask(0x1, 0x0);
  ASSERT_EQ(tc.mask(), kUnusedMask | 0x1);
  EXPECT_TRUE(tc.is_traced());
  EXPECT_EQ(tc.initiator_id(), 1234);
  ASSERT_EQ(copy.mask(), kUnusedMask | 0x1);
  EXPECT_TRUE(copy.is_traced());
  EXPECT_EQ(copy.initiator_id(), 1234);

  perftools::tracing::testing::AbandonTracer(&tc);
  ASSERT_EQ(tc.mask(), kUnusedMask | 0x1);
  // No more access to Tracer, unable to retrieve initiator ID.
  EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));

  // The copy still has the same Tracer attached, remembering initiator ID.
  EXPECT_EQ(copy.initiator_id(), 1234);

  perftools::tracing::testing::AbandonTracer(&copy);
  ASSERT_EQ(copy.mask(), kUnusedMask | 0x1);
  // No more access to Tracer, unable to retrieve initiator ID.
  EXPECT_THAT(copy.initiator_id(), testing::Eq(std::nullopt));
}

namespace {
// To make the a = std::move(a) self-move-assignment test slightly more
// realistic and avoid lint errors about use after move, we introduce a helper
// function that is called with lhs and rhs pointing to the same object.
void UnexpectedSelfMoveAssignment(TraceContext& lhs, TraceContext& rhs) {
  lhs = std::move(rhs);
}
}  // namespace

TEST(TraceContextTest, SelfMoveSwap) {
  TraceContext tc(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
  base::Tracer* tracer = new TestBaseTracer;
  perftools::tracing::testing::AdoptTracer(&tc, tracer);

  EXPECT_TRUE(tc.CanRecordAnnotations());
  EXPECT_EQ(tc.tracer(), tracer);

  TraceContext other_tc;

  EXPECT_NE(other_tc.tracer(), tracer);

  // Check normal swap works
  using std::swap;
  swap(tc, other_tc);

  EXPECT_NE(tc.tracer(), tracer);
  EXPECT_EQ(other_tc.tracer(), tracer);
  EXPECT_FALSE(tracer->has_unref_time());

  // Check that self-swap works with std::swap
  swap(other_tc, other_tc);

  EXPECT_EQ(other_tc.tracer(), tracer);
  EXPECT_FALSE(tracer->has_unref_time());

  // This call is allowed to leave the other_tc in an undefined state.
  UnexpectedSelfMoveAssignment(other_tc, other_tc);

  EXPECT_FALSE(tracer->has_unref_time());
  EXPECT_EQ(other_tc.tracer(), tracer);
}

// A dummy tracer which does nothing.
class DummyTracer : public base::Tracer {
 public:
  // If destroy_count is not null, *destroy_count will be incremented in the
  // destructor.
  explicit DummyTracer(int* destroy_count = nullptr)
      : destroy_count_(destroy_count) {
    SetStartTimeNow();
  }
  ~DummyTracer() override {
    if (destroy_count_) {
      ++*destroy_count_;
    }
  }
  int32_t RefCountForTesting() const { return Tracer::RefCountForTesting(); }

  void Attach(base::TraceEntrySource* source) override {}
  void Detach(base::TraceEntrySource* source, bool save_clone) override {}
  void SetMaxBytesToKeep(int n) override {}
  void SetMaxBytesToKeepPerEntry(int n) override {}
  std::string ToString() const override { return "DummyTracer"; }
  void EmitTraceEntrySources(base::TraceEntrySink* /*sink*/,
                             bool /*skip_unowned_sources*/) const override {}
  std::string name() const override { return "DummyTracer"; }
  perftools::tracing::AnnotationMap* GetAnnotationMap() override {
    return nullptr;
  }
  void AttachTraceConsumer(base::TraceConsumer* c) override {}

 protected:
  void ChannelPrintFormattedStringImpl(perftools::tracing::channels::ChannelID,
                                       base::TraceStringFormatter,
                                       absl::SourceLocation) override {}
  void NotifyTraceConsumers() override {}
  void ChannelPrintLiteralImpl(perftools::tracing::channels::ChannelID,
                               const char*, absl::SourceLocation) override {}
  void ChannelPrintStringViewImpl(perftools::tracing::channels::ChannelID,
                                  absl::string_view,
                                  absl::SourceLocation) override {}

 private:
  int* destroy_count_;
};

TEST(TraceContextTest, Moved) {
  uint64_t rpc_id = 1;
  uint64_t parent_rpc_id = 2;
  uint64_t global_id = 3;
  uint32_t mask = TraceContext::kTraceMaskTransientTracingOn;

  TraceContext moved_tc;
  {
    TraceContext tc(rpc_id, parent_rpc_id, global_id, mask);
    perftools::tracing::testing::AdoptTracer(&tc, new DummyTracer());
    moved_tc = std::move(tc);
  }

  // Move the original tc to a new one, and verify contents after destruction.
  EXPECT_EQ(moved_tc.rpc_id(), rpc_id);
  EXPECT_EQ(moved_tc.parent_rpc_id(), parent_rpc_id);
  EXPECT_EQ(moved_tc.global_id(), global_id);
  EXPECT_EQ(moved_tc.mask(), mask);
  ASSERT_NE(moved_tc.tracer(), nullptr);
  EXPECT_EQ(moved_tc.tracer()->name(), "DummyTracer");

  // Now move it back, and verify the content.
  TraceContext tc{std::move(moved_tc)};
  EXPECT_EQ(tc.rpc_id(), rpc_id);
  EXPECT_EQ(tc.parent_rpc_id(), parent_rpc_id);
  EXPECT_EQ(tc.global_id(), global_id);
  EXPECT_EQ(tc.mask(), mask);
  ASSERT_NE(tc.tracer(), nullptr);
  EXPECT_EQ(tc.tracer()->name(), "DummyTracer");
}

TEST(TraceContextTest, CopyConstructorAddsReferenceToTracer) {
  TraceContext tc;
  auto* tracer = new TestBaseTracer;
  perftools::tracing::testing::AdoptTracer(&tc, tracer);
  ASSERT_THAT(tracer->RefCountForTesting(), Eq(1));
  {
    TraceContext copy1(tc);
    EXPECT_THAT(tracer->RefCountForTesting(), Eq(2));
  }
  EXPECT_THAT(tracer->RefCountForTesting(), Eq(1));
}

// This tests that TraceContext's move assignment operator does not leak
// existing tracer_ instances (when present) and adopts the new tracer properly.
TEST(TraceContextTest, MovedOverwritingTracer) {
  int tracer1_destroy_count = 0;
  int tracer2_destroy_count = 0;
  {
    auto* tracer1 = new DummyTracer(&tracer1_destroy_count);
    auto* tracer2 = new DummyTracer(&tracer2_destroy_count);
    TraceContext tc1(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
    perftools::tracing::testing::AdoptTracer(&tc1, tracer1);
    TraceContext tc2(2, 3, 4, TraceContext::kTraceMaskTransientTracingOn);
    perftools::tracing::testing::AdoptTracer(&tc2, tracer2);

    EXPECT_EQ(tc1.tracer(), tracer1);
    EXPECT_EQ(tc2.tracer(), tracer2);
    EXPECT_EQ(tracer1_destroy_count, 0);
    EXPECT_EQ(tracer2_destroy_count, 0);

    tc1 = std::move(tc2);

    // We expect that tracer1 has been destroyed and replaced by tracer2 in
    // tc1.tracer_.
    EXPECT_EQ(tc1.tracer(), tracer2);
    EXPECT_EQ(tracer1_destroy_count, 1);
    EXPECT_EQ(tracer2_destroy_count, 0);
  }
  // Both tracers should be destroyed exactly once.
  EXPECT_EQ(tracer1_destroy_count, 1);
  EXPECT_EQ(tracer2_destroy_count, 1);
}

class TraceContextCopyMoveTest : public ::testing::TestWithParam<bool> {
 protected:
  bool ByMove() const { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(Instantiation, TraceContextCopyMoveTest,
                         ::testing::Bool());

TEST_P(TraceContextCopyMoveTest, AssignBasics) {
  TraceContext tc1(1, 2, 3, 4);
  TraceContext tc2(5, 6, 7, 8);

#ifdef ENABLE_CONTEXT_ORIGIN
  std::vector<void*> stack_trace1{&tc1};
  std::vector<void*> stack_trace2{&tc2};
  tc1.set_origin(base::ContextOrigin(stack_trace1));
  tc2.set_origin(base::ContextOrigin(stack_trace2));
#endif

  if (ByMove()) {
    tc1 = std::move(tc2);
  } else {
    tc1 = tc2;
  }

  EXPECT_EQ(tc1.rpc_id(), 5);
  EXPECT_EQ(tc1.parent_rpc_id(), 6);
  EXPECT_EQ(tc1.global_id(), 7);
  EXPECT_EQ(tc1.mask(), 8);

#ifdef ENABLE_CONTEXT_ORIGIN
  EXPECT_THAT(tc1.origin().stack_trace(), Optional(stack_trace2));
#endif
}

TEST_P(TraceContextCopyMoveTest, AssignWithTracer) {
  int tracer1_destroy_count = 0;
  int tracer2_destroy_count = 0;
  {
    auto* tracer1 = new DummyTracer(&tracer1_destroy_count);
    TraceContext tc1(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
    perftools::tracing::testing::AdoptTracer(&tc1, tracer1);
    ASSERT_EQ(tc1.tracer(), tracer1);
    ASSERT_EQ(tracer1_destroy_count, 0);

    auto* tracer2 = new DummyTracer(&tracer2_destroy_count);
    TraceContext tc2(5, 6, 7, TraceContext::kTraceMaskTransientTracingOn);
    perftools::tracing::testing::AdoptTracer(&tc2, tracer2);
    ASSERT_EQ(tc2.tracer(), tracer2);
    ASSERT_EQ(tracer2_destroy_count, 0);

    if (ByMove()) {
      tc1 = std::move(tc2);
    } else {
      tc1 = tc2;
    }

    // We expect that all properties from tc2 have been copied into tc1.
    EXPECT_EQ(tc1.rpc_id(), 5);
    EXPECT_EQ(tc1.parent_rpc_id(), 6);
    EXPECT_EQ(tc1.global_id(), 7);
    EXPECT_EQ(tc1.mask(), tracer2->trace_mask());

    // We expect tc1 to contain tracer2
    EXPECT_EQ(tc1.tracer(), tracer2);

    if (ByMove()) {
      EXPECT_EQ(tracer2->RefCountForTesting(), 1);
      EXPECT_FALSE(tc2.CanRecordAnnotations());  // NOLINT
    } else {
      EXPECT_EQ(tracer2->RefCountForTesting(), 2);
    }

    EXPECT_EQ(tracer1_destroy_count, 1);
    EXPECT_EQ(tracer2_destroy_count, 0);
  }

  // Both tracers should be destroyed exactly once.
  EXPECT_EQ(tracer1_destroy_count, 1);
  EXPECT_EQ(tracer2_destroy_count, 1);
}

TEST_P(TraceContextCopyMoveTest, AssignWithSameTracer) {
  int tracer_destroy_count = 0;
  {
    auto* tracer = new DummyTracer(&tracer_destroy_count);
    TraceContext tc1(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
    perftools::tracing::testing::AdoptTracer(&tc1, tracer);

    TraceContext tc2(tc1);
    ASSERT_EQ(tc2.tracer(), tracer);

    if (ByMove()) {
      tc1 = std::move(tc2);
    } else {
      tc1 = tc2;
    }

    // We expect that all properties from tc2 have been copied into tc1.
    EXPECT_EQ(tc1.rpc_id(), 1);
    EXPECT_EQ(tc1.parent_rpc_id(), 2);
    EXPECT_EQ(tc1.global_id(), 3);
    EXPECT_EQ(tc1.mask(), tracer->trace_mask());

    // We expect tc1 to still contain tracer
    EXPECT_EQ(tc1.tracer(), tracer);

    if (ByMove()) {
      EXPECT_EQ(tracer->RefCountForTesting(), 1);
      EXPECT_FALSE(tc2.CanRecordAnnotations());  // NOLINT
    } else {
      EXPECT_EQ(tracer->RefCountForTesting(), 2);
    }

    EXPECT_EQ(tracer_destroy_count, 0);
  }

  // The tracer should be destroyed exactly once.
  EXPECT_EQ(tracer_destroy_count, 1);
}

TEST_P(TraceContextCopyMoveTest, AssignWithEventListeners) {
  NiceMock<MockTraceEventListener> listener1, listener2, listener3;

  TraceContext tc1(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
  tc1.AddTraceEventListener(&listener1);

  TraceContext tc2(5, 6, 7, TraceContext::kTraceMaskTransientTracingOn);
  tc2.AddTraceEventListener(&listener2);

  // We expect trace listener1 to be destroyed and be replaced
  // by either a copy of listener2, or a moved listener2
  EXPECT_CALL(listener1, ReleaseEventListener());

  if (ByMove()) {
    tc1 = std::move(tc2);
  } else {
    EXPECT_CALL(listener2, GetEventListener(_)).WillOnce(Return(&listener3));
    tc1 = tc2;
  }

  // We expect that all properties from tc2 have been copied into tc1.
  EXPECT_EQ(tc1.rpc_id(), 5);
  EXPECT_EQ(tc1.parent_rpc_id(), 6);
  EXPECT_EQ(tc1.global_id(), 7);
  EXPECT_EQ(tc1.mask(), TraceContext::kTraceMaskTransientTracingOn);

  if (ByMove()) {
    EXPECT_FALSE(tc2.ContainsTraceEventListener(&listener2));  // NOLINT
    EXPECT_TRUE(tc1.ContainsTraceEventListener(&listener2));
  } else {
    // We expect tc1 to contain the copy of listener2
    EXPECT_TRUE(tc1.ContainsTraceEventListener(&listener3));
    EXPECT_CALL(listener3, ReleaseEventListener());
  }

  EXPECT_CALL(listener2, ReleaseEventListener());
}

TEST_P(TraceContextCopyMoveTest, AssignWithCensusHandle) {
  TraceContext restore;
  TraceContextSwitcher tcs(std::move(restore));

  stats_census::Tagger tagger1({{"k1", "v1"}});
  TraceContext tc1(TraceContext::kThread);

  stats_census::Tagger tagger2({{"k2", "v2"}});
  TraceContext tc2(TraceContext::kThread);

  if (ByMove()) {
    tc1 = std::move(tc2);
  } else {
    tc1 = tc2;
  }

  EXPECT_THAT(stats_census::GetTagValue(tc1.census_handle(),
                                        stats_census::MaybeKeyId("k1")),
              Eq("v1"));
  EXPECT_THAT(stats_census::GetTagValue(tc1.census_handle(),
                                        stats_census::MaybeKeyId("k2")),
              Eq("v2"));

  if (ByMove()) {
    EXPECT_THAT(
        stats_census::GetTagValue(tc2.census_handle(),
                                  stats_census::MaybeKeyId("k1")),  // NOLINT
        Eq(""));
  }
}

TEST(TraceContextTest, MovedFromInstanceIsReleasedAfterMoveConstruct) {
  NiceMock<MockTraceEventListener> listener;
  stats_census::Tagger tagger1({{"k1", "v1"}});
  TraceContext tc1(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
  tc1.set_census_handle(TraceContext::Current()->census_handle());
  tc1.AddTraceEventListener(&listener);
  perftools::tracing::testing::AdoptTracer(&tc1, new DummyTracer(nullptr));

  ASSERT_TRUE(tc1.CanRecordAnnotations());
  ASSERT_TRUE(tc1.has_sync_tracer());
  ASSERT_FALSE(stats_census::IsDefaultHandle(tc1.census_handle()));

  // Move construct must leave tc1 not holding census data or tracers
  TraceContext tc2(std::move(tc1));
  EXPECT_FALSE(tc1.CanRecordAnnotations());                         // NOLINT
  EXPECT_FALSE(tc1.has_sync_tracer());                              // NOLINT
  EXPECT_TRUE(stats_census::IsDefaultHandle(tc1.census_handle()));  // NOLINT
}

TEST(TraceContextTest, MovedFromInstanceIsReleasedAfterMoveAssign) {
  NiceMock<MockTraceEventListener> listener;
  stats_census::Tagger tagger1({{"k1", "v1"}});
  TraceContext tc1(1, 2, 3, TraceContext::kTraceMaskTransientTracingOn);
  tc1.set_census_handle(TraceContext::Current()->census_handle());
  TraceContext tc2(2, 3, 4, TraceContext::kTraceMaskTransientTracingOn);
  tc2.set_census_handle(TraceContext::Current()->census_handle());
  tc1.AddTraceEventListener(&listener);
  tc2.AddTraceEventListener(&listener);
  perftools::tracing::testing::AdoptTracer(&tc1, new DummyTracer(nullptr));
  perftools::tracing::testing::AdoptTracer(&tc2, new DummyTracer(nullptr));

  ASSERT_TRUE(tc1.CanRecordAnnotations());
  ASSERT_TRUE(tc1.has_sync_tracer());
  ASSERT_FALSE(stats_census::IsDefaultHandle(tc1.census_handle()));

  // Move assign must leave tc1 not holding census data or tracers
  tc2 = std::move(tc1);
  EXPECT_FALSE(tc1.CanRecordAnnotations());                         // NOLINT
  EXPECT_FALSE(tc1.has_sync_tracer());                              // NOLINT
  EXPECT_TRUE(stats_census::IsDefaultHandle(tc1.census_handle()));  // NOLINT
}

TEST(TraceContextTest, Accessors) {
  uint64_t rpc_id = 1;
  uint64_t parent_rpc_id = 2;
  uint64_t global_id = 3;
  uint32_t mask = 0;

  TraceContext tc(rpc_id, parent_rpc_id, global_id, mask);

  tc.set_mask(0);
  EXPECT_FALSE(tc.is_traced());
  EXPECT_FALSE(tc.is_layer_local_sampled());
  EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
  tc.set_mask(TraceContext::kTraceMaskRPCTracingOn);
  EXPECT_TRUE(tc.is_traced());
  EXPECT_FALSE(tc.is_layer_local_sampled());
  EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
  tc.set_mask(TraceContext::kTraceMaskLayerLocalSamplingOn);
  EXPECT_TRUE(tc.is_layer_local_sampled());

  tc.set_mask(0);
  EXPECT_FALSE(tc.has_gwfi_enabled());
  EXPECT_THAT(tc.initiator_id(), testing::Eq(std::nullopt));
  tc.set_mask(TraceContext::kTraceMaskGoogleWideFaultInjection);
  EXPECT_TRUE(tc.has_gwfi_enabled());
}

TEST(TraceContextTest, SetStatus) {
  uint64_t rpc_id = 1;
  uint64_t parent_rpc_id = 2;
  uint64_t global_id = 3;
  uint32_t mask = 1;

  TraceContext tc(rpc_id, parent_rpc_id, global_id, mask);
  perftools::tracing::testing::AdoptTracer(&tc, new TestBaseTracer);
  tc.set_status(absl::InternalError("test error"));
  EXPECT_THAT(tc.tracer()->status(),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("test error")));
}

TEST(TraceContextTest, GetTracerSignalSafe) {
  TraceContext tc;
  EXPECT_THAT(tc.get_tracer_signal_safe(), IsNull());
  auto* tracer = new TestBaseTracer;
  perftools::tracing::testing::AdoptTracer(&tc, tracer);
#ifdef __cpp_lib_atomic_ref
  EXPECT_THAT(tc.get_tracer_signal_safe(), Eq(tracer));
#else
  EXPECT_THAT(tc.get_tracer_signal_safe(), IsNull());
#endif
}

struct SkeletalTestCase {
  std::string desc;
  // Mask indicates which sampling mechanisms are active for this test.
  int64_t mask;
  // Whether a tracer should be attached to the context for this test.
  bool attach_tracer;
  // Below are all test expectations.
  bool is_traced;
  bool is_skeletally_traced;
  bool is_only_skeletally_traced;
  bool is_traced_any_kind;
  bool is_traced_or_speculatively_traced;
  bool can_record_annotations;
  bool can_record_skeletal_annotations;
  bool has_initiator_id;
};

class SkeletalTracingTest : public ::testing::TestWithParam<SkeletalTestCase> {
};

TEST_P(SkeletalTracingTest, TracingStateAccessors) {
  SkeletalTestCase test_case = GetParam();
  TraceContext tc;
  tc.set_mask(test_case.mask);
  if (test_case.attach_tracer) {
    auto* tracer = new TestBaseTracer;
    if (test_case.is_traced_any_kind) {
      tracer->set_inherited_initiator_id(1234);
    }
    perftools::tracing::testing::AdoptTracer(&tc, tracer);
  }
  EXPECT_EQ(tc.is_traced(), test_case.is_traced);
  EXPECT_EQ(tc.is_skeletally_traced(SkeletalTracingAccess::Get()),
            test_case.is_skeletally_traced);
  EXPECT_EQ(tc.is_only_skeletally_traced(SkeletalTracingAccess::Get()),
            test_case.is_only_skeletally_traced);
  EXPECT_EQ(tc.is_traced_any_kind(SkeletalTracingAccess::Get()),
            test_case.is_traced_any_kind);
  EXPECT_EQ(tc.is_traced_or_speculatively_traced(),
            test_case.is_traced_or_speculatively_traced);
  EXPECT_EQ(tc.CanRecordAnnotations(), test_case.can_record_annotations);
  EXPECT_EQ(tc.CanRecordSkeletalAnnotations(SkeletalTracingAccess::Get()),
            test_case.can_record_skeletal_annotations);
  if (test_case.has_initiator_id) {
    EXPECT_THAT(tc.initiator_id(), Optional(1234));
  } else {
    EXPECT_THAT(tc.initiator_id(), Eq(std::nullopt));
  }
}

INSTANTIATE_TEST_SUITE_P(
    SkeletalTracingTests, SkeletalTracingTest,
    ::testing::Values(
        SkeletalTestCase{
            .desc = "Skeletal tracing only",
            .mask = TraceContext::kTraceMaskSkeletalTracingOn,
            .attach_tracer = true,
            .is_traced = false,
            .is_skeletally_traced = true,
            .is_only_skeletally_traced = true,
            .is_traced_any_kind = true,
            .is_traced_or_speculatively_traced = false,
            .can_record_annotations = false,
            .can_record_skeletal_annotations = true,
            .has_initiator_id = true,
        },
        SkeletalTestCase{
            .desc = "Skeletal and transient tracing",
            .mask = TraceContext::kTraceMaskSkeletalTracingOn |
                    TraceContext::kTraceMaskTransientTracingOn,
            .attach_tracer = true,
            .is_traced = false,
            .is_skeletally_traced = true,
            .is_only_skeletally_traced = false,
            .is_traced_any_kind = true,
            .is_traced_or_speculatively_traced = false,
            .can_record_annotations = true,
            .can_record_skeletal_annotations = true,
            .has_initiator_id = true,
        },
        SkeletalTestCase{
            .desc = "Skeletal and normal tracing",
            .mask = TraceContext::kTraceMaskSkeletalTracingOn |
                    TraceContext::kTraceMaskRPCTracingOn,
            .attach_tracer = true,
            .is_traced = true,
            .is_skeletally_traced = true,
            .is_only_skeletally_traced = false,
            .is_traced_any_kind = true,
            .is_traced_or_speculatively_traced = true,
            .can_record_annotations = true,
            .can_record_skeletal_annotations = true,
            .has_initiator_id = true,
        },
        SkeletalTestCase{
            .desc = "Skeletal and speculative tracing",
            .mask = TraceContext::kTraceMaskSkeletalTracingOn |
                    TraceContext::kTraceMaskSpeculativeCollectingOn,
            .attach_tracer = true,
            .is_traced = false,
            .is_skeletally_traced = true,
            .is_only_skeletally_traced = false,
            .is_traced_any_kind = true,
            .is_traced_or_speculatively_traced = true,
            .can_record_annotations = true,
            .can_record_skeletal_annotations = true,
            .has_initiator_id = true,
        },
        SkeletalTestCase{
            .desc = "Skeletal and Sherlog tracing",
            .mask = TraceContext::kTraceMaskSkeletalTracingOn |
                    TraceContext::kTraceMaskSherlogTracingOn,
            .attach_tracer = true,
            .is_traced = false,
            .is_skeletally_traced = true,
            .is_only_skeletally_traced = false,
            .is_traced_any_kind = true,
            .is_traced_or_speculatively_traced = false,
            .can_record_annotations = true,
            .can_record_skeletal_annotations = true,
            .has_initiator_id = true,
        },
        SkeletalTestCase{
            .desc = "Not traced but has a tracer",
            .mask = TraceContext::kTraceMaskTransientTracingOn,
            .attach_tracer = true,
            .is_traced = false,
            .is_skeletally_traced = false,
            .is_only_skeletally_traced = false,
            .is_traced_any_kind = false,
            .is_traced_or_speculatively_traced = false,
            // Even though this request is not traced, it can still record
            // annotations (both skeletal and non-skeletal). This ensures the
            // property that if CanRecordAnnotations = true, then
            // CanRecordSkeletalAnnotations = true. This is important, because
            // the idea of skeletal tracing is to be invisible and act as if
            // it's not there.
            .can_record_annotations = true,
            .can_record_skeletal_annotations = true,
            .has_initiator_id = false,
        },
        SkeletalTestCase{
            .desc = "Not traced and has no tracer",
            .mask = 0,
            .attach_tracer = false,
            .is_traced = false,
            .is_skeletally_traced = false,
            .is_only_skeletally_traced = false,
            .is_traced_any_kind = false,
            .is_traced_or_speculatively_traced = false,
            .can_record_annotations = false,
            .can_record_skeletal_annotations = false,
            .has_initiator_id = false,
        }),
    [](const testing::TestParamInfo<SkeletalTracingTest::ParamType>& info) {
      std::string name = info.param.desc;
      absl::c_replace_if(name, [](char c) { return !std::isalnum(c); }, '_');
      return name;
    });

TEST(TraceEventListenerTest, AddTraceEventListener) {
  NiceMock<MockTraceEventListener> listener;
  TraceContext tc;
  tc.AddTraceEventListener(&listener);
  EXPECT_CALL(listener, ReleaseEventListener());
}

TEST(TraceEventListenerTest, AddNullptrTraceEventListener) {
  TraceContext tc;
  tc.AddTraceEventListener(nullptr);
}

TEST(TraceEventListenerTest, AddTraceEventListenerToCurrent) {
  auto access = base::ContextAccessForTesting();
  NiceMock<MockTraceEventListener> listener;
  {
    TraceContext restore;
    TraceContextSwitcher tcs(std::move(restore));

    EXPECT_CALL(listener, OnTraceBeginSync(_, _));
    TraceContext::AddTraceEventListener(access, &listener);
    EXPECT_CALL(listener, OnTraceEndSync(_));
    EXPECT_CALL(listener, ReleaseEventListener());
  }
}

TEST(TraceEventListenerTest, AddNullptrTraceEventListenerToCurrent) {
  auto access = base::ContextAccessForTesting();
  TraceContext::AddTraceEventListener(access, nullptr);
}

TEST(TraceEventListenerTest, RemoveTraceEventListenerFromCurrent) {
  TraceContext restore;
  TraceContextSwitcher tcs(std::move(restore));

  auto access = base::ContextAccessForTesting();
  NiceMock<MockTraceEventListener> listener;

  EXPECT_CALL(listener, OnTraceBeginSync(_, _));
  TraceContext::AddTraceEventListener(access, &listener);

  EXPECT_CALL(listener, OnTraceEndSync(_));
  EXPECT_CALL(listener, ReleaseEventListener());
  TraceContext::RemoveTraceEventListener(access, &listener);
  Mock::VerifyAndClearExpectations(&listener);
}

TEST(TraceEventListenerTest, RemoveNullptrTraceEventListenerFromCurrent) {
  auto access = base::ContextAccessForTesting();
  TraceContext::RemoveTraceEventListener(access, nullptr);
}

TEST(TraceEventListenerTest, ContainsTraceEventListener) {
  NiceMock<MockTraceEventListener> listener;

  TraceContext tc;
  EXPECT_FALSE(tc.ContainsTraceEventListener(nullptr));
  EXPECT_FALSE(tc.ContainsTraceEventListener(&listener));

  tc.AddTraceEventListener(&listener);
  EXPECT_FALSE(tc.ContainsTraceEventListener(nullptr));
  EXPECT_TRUE(tc.ContainsTraceEventListener(&listener));
}

TEST(TraceEventListenerTest, WithTraceEventListener) {
  InSequence in_sequence;

  // We use two trace contexts which we deliberately nest, which verifies that
  // both 'before' and 'after' context switch paths in the code are hit in the
  // context switcher calls.
  NiceMock<MockTraceEventListener> listener1;
  TraceContext tc1;
  tc1.AddTraceEventListener(&listener1);
  {
    EXPECT_CALL(listener1, OnTraceBeginSync(_, _));
    TraceContextSwitcher tcs1(std::move(tc1));

    const TraceContext* current = base::CurrentTraceContextNoAlloc();
    EXPECT_TRUE(current->ContainsTraceEventListener(&listener1));

    NiceMock<MockTraceEventListener> listener2;
    TraceContext tc2;
    tc2.AddTraceEventListener(&listener2);
    {
      EXPECT_CALL(listener1, OnTraceSuspendSync(_));
      EXPECT_CALL(listener2, OnTraceBeginSync(_, _));
      TraceContextSwitcher tcs2(std::move(tc2));

      const TraceContext* current = base::CurrentTraceContextNoAlloc();
      EXPECT_TRUE(current->ContainsTraceEventListener(&listener2));

      EXPECT_CALL(listener2, OnTraceEndSync(_));
      EXPECT_CALL(listener2, ReleaseEventListener());
      EXPECT_CALL(listener1, OnTraceResumeSync(_));
    }

    EXPECT_CALL(listener1, OnTraceEndSync(_));
    EXPECT_CALL(listener1, ReleaseEventListener());
  }
}

TEST(TraceEventListenerTest, MoveContextWithTraceEventListener) {
  InSequence in_sequence;
  NiceMock<MockTraceEventListener> listener;

  TraceContext tc1;
  tc1.AddTraceEventListener(&listener);

  TraceContext tc2 = std::move(tc1);
  EXPECT_FALSE(tc1.ContainsTraceEventListener(&listener));  // NOLINT
  EXPECT_TRUE(tc2.ContainsTraceEventListener(&listener));

  {
    EXPECT_CALL(listener, OnTraceBeginSync(_, _));
    TraceContextSwitcher tcs(std::move(tc2));
    EXPECT_CALL(listener, OnTraceEndSync(_));
    EXPECT_CALL(listener, ReleaseEventListener());
  }
}

TEST(TraceEventListenerTest, CopyContextWithTraceEventListener) {
  InSequence in_sequence;
  NiceMock<MockTraceEventListener> listener1;
  NiceMock<MockTraceEventListener> listener2;

  TraceContext tc1;
  tc1.AddTraceEventListener(&listener1);

  EXPECT_CALL(listener1, GetEventListener(_)).WillOnce(Return(&listener2));
  TraceContext tc2 = tc1;
  EXPECT_FALSE(tc2.ContainsTraceEventListener(&listener1));
  EXPECT_TRUE(tc2.ContainsTraceEventListener(&listener2));

  {
    EXPECT_CALL(listener2, OnTraceBeginSync(_, _));
    TraceContextSwitcher tcs(std::move(tc2));
    EXPECT_CALL(listener2, OnTraceEndSync(_));
    EXPECT_CALL(listener2, ReleaseEventListener());
  }
  EXPECT_CALL(listener1, ReleaseEventListener());
}

#if defined(GTEST_HAS_DEATH_TEST)
TEST(TraceEventListenerTest, AssertAddTraceEventListenerNotCurrent) {
  EXPECT_DEBUG_DEATH(const_cast<TraceContext*>(TraceContext::Current())
                         ->AddTraceEventListener(nullptr),
                     "CurrentTraceContextNoAlloc");
}
#endif  // GTEST_HAS_DEATH_TEST

TEST(TraceEventListenerTest, TraceContextEmitsSpawnEventWithThreadName) {
  NiceMock<MockTraceEventListener> listener1;
  NiceMock<MockTraceEventListener> listener2;

  TraceContext tc1;
  tc1.AddTraceEventListener(&listener1);
  TraceContextSwitcher tcss(std::move(tc1));
  {
    EXPECT_CALL(listener1, GetEventListener(_)).WillOnce(Return(&listener2));
    EXPECT_CALL(listener1, OnTraceSpawn(_, Eq("ThreadName")));
    TraceContext tc2(TraceContext::kThread, "ThreadName");
    EXPECT_CALL(listener2, ReleaseEventListener());
  }
  EXPECT_CALL(listener1, ReleaseEventListener());
}

TEST(TraceContextTest, SetGlobalIdAbandonsAllTracers) {
  NiceMock<MockTraceEventListener> listener;
  uint64_t global_id = random64();
  uint64_t rpc_id = random64();
  uint64_t parent_rpc_id = random64();
  uint32_t mask = random();
  TraceContext tc(rpc_id, parent_rpc_id, global_id, mask);

  ON_CALL(listener, GetEventListener(_)).WillByDefault(Return(&listener));
  tc.AddTraceEventListener(&listener);
  perftools::tracing::testing::AdoptTracer(&tc, new TestBaseTracer);

  ASSERT_TRUE(tc.CanRecordAnnotations());
  ASSERT_TRUE(tc.ContainsTraceEventListener(&listener));

  tc.set_global_id(random64());
  EXPECT_FALSE(tc.CanRecordAnnotations());
  EXPECT_FALSE(tc.ContainsTraceEventListener(&listener));
}

TEST(TraceContextTest, SetRpcIdAbandonsTracerButNotTraceEventListeners) {
  for (bool set_parent : {false, true}) {
    NiceMock<MockTraceEventListener> listener;
    TraceContext tc(random64(), random64(), random64(), random());

    ON_CALL(listener, GetEventListener(_)).WillByDefault(Return(&listener));
    tc.AddTraceEventListener(&listener);
    perftools::tracing::testing::AdoptTracer(&tc, new TestBaseTracer);

    ASSERT_TRUE(tc.CanRecordAnnotations());
    ASSERT_TRUE(tc.ContainsTraceEventListener(&listener));

    if (set_parent) {
      tc.set_parent_rpc_id(random64());
    } else {
      tc.set_rpc_id(random64());
    }
    EXPECT_FALSE(tc.CanRecordAnnotations());
    EXPECT_TRUE(tc.ContainsTraceEventListener(&listener));
  }
}

TEST(TraceContextTest, HasSyncTracer) {
  NiceMock<MockTraceEventListener> listener;
  TraceContext tc(random64(), random64(), random64(), random());
  EXPECT_FALSE(tc.has_sync_tracer());
  tc.AddTraceEventListener(&listener);
  EXPECT_TRUE(tc.has_sync_tracer());
}

#ifdef ENABLE_CONTEXT_ORIGIN
TEST(TraceContextOriginTest, SetOrigin) {
  TraceContext tc(TraceContext::kDefault);
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  tc.set_origin(base::ContextOrigin{stack});
  ASSERT_THAT(tc.origin().stack_trace(), Optional(ElementsAreArray(stack)));
}

TEST(TraceContextOriginTest, PropagatesOnCopy) {
  TraceContext tc1(TraceContext::kDefault);
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  tc1.set_origin(base::ContextOrigin{stack});
  TraceContext tc2(tc1);
  ASSERT_THAT(tc2.origin().stack_trace(), Optional(ElementsAreArray(stack)));
}

TEST(TraceContextOriginTest, PropagatesOnMove) {
  TraceContext tc1(TraceContext::kDefault);
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  tc1.set_origin(base::ContextOrigin{stack});
  TraceContext tc2(std::move(tc1));
  ASSERT_THAT(tc2.origin().stack_trace(), Optional(ElementsAreArray(stack)));
}

TEST(TraceContextOriginTest, PropagatesOnMoveAssign) {
  TraceContext tc1(TraceContext::kDefault);
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  tc1.set_origin(base::ContextOrigin{stack});
  TraceContext tc2(TraceContext::kDefault);
  tc2 = std::move(tc1);
  ASSERT_THAT(tc2.origin().stack_trace(), Optional(ElementsAreArray(stack)));
}
#else
TEST(TraceContextOriginTest, SetOriginNoopWhenContextOriginIsUndefined) {
  TraceContext tc;
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  tc.set_origin(base::ContextOrigin{stack});
  ASSERT_THAT(tc.origin().stack_trace(), Eq(std::nullopt));
}
#endif  // ENABLE_CONTEXT_ORIGIN

void BM_TraceContextDefaultConstructor(benchmark::State& state) {
  for (auto _ : state) {
    TraceContext context;
    benchmark::DoNotOptimize(context);
  }
}
BENCHMARK(BM_TraceContextDefaultConstructor);

void BM_TraceContextLegacyDefaultConstructor(benchmark::State& state) {
  for (auto _ : state) {
    TraceContext context;
    benchmark::DoNotOptimize(context);
  }
}
BENCHMARK(BM_TraceContextLegacyDefaultConstructor);

void BM_TraceContextThreadConstructor(benchmark::State& state) {
  for (auto _ : state) {
    TraceContext context(TraceContext::kThread);
    benchmark::DoNotOptimize(context);
  }
}
BENCHMARK(BM_TraceContextThreadConstructor);

void BM_TraceContextLegacyThreadConstructor(benchmark::State& state) {
  for (auto _ : state) {
    TraceContext context(TraceContext::kThread);
    benchmark::DoNotOptimize(context);
  }
}
BENCHMARK(BM_TraceContextLegacyThreadConstructor);

void BM_TraceContextThreadConstructorWithCensusTags(benchmark::State& state) {
  stats_census::Tagger t({{"key", "value"}});
  for (auto _ : state) {
    TraceContext context(TraceContext::kThread);
    benchmark::DoNotOptimize(context);
  }
}
BENCHMARK(BM_TraceContextThreadConstructorWithCensusTags);

void BM_TraceContextLegacyThreadConstructorWithCensusTags(
    benchmark::State& state) {
  stats_census::Tagger t({{"key", "value"}});
  for (auto _ : state) {
    TraceContext context(TraceContext::kThread);
    benchmark::DoNotOptimize(context);
  }
}
BENCHMARK(BM_TraceContextLegacyThreadConstructorWithCensusTags);

void BM_TraceContextThreadMoveConstructorWithTracer(benchmark::State& state) {
  CurrentTraceContext current_context;
  constexpr size_t kBatch = 1000;
  std::vector<TraceContext> input, output;
  while (state.KeepRunningBatch(kBatch)) {
    state.PauseTiming();
    output.clear();
    output.reserve(kBatch);
    input.clear();
    for (size_t i = 0; i < kBatch; ++i) {
      input.emplace_back(TraceContext::kDefault);
      perftools::tracing::testing::AdoptTracer(&input.back(),
                                               new DummyTracer());
    }
    state.ResumeTiming();
    for (auto& elem : input) {
      output.push_back(std::move(elem));
    }
  }
}
BENCHMARK(BM_TraceContextThreadMoveConstructorWithTracer);

void BM_TraceContextThreadMoveConstructorWithoutTracer(
    benchmark::State& state) {
  constexpr size_t kBatch = 1000;
  std::vector<TraceContext> input, output;
  CurrentTraceContext current_context;
  while (state.KeepRunningBatch(kBatch)) {
    state.PauseTiming();
    output.clear();
    output.reserve(kBatch);
    input.clear();
    for (size_t i = 0; i < kBatch; ++i) {
      input.emplace_back(TraceContext::kDefault);
    }
    state.ResumeTiming();
    for (auto& elem : input) {
      output.push_back(std::move(elem));
    }
  }
}
BENCHMARK(BM_TraceContextThreadMoveConstructorWithoutTracer);

void BM_TraceContextReset(benchmark::State& state) {
  TraceContext context;
  for (auto _ : state) {
    context.Reset();
  }
}
BENCHMARK(BM_TraceContextReset);

void BM_Swap(benchmark::State& state) {
  TraceContext context1;
  TraceContext context2;
  for (auto _ : state) {
    using std::swap;
    swap(context1, context2);
    benchmark::DoNotOptimize(context1);
    benchmark::DoNotOptimize(context2);
  }
}
BENCHMARK(BM_Swap);

void BM_SwapWithTracer(benchmark::State& state) {
  TraceContext context1;
  TraceContext context2;
  perftools::tracing::testing::AdoptTracer(&context1, new DummyTracer());
  for (auto _ : state) {
    using std::swap;
    swap(context1, context2);
    benchmark::DoNotOptimize(context1);
    benchmark::DoNotOptimize(context2);
  }
}
BENCHMARK(BM_SwapWithTracer);

void BM_SwapWithCensusHandle(benchmark::State& state) {
  TraceContext current;
  TraceContextSwitcher tcs(std::move(current));

  stats_census::Tagger t({{"key", "value"}});
  TraceContext context1(TraceContext::kThread);
  TraceContext context2(TraceContext::kThread);
  for (auto _ : state) {
    using std::swap;
    swap(context1, context2);
    benchmark::DoNotOptimize(context1);
    benchmark::DoNotOptimize(context2);
  }
}
BENCHMARK(BM_SwapWithCensusHandle);

void BM_CurrentTraceContextSwapWithoutTracer(benchmark::State& state) {
  TraceContext context;
  for (auto _ : state) {
    TraceContextSwitcher with(context);
    benchmark::DoNotOptimize(with);
  }
}
BENCHMARK(BM_CurrentTraceContextSwapWithoutTracer);

void BM_CurrentTraceContextSwapWithTracer(benchmark::State& state) {
  TraceContext context;
  perftools::tracing::testing::AdoptTracer(&context, new DummyTracer());
  for (auto _ : state) {
    TraceContextSwitcher with(context);
    benchmark::DoNotOptimize(with);
  }
}
BENCHMARK(BM_CurrentTraceContextSwapWithTracer);

void BM_CurrentTraceContextCurrent(benchmark::State& state) {
  constexpr const int kRepeats = 10;
  for (auto _ : state) {
    for (int i = 0; i < kRepeats; ++i) {
      ::benchmark::DoNotOptimize(TraceContext::Current());
    }
  }
}
BENCHMARK(BM_CurrentTraceContextCurrent);

// google3> benchy --perflab --fdo --benchmark_filter=BM_WithTraceContext
//
// name                             cpu/op
// --------------------------------------------------------
// BM_WithTraceContextNoTracer      4.49ns ± 0%
// BM_WithTraceContextOneTracerIn   17.9ns ± 2%
// BM_WithTraceContextOneTracerOut  4.50ns ± 0%
// BM_WithTraceContextTwoTracers    17.9ns ± 2%
void BM_WithTraceContext(benchmark::State& state, base::Tracer* tracerA,
                         base::Tracer* tracerB) {
  TraceContext tca(1, 2, 3, 0);
  if (tracerA != nullptr) {
    perftools::tracing::testing::AdoptTracerForTesting(&tca, tracerA);
  }
  base::WithTraceContext with_a(std::move(tca));

  TraceContext tcb(4, 5, 6, 0);
  if (tracerB != nullptr) {
    perftools::tracing::testing::AdoptTracerForTesting(&tcb, tracerB);
  }
  for (auto s : state) {
    base::WithTraceContext with_b(tcb);
  }
}
void BM_WithTraceContextNoTracer(benchmark::State& state) {
  BM_WithTraceContext(state, nullptr, nullptr);
}
void BM_WithTraceContextOneTracerIn(benchmark::State& state) {
  BM_WithTraceContext(state, nullptr, new TestBaseTracer);
}
void BM_WithTraceContextOneTracerOut(benchmark::State& state) {
  BM_WithTraceContext(state, new TestBaseTracer, nullptr);
}
void BM_WithTraceContextTwoTracers(benchmark::State& state) {
  BM_WithTraceContext(state, new TestBaseTracer, new TestBaseTracer);
}
BENCHMARK(BM_WithTraceContextNoTracer);
BENCHMARK(BM_WithTraceContextOneTracerIn);
BENCHMARK(BM_WithTraceContextOneTracerOut);
BENCHMARK(BM_WithTraceContextTwoTracers);

}  // namespace

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);
  // Note: this cannot use
  // benchmark::benchmark::RunSpecifiedBenchmarksThenExit() because it may be
  // link with the external library.
  // TODO: Once the last step in the migration plan is
  // implemented, we should be able to use
  // benchmark::benchmark::RunSpecifiedBenchmarksThenExit() directly and not
  // duplicate the check here.
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }
  srandom(1);
  return RUN_ALL_TESTS();
}
