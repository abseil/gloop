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

#include "gloop/perftools/tracing/tracing_base.h"

#include <atomic>
#include <cstdint>
#include <ostream>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace perftools::tracing {

std::ostream& operator<<(std::ostream& s, MsgOrigin origin) {
  switch (origin) {
    case MsgOrigin::kClient:
      return s << "Client";
    case MsgOrigin::kServer:
      return s << "Server";
  }
  return s << "MsgOrigin?(" << static_cast<int>(origin) << ")";
}

std::ostream& operator<<(std::ostream& s, ControlFlowType control_flow_type) {
  switch (control_flow_type) {
    case ControlFlowType::kUndefined:
      return s << "Undefined";
    case ControlFlowType::kGeneric:
      return s << "Generic";
    case ControlFlowType::kSchedule:
      return s << "Schedule";
    case ControlFlowType::kContinue:
      return s << "Continue";
    case ControlFlowType::kStart:
      return s << "Start";
    case ControlFlowType::kEnd:
      return s << "End";
  }
  return s << "ControlFlowType?(" << static_cast<int>(control_flow_type) << ")";
}

std::ostream& operator<<(std::ostream& s, EndPoint end_point) {
  switch (end_point) {
    case EndPoint::kStreamingClient:
      return s << "StreamingClient";
    case EndPoint::kStreamingServer:
      return s << "StreamingServer";
  }
  return s << "EndPoint?(" << static_cast<int>(end_point) << ")";
}

std::ostream& operator<<(std::ostream& s, MsgFlags flags) {
  int count = 0;
  auto append = [&](MsgFlags flag, absl::string_view text) {
    if ((flags & flag) == flag) {
      if (count++) s << "|";
      s << text;
    }
  };
  append(MsgFlags::kHalfClose, "HalfClose");
  append(MsgFlags::kControl, "Control");
  return count ? s : s << "Default";
}

namespace internal {

// Returns a somewhat random value based on the compile time of this
// source, which we use as a static initialization salt for any
// hypothetical trace running before main (there should not be any).
// This is good enough for those hypothetical 'trace at init' uses.
static constexpr uint64_t CompileTime() {
  constexpr const char timestr[9] = __TIME__;
  return (static_cast<uint64_t>(timestr[0]) << 0) +
         (static_cast<uint64_t>(timestr[1]) << 8) +
         (static_cast<uint64_t>(timestr[3]) << 16) +
         (static_cast<uint64_t>(timestr[4]) << 24) +
         (static_cast<uint64_t>(timestr[6]) << 32) +
         (static_cast<uint64_t>(timestr[7]) << 40);
}

// Random value we use to encrypt unique ptr values into barrier id values to
// avoid leaking addresses in the context of ASLR. We do a 2 step init:
// - initialize `barrier_salt` with a compile time 'compile time' constant to
//   serve as a minimum salt for hypothetical 'trace during initialization'
// - dynamically initialize `barrier_salt` through a helper initialization
//   using `absl::HashOf(&salt, &salt_init)`. The two variable addresses
//   themselves are ALSR. Combining the two values into a hash provides
//   a strongly random bit shuffled value of the two ALSR addresses.
// This provides a highly secure, but minimum cost salt value we can use for
// deterministically converting pointers to barrier id values.
// See <link> for motivation.
// std::atomic<uint64_t>
// barrier_salt{~absl::bit_cast<uintptr_t>(&barrier_salt)};
ABSL_CONST_INIT std::atomic<uint64_t> barrier_salt{CompileTime()};
bool barrier_salt_init = [] {
  // To minimize the change for any pointer collision to zero, we take the 8
  // top most bits to the negated address of the salt (which are typically
  // either all zero or all one for user mode addresses), and always set the
  // bottom bit as we expect most addresses to be an even value.
  uint64_t salt = ~absl::bit_cast<uintptr_t>(&barrier_salt);
  uint64_t rnd = absl::HashOf(&barrier_salt, &barrier_salt_init);
  barrier_salt.store((salt ^ (rnd >> 8)) | 1, std::memory_order_relaxed);
  return true;
}();

ABSL_CONST_INIT thread_local TracePerThreadData per_thread{kNoSyncId, nullptr};
}  // namespace internal

}  // namespace perftools::tracing
