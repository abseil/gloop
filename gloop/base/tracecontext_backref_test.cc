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

#include "absl/base/internal/raw_logging.h"
#include "gloop/base/tracecontext.h"

int main() {
#if BASE_HAVE_TRACECONTEXT
  const TraceContext* current = TraceContext::Current();
  ABSL_RAW_CHECK(current != nullptr, "TraceContext::Current() returned null");

  TraceContext tc_from_thread(TraceContext::kThread);
  TraceContext tc_copy(1234, 5678, 9012, TraceContext::kTraceMaskRPCTracingOn);
  tc_from_thread.FromThread();
  tc_from_thread = tc_copy;
  ABSL_RAW_CHECK(tc_from_thread.rpc_id() == 1234, "rpc_id mismatch");
  ABSL_RAW_CHECK(tc_from_thread.parent_rpc_id() == 5678,
                 "parent_rpc_id mismatch");
  ABSL_RAW_CHECK(tc_from_thread.global_id() == 9012, "global_id mismatch");
  ABSL_RAW_CHECK(tc_from_thread.mask() == TraceContext::kTraceMaskRPCTracingOn,
                 "mask mismatch");
#endif
  return 0;
}
