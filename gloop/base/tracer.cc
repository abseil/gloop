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

#include "gloop/base/tracer.h"

#include <atomic>
#include <cstdint>

#include "gloop/base/tracecontext.h"

namespace base {

Tracer::Tracer(const TraceContext& tc) {
  span_id_ = tc.rpc_id();
  parent_span_id_ = tc.parent_rpc_id();
  trace_id_ = tc.global_id();
  trace_mask_.store(tc.mask(), std::memory_order_relaxed);
  if (is_traced_or_speculatively_traced()) {
    // Since initiator ID is stored in Tracer and not in TraceContext, it will
    // not be available in the scenarios when an already sampled TraceContext
    // comes without a Tracer attached. The code using this path is expected to
    // immediately update this Tracer's initiator ID. A special "initiator not
    // set by parent" value is propagated otherwise, so that such code could be
    // identified and fixed.
    uint64_t initiator_id = tc.CanRecordAnnotations()
                                ? tc.tracer()->initiator_id()
                                : kInitiatorNotSetByParent;
    initiator_id_.store(initiator_id, std::memory_order_relaxed);
  }
}

}  // namespace base
