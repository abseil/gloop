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

#include <stdint.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/internal/effective_user_id.h"
#include "gloop/base/reference_tracker.h"  // IWYU pragma: keep
#include "gloop/base/time/time_unix_nanos.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/tracing_types.h"
#include "gloop/base/xray/tracing_annotations.h"
#include "gloop/perftools/tracing/tracing.h"

ABSL_FLAG(bool, tracer_debug_refcounts, false,
          "With this flag on and when compiled with "
          "`--copt=-DENABLE_TRACER_REF_TRACKING`, each Tracer will keep track "
          "of how each of its outstanding references was acquired "
          "(that is, the call stack of each copy of its parent TraceContext). "
          "You can then consult /requestz to get this information.  Used to "
          "debug cases where a tracer has been copied to too many trace "
          "contexts and is living too long.");

namespace {

// A functionality-free implementation of the Tracer interface.  This is used
// when a more meaningful implementation isn't available in
// TraceContext::tracer().
class NoopTracer : public base::Tracer {
 public:
  // Create a NoopTracer. Hystorically, the global NoopTracer does 'not' have a
  // start time, and some tests assert this, so we keep this default behavior.
  explicit NoopTracer(bool start_tracer = false) {
    if (start_tracer) {
      SetStartTimeNow();
    }
  }
  ~NoopTracer() override {}

  void Attach(base::TraceEntrySource* source) override {}
  void Detach(base::TraceEntrySource* source, bool save_clone) override {}
  void EmitTraceEntrySources(base::TraceEntrySink* /*sink*/,
                             bool /*skip_unowned_sources*/) const override {}
  void SetMaxBytesToKeep(int n) override {}
  void SetMaxBytesToKeepPerEntry(int n) override {}
  std::string ToString() const override { return std::string(); }
  std::string name() const override { return std::string(); }
  void AttachTraceConsumer(base::TraceConsumer* c) override {}

 protected:
  void ChannelPrintFormattedStringImpl(
      perftools::tracing::channels::ChannelID channel_id,
      base::TraceStringFormatter, absl::SourceLocation) override {}
  void ChannelPrintLiteralImpl(perftools::tracing::channels::ChannelID,
                               const char*, absl::SourceLocation) override {}
  void ChannelPrintStringViewImpl(perftools::tracing::channels::ChannelID,
                                  absl::string_view,
                                  absl::SourceLocation) override {}

  perftools::tracing::TraceBuffer* GetAnnotationMap() override {
    return nullptr;
  }
  void NotifyTraceConsumers() override {}

 private:
  NoopTracer(const NoopTracer&) = delete;
  NoopTracer& operator=(const NoopTracer&) = delete;
};

static absl::once_flag nooptracer_init;
base::Tracer* noop_tracer_instance = nullptr;

void CreateNoopTracer() { noop_tracer_instance = new NoopTracer; }

}  // namespace

