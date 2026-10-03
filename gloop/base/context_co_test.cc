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

#include <iostream>
#include <memory>
#include <utility>

#include "benchmark/benchmark.h"
#include "gloop/base/censushandle.h"
#include "gloop/base/context.h"
#include "gloop/base/context_access.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/multiplex_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/test_only_access.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perftools/tracing/public/debug_trace_event_listener.h"
#include "security/context/public/with_security_context.h"
#include "security/context/testing/fake_security_context.h"
#include "security/credentials/public/principal.h"
#include "stats/census/public/tagging.h"

namespace perftools::tracing {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::NiceMock;

using ::perftools::tracing::testing::TestOnlyAccess;
using ::security::context::WithSecurityContext;
using ::security::context::testing::FakeSecurityContextBuilder;
using ::security::credentials::UserPrincipal;

void AddListener(base::Context& context, TraceEventListener* listener) {
  context.trace()->AddTraceEventListener(MultiplexTraceEventListener(
      DebugTraceEventListener(std::cout), listener));
}

base::ContextPtr ContextWith(TraceEventListener* listener) {
  auto context = std::make_unique<base::Context>();
  AddListener(*context, listener);
  return context;
}

static constexpr auto access = TestOnlyAccess::Create<base::ContextAccess>();

TEST(ContextCoTest, BeginEnd) {
  NiceMock<MockTraceEventListener> mock;
  auto context = ContextWith(&mock);

  EXPECT_CALL(mock, OnTraceBeginSync(SyncId(1), Eq("Begin")));
  context = base::CoSwapContext(access, std::move(context), 1234, "Begin");

  EXPECT_CALL(mock, OnTraceEndSync(SyncId(1)));
  base::CoRestoreContext(access, std::move(context));
}

TEST(ContextCoTest, BeginSuspendResumeEnd) {
  NiceMock<MockTraceEventListener> mock;
  auto context = ContextWith(&mock);

  EXPECT_CALL(mock, OnTraceBeginSync(SyncId(1), Eq("Begin")));
  context = base::CoSwapContext(access, std::move(context), 1234, "Begin");

  EXPECT_CALL(mock, OnTraceWait(_, Eq("Suspend")));
  context = base::CoSwapContext(access, std::move(context), 1234, "Suspend");

  EXPECT_CALL(mock, OnTraceContinue(_));
  context = base::CoSwapContext(access, std::move(context), 1234, "Continue");

  EXPECT_CALL(mock, OnTraceEndSync(SyncId(1)));
  base::CoRestoreContext(access, std::move(context));
}

TEST(ContextCoTest, HandOvers) {
  NiceMock<MockTraceEventListener> mock1;
  NiceMock<MockTraceEventListener> mock2;
  auto context1 = ContextWith(&mock1);
  auto context2 = ContextWith(&mock2);

  EXPECT_CALL(mock1, OnTraceBeginSync(SyncId(1), Eq("Begin1")));
  base::CoRestoreContext(access, std::move(context1), 1234, "Begin1");

  EXPECT_CALL(mock1, OnTraceEndSync(SyncId(1)));
  EXPECT_CALL(mock2, OnTraceBeginSync(SyncId(1), Eq("Begin2")));
  base::CoRestoreContext(access, std::move(context2), 1234, "Begin2");

  EXPECT_CALL(mock2, OnTraceEndSync(SyncId(1)));
  base::CoRestoreContext(access, std::make_unique<base::Context>());
}

security::context::WithSecurityContext* with_sc;
base::WithCensusHandle* with_ch;

void BM_Setup(const benchmark::State&) {
  with_sc = new WithSecurityContext(FakeSecurityContextBuilder::WithUser(
                                        UserPrincipal::FromMdbUser("john-doe"))
                                        ->BuildValidated()
                                        .value());

  with_ch = new base::WithCensusHandle(
      stats_census::SetTags(CensusHandle(), {{"a", "b"}}));
}

void BM_Teardown(const benchmark::State&) {
  delete with_ch;
  delete with_sc;
  with_ch = nullptr;
  with_sc = nullptr;
}

void BM_CoInlinedLegacy(benchmark::State& state) {
  for (auto _ : state) {
    // Start
    const base::Context co_thread_context = base::CurrentContext();

    base::CurrentContext();
  }
}
BENCHMARK(BM_CoInlinedLegacy)
    ->Setup(BM_Setup)
    ->Teardown(BM_Teardown)
    ->Threads(1)
    ->Threads(4);

void BM_CoInlinedWithFork(benchmark::State& state) {
  for (auto _ : state) {
    base::CurrentContext();
  }
}
BENCHMARK(BM_CoInlinedWithFork)
    ->Setup(BM_Setup)
    ->Teardown(BM_Teardown)
    ->Threads(1)
    ->Threads(4);

void BM_CoSuspendResumeLegacy(benchmark::State& state) {
  for (auto _ : state) {
    // Start
    const base::Context co_thread_context = base::CurrentContext();

    // Suspend: no op

    // Resume:
    base::Context context = co_thread_context;
    base::RestoreCurrentContext(&context);
  }
}
BENCHMARK(BM_CoSuspendResumeLegacy)
    ->Setup(BM_Setup)
    ->Teardown(BM_Teardown)
    ->Threads(1)
    ->Threads(4);

void BM_CoSuspendResumeWithFork(benchmark::State& state) {
  for (auto _ : state) {
    // Start: no op

    // Fork
    auto context = std::make_unique<base::Context>(base::Context::kThread);
    context = base::CoSwapContext(access, std::move(context));

    // Resume:
    base::CoRestoreContext(access, std::move(context));
  }
}
BENCHMARK(BM_CoSuspendResumeWithFork)
    ->Setup(BM_Setup)
    ->Teardown(BM_Teardown)
    ->Threads(1)
    ->Threads(4);

void BM_CoSuspendResumeRepeatedLegacy(benchmark::State& state) {
  for (auto _ : state) {
    // Start
    const base::Context co_thread_context = base::CurrentContext();

    for (int i = 0; i < 10; ++i) {
      // Suspend: no op

      // Resume:
      base::Context context = co_thread_context;
      base::RestoreCurrentContext(&context);
    }
  }
  state.SetItemsProcessed(state.iterations() * 10);
}
BENCHMARK(BM_CoSuspendResumeRepeatedLegacy)
    ->Setup(BM_Setup)
    ->Teardown(BM_Teardown)
    ->Threads(1)
    ->Threads(4);

void BM_CoSuspendResumeRepeatedWithFork(benchmark::State& state) {
  for (auto _ : state) {
    // Fork
    auto context = std::make_unique<base::Context>(base::Context::kThread);
    context = base::CoSwapContext(access, std::move(context));
    for (int i = 0; i < 9; ++i) {
      // Resume:
      context = base::CoSwapContext(access, std::move(context));

      // Suspend:
      context = base::CoSwapContext(access, std::move(context));
    }
    // end
    base::CoRestoreContext(access, std::move(context));
  }
  state.SetItemsProcessed(state.iterations() * 10);
}
BENCHMARK(BM_CoSuspendResumeRepeatedWithFork)
    ->Setup(BM_Setup)
    ->Teardown(BM_Teardown)
    ->Threads(1)
    ->Threads(4);

}  // namespace
}  // namespace perftools::tracing
