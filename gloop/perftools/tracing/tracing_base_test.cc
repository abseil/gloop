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

#include "gloop/perftools/tracing/tracing_base.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <thread>  // NOLINT

#include "absl/base/casts.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::Ne;
using testing::StrEq;

// We don't publicly expose this flag, declare it for testing
// TODO: b/414786755 - remove
ABSL_DECLARE_FLAG(bool, dapper_pe_use_regions);

namespace perftools::tracing {
namespace {

TEST(TracingBase, PerThreadDefaults) {
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_FALSE(has_sync_trace());
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(active_trace_span_id(), Eq(0));
}

TEST(TracingBase, PerThreadValues) {
  MockTraceEventListener mock;
  internal::set_active_sync_id(kMainSyncId);
  internal::set_active_event_listener(&mock);
  internal::set_active_trace_span_id(0x12345678);
  EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));
  EXPECT_TRUE(has_sync_trace());
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock));
  EXPECT_THAT(active_trace_span_id(), Eq(0x12345678));
  std::thread thread([] {
    EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
    EXPECT_FALSE(has_sync_trace());
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
    EXPECT_THAT(active_trace_span_id(), Eq(0));
  });
  thread.join();
  internal::set_active_sync_id(kNoSyncId);
  internal::set_active_event_listener(nullptr);
  internal::set_active_trace_span_id(0);
}

TEST(TracingBase, ToBarrierId) {
  // Guarantee nullptr -> kNoBarrierId
  EXPECT_THAT(ToBarrierId(nullptr), Eq(kNoBarrierId));

  // Guarantee that while the barrier salt is ASLR initialized from
  // its own address does not eval to 0 or itself.
  BarrierId salt_id = ToBarrierId(&internal::barrier_salt);
  EXPECT_THAT(salt_id, Ne(kNoBarrierId));
  EXPECT_THAT(salt_id, Ne(absl::bit_cast<BarrierId>(&internal::barrier_salt)));

  // Guarantee <> nullptr -> <> kNoBarrierId
  for (uintptr_t ptr = 1; ptr != 0; ptr = ptr << 1) {
    const void* p = reinterpret_cast<const void*>(ptr);
    EXPECT_THAT(ToBarrierId(&p), Ne(kNoBarrierId));
  }

  // Guarantee unique address --> unique BarrierId
  // Reasonable proof is that each single bit is included.
  for (uintptr_t ptr1 = 1; ptr1 != 0; ptr1 = ptr1 << 1) {
    const void* p1 = reinterpret_cast<const void*>(ptr1);
    for (uintptr_t ptr2 = 1; ptr2 != 0; ptr2 = ptr2 << 1) {
      const void* p2 = reinterpret_cast<const void*>(ptr2);
      if (ptr2 == ptr1) {
        EXPECT_THAT(ToBarrierId(p1), Eq(ToBarrierId(p2)));
      } else {
        EXPECT_THAT(ToBarrierId(p1), Ne(ToBarrierId(p2)));
      }
    }
  }

  // Currently we swap the bits for obfuscation
  for (uintptr_t ptr = 1; ptr != 0; ptr = ptr << 1) {
    const void* p1 = reinterpret_cast<const void*>(ptr);
    const void* p2 = reinterpret_cast<const void*>(ToBarrierId(p1));
    EXPECT_THAT(p1, Ne(p2));
  }
}

template <typename T>
std::string Stream(T t) {
  std::stringstream s;
  s << t;
  return s.str();
}

TEST(TracingBase, StreamMsgOrigin) {
  EXPECT_THAT(Stream(MsgOrigin::kClient), StrEq("Client"));
  EXPECT_THAT(Stream(MsgOrigin::kServer), StrEq("Server"));
  EXPECT_THAT(Stream(static_cast<MsgOrigin>(3)), StrEq("MsgOrigin?(3)"));
}

TEST(TracingBase, StreamControlFlowType) {
  EXPECT_THAT(Stream(ControlFlowType::kUndefined), StrEq("Undefined"));
  EXPECT_THAT(Stream(ControlFlowType::kGeneric), StrEq("Generic"));
  EXPECT_THAT(Stream(ControlFlowType::kSchedule), StrEq("Schedule"));
  EXPECT_THAT(Stream(ControlFlowType::kContinue), StrEq("Continue"));
  EXPECT_THAT(Stream(ControlFlowType::kStart), StrEq("Start"));
  EXPECT_THAT(Stream(ControlFlowType::kEnd), StrEq("End"));
  EXPECT_THAT(Stream(static_cast<ControlFlowType>(10)),
              StrEq("ControlFlowType?(10)"));
}

TEST(TracingBase, StreamEndPoint) {
  EXPECT_THAT(Stream(EndPoint::kStreamingClient), StrEq("StreamingClient"));
  EXPECT_THAT(Stream(EndPoint::kStreamingServer), StrEq("StreamingServer"));
  EXPECT_THAT(Stream(static_cast<EndPoint>(3)), StrEq("EndPoint?(3)"));
}

TEST(TracingBase, StreamMsgFlags) {
  EXPECT_THAT(Stream(MsgFlags::kDefault), StrEq("Default"));
  EXPECT_THAT(Stream(MsgFlags::kHalfClose), StrEq("HalfClose"));
  EXPECT_THAT(Stream(MsgFlags::kControl), StrEq("Control"));
  EXPECT_THAT(Stream(MsgFlags::kHalfClose | MsgFlags::kControl),
              StrEq("HalfClose|Control"));
}

TEST(TracingBase, UseRegionsReflectsFlag) {
  // Default
  EXPECT_TRUE(internal::UseRegions());

  absl::SetFlag(&FLAGS_dapper_pe_use_regions, false);
  EXPECT_FALSE(internal::UseRegions());

  absl::SetFlag(&FLAGS_dapper_pe_use_regions, true);
  EXPECT_TRUE(internal::UseRegions());
}

}  // namespace
}  // namespace perftools::tracing