namespace base {

Tracer* GetNoopTracer() {
  absl::call_once(nooptracer_init, &CreateNoopTracer);
  return noop_tracer_instance;
}

std::unique_ptr<Tracer> GetNoopTracerForTesting() {
  return std::make_unique<NoopTracer>(/*start_tracer=*/true);
}

Tracer::Tracer(const TraceContext& tc) {
  span_id_ = tc.rpc_id();
  parent_span_id_ = tc.parent_rpc_id();
  trace_id_ = tc.global_id();
  trace_mask_ = tc.mask();
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

Tracer::~Tracer() {
  ABSL_RAW_CHECK(0 == ref_count_.load(std::memory_order_relaxed),
                 "Deleting a Tracer with a non-zero refcount");
}

void Tracer::UnrefSlow() {
  // If the request previously had its unref_time set, assume it was
  // already logged and entered into the history.
  if (has_unref_time()) {
    delete this;
    return;
  }

  // Checking has_stop_time() first avoids needlessly reading the clock when we
  // already have a stop time.
  if (has_start_time() && !has_stop_time()) {
    SetStopTimeNow();
  }

  // Disable causality aware trace events from tracer tear-down code.
  perftools::tracing::ScopedDisableTraceEvents scoped;

  // Get the notification and unregister it so it doesn't get invoked again.
  TracerNotification* notify =
      notification_.exchange(nullptr, std::memory_order_acq_rel);
  if (notify != nullptr && notify->TakeOwnershipBeforeDestroy(this)) {
    // The receiver revived the Tracer. It now owns the Tracer, so the Tracer
    // may be deleted at any moment. It is no longer safe to access this Tracer.
    return;
  }

  // This is the finish of a referenced Tracer's "ordinary" life span.
  // The implementation is permitted to create new references to the
  // object during OnRefCountZero(), but the unref time must be set
  // so that the next time the reference count falls to zero, the
  // object will be deleted in the branch above.
  OnRefCountZero();
}

bool Tracer::NotifyBeforeDestroy(TracerNotification* new_notify,
                                 NotificationAccess) {
  if (new_notify == nullptr) return false;
  TracerNotification* old_notify = nullptr;
  // Only a single notification can be registered. Any subsequent attempts to
  // override an existing notification will fail.
  bool replaced = notification_.compare_exchange_strong(
      old_notify, new_notify, std::memory_order_acq_rel,
      std::memory_order_acquire);
  // Return true (success) if a new notification was successfully registered OR
  // if the existing and new notifications were the same anyways.
  return replaced || old_notify == new_notify;
}

void Tracer::UpdateMask(uint32_t add, uint32_t remove) {
  // The tracer may be shared between trace contexts, so we must update its mask
  // atomically.
  uint32_t old_mask = trace_mask_.load(std::memory_order_relaxed);
  uint32_t new_mask;
  do {
    new_mask = (old_mask & ~remove) | add;
  } while (!trace_mask_.compare_exchange_weak(old_mask, new_mask,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed));
}

void Tracer::SetStartTime(absl::Time time) {
  start_time_ = time;
  unref_time_.store(base::TimeUnixNanos(), std::memory_order_relaxed);
  stop_time_.store(base::TimeUnixNanos(), std::memory_order_relaxed);
  ABSL_RAW_DCHECK(has_start_time(), "Invalid start time was set");
  XRAY_CAPTURE_RPC_START(this);  // should be after setting start_time_
}

void Tracer::set_inherited_initiator_id(uint64_t value) {
  // Reset the bit indicating this span is initiating the trace.
  value &= ~kTraceInitiatingSpan;
  initiator_id_.store(value, std::memory_order_relaxed);
}

void Tracer::GetTraceContextStackTraces(
    std::vector<ReferenceTracker::StackTrace>* traces) const {
  tracker_.GetReferenceTraces(traces);
}

void Tracer::set_initiator_id_on_child_trace(uint64_t value) {
  // Add the bits indicating this is a linked trace and the span is its root
  // (the decision to sample was propagated from the parent at this span).
  value |= kInitiatedByLinkContexts;
  value |= kTraceInitiatingSpan;
  initiator_id_.store(value, std::memory_order_relaxed);
}

void Tracer::set_invalid_inherited_initiator_id() {
  // The initiator id has been lost. Introduce a new initiator id based on prod
  // UID if available, tagged with a marker that remembers that we synthesized
  // an initiator. See <link> for details. If prod UID is
  // not available, use kInitiatorNotSetByParent as a fallback.
  std::optional<uint64_t> effective_uid = internal::GetEffectiveUserId();
  uint64_t initiator_id = effective_uid.has_value()
                              ? ((*effective_uid & kInitiatorValueMask) |
                                 kInitiatorTypeProdUid | kAdoptedInitiatorId)
                              : kInitiatorNotSetByParent;
  initiator_id_.store(initiator_id, std::memory_order_relaxed);
}

void Tracer::set_initiator_id() {
  std::optional<uint64_t> effective_uid = internal::GetEffectiveUserId();
  uint64_t initiator_id =
      effective_uid.has_value()
          ? ((*effective_uid & kInitiatorValueMask) | kInitiatorTypeProdUid)
          : 0;
  initiator_id_.store(initiator_id | kTraceInitiatingSpan,
                      std::memory_order_relaxed);
}

void Tracer::set_initiated_by_batch_sampling() {
  initiator_id_.fetch_or(kBatchSampling, std::memory_order_relaxed);
}

void Tracer::set_initiator_id_string(uint32_t hash) {
  // There may be other unrelated bits already set in the initiator ID metadata,
  // which we don't want to change.
  constexpr uint64_t kUpdateBits =
      kInitiatorTypeMask | kTraceInitiatingSpan | kInitiatorValueMask;
  const uint64_t new_initiator_id =
      uint64_t{hash} | kTraceInitiatingSpan | kInitiatorTypeStringHash;
  initiator_id_.store(
      (initiator_id() & ~kUpdateBits) | (new_initiator_id & kUpdateBits),
      std::memory_order_relaxed);
}

absl::Duration Tracer::Elapsed() const {
  ABSL_RAW_DCHECK(IsStarted(), "Tracer is not started.");
  absl::Time stop = has_stop_time() ? get_stop_time() : absl::Now();
  return stop - get_start_time();
}

void Tracer::enable_query_cost_tracing() {
  initiator_id_.fetch_or(kQueryCostTracingEnabled, std::memory_order_relaxed);
}

void Tracer::internal_enable_shared_fate() {
  initiator_id_.fetch_or(kSharedFate, std::memory_order_relaxed);
}

void Tracer::set_high_value_trace() {
  initiator_id_.fetch_or(kHighValueTrace, std::memory_order_relaxed);
}

void Tracer::set_speculative_root() {
  initiator_id_.fetch_or(kSpeculativeRoot, std::memory_order_relaxed);
}

void Tracer::set_initiated_by_tracing_cookie() {
  initiator_id_.fetch_or(kTracingCookie | kTraceInitiatingSpan,
                         std::memory_order_relaxed);
}

void Tracer::set_initiated_by_bucketed_sampling() {
  initiator_id_.fetch_or(kBucketedSampling, std::memory_order_relaxed);
}

ABSL_XRAY_ALWAYS_INSTRUMENT void Tracer::SetStopTime(absl::Time time) {
  ABSL_RAW_DCHECK(IsAlive(), "Tracer is not alive.");
  if (!has_stop_time()) {
    auto stop_time = base::TimeUnixNanos::FromTime(time);
    stop_time_.store(stop_time, std::memory_order_relaxed);
    XRAY_CAPTURE_RPC_STOP(this);  // should be after setting stop_time_
  }
}

void Tracer::set_status(absl::Status status) {
  if (!status.ok()) {
    SetErrorStatus();  // NOLINT
  }
}

absl::Status Tracer::status() const {
  if (GetErrorStatus()) {  // NOLINT
    return absl::UnknownError("unknown");
  }
  return absl::OkStatus();
}

void Tracer::EmitPreformattedHTML(IOBuffer* output) const {}

void Tracer::SetErrorStatus() {}

bool Tracer::GetErrorStatus() const { return false; }

// We keep the vtable and empty destructors out of any/every .o that includes
// tracer.h by declaring these empty destructors in the .cc file)
TraceConsumer::~TraceConsumer() {}
TraceEntrySink::~TraceEntrySink() {}
TraceEntrySource::~TraceEntrySource() {}

void TraceEntrySink::EmitLazy(
    perftools::tracing::channels::ChannelID channel_id, absl::Time time,
    void (*function_ptr)(void* arg, std::string* out), void* arg) {
  std::string data;
  (*function_ptr)(arg, &data);
  Emit(channel_id, time, data.data(), data.size());
}

}  // namespace base
