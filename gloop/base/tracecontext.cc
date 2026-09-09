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

// This module initializes the TraceContext thread-specific key
// and defines the destructor for per-thread TraceContext blocks

#include "gloop/base/tracecontext.h"

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <optional>
#include <string>
#include <utility>

#if BASE_HAVE_TRACECONTEXT

#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "base/tracecontext-ktrace.h"
#include "gloop/base/censushandle.h"
#include "gloop/base/context_access.h"
#include "gloop/base/tracer.h"
#include "gloop/perftools/tracing/sync_context.h"
#include "gloop/perftools/tracing/trace_event_listener.h"

#ifdef ENABLE_CONTEXT_ORIGIN
#include "gloop/base/context_origin.h"
#endif

const uint32_t TraceContext::kTraceMaskRPCTracingOn;
const uint32_t TraceContext::kTraceMaskNonUniform;
const uint32_t TraceContext::kTraceMaskVerboseTrace;
const uint32_t TraceContext::kTraceMaskClientRootedTrace;
const uint32_t TraceContext::kTraceMaskGoogleWideFaultInjection;
const uint32_t TraceContext::kTraceMaskTransientTracingOn;
const uint32_t TraceContext::kTraceMaskTransientTracingOffset;
const uint32_t TraceContext::kTraceMaskSherlogTracingOn;
const uint32_t TraceContext::kTraceMaskSkeletalTracingOn;

const uint32_t TraceContext::kTraceMaskTagWidth;
const uint32_t TraceContext::kTraceMaskTagOffset;

// Wrapper to base::GetNoopTracer() as including base/tracer.h
// in tracecontext.h won't compile.
base::Tracer* TraceContext::GetNoopTracer() { return base::GetNoopTracer(); }

void TraceContext::RefTracer() { get_raw_tracer()->Ref(get_tracer_owner()); }

// Copy from the current thread to this context
void TraceContext::FromThread() { *this = *Current(); }

void TraceContext::set_rpc_id(uint64_t rpc_id) {
#if defined(__linux__) && !defined(__ANDROID__)
  if (ABSL_PREDICT_FALSE(base::ktrace::ShouldAddKtraceAnnotations())) {
    if (this == base::CurrentTraceContextNoAlloc()) {
      // Sixteen bits of argument go into ktrace
      // Change thread-local tracecontext.rpc_id_:ppid(marker)  pid(new rpc_id)
      if (this->rpc_id_ != rpc_id) {
        KTRACE_SYSCALL_A(base::ktrace::kKtraceTraceContextSetRPC4);
        KTRACE_SYSCALL_B(base::ktrace::PackRpcidTo16(rpc_id_));
      }
    }
  }
#endif
  // If the rpc_id changes, our tracer_ is no longer valid.
  // Note that trace event listeners can span any number of rpc ids.
  AbandonTracer();
  rpc_id_ = rpc_id;
}

void TraceContext::set_parent_rpc_id(uint64_t parent_rpc_id) {
  // If the parent_rpc_id changes, our tracer_ is no longer valid.
  // Note that trace event listeners can span any number of rpc ids.
  AbandonTracer();
  parent_rpc_id_ = parent_rpc_id;
}

void TraceContext::set_global_id(uint64_t global_id) {
  // If the global_id changes, our tracer_ is no longer valid, and we reset
  // all trace event listeners as they should not span multiple traces.
  sync_context_ = {};
  AbandonTracer();
  global_id_ = global_id;
}

void TraceContext::UpdateMask(uint32_t add, uint32_t remove) {
  mask_ = static_cast<uint64_t>((static_cast<uint32_t>(mask_) & ~remove) | add);
  // The tracer may be shared between trace contexts, so mask_ may differ from
  // get_raw_tracer()->trace_mask_. But when a tracer is attached, its mask is
  // used instead of mask_. Therefore we must update the tracer mask here.
  if (auto* tracer = get_raw_tracer(); tracer != nullptr) {
    tracer->UpdateMask(add, remove);
  }
}

void TraceContext::AdoptTracer(base::Tracer* tracer) {
  // The contract states that adopted base::Tracer*s must not be refcounted
  // until they're adopted by the TraceContext system, as only the TraceContext
  // has access to the private reference counting methods (as a friend class).
  ABSL_RAW_DCHECK(tracer, "Tracer must not be null");
  ABSL_RAW_DCHECK(tracer->ref_count_.load(std::memory_order_relaxed) == 0,
                  "Tracer must not have references.");

  if (!CheckTracerAttributesMatch(*tracer)) {
    UnsafeSetTracerAttributes(*tracer);
  }

  // Replace any current tracer with the new one.
  this->ReplaceTracer(tracer);
}

void TraceContext::AbandonTracer() {
  if (auto* tracer = get_raw_tracer(); tracer != nullptr) {
    // syncs back the tracing mask from the tracer to the context so that the
    // returned value of mask() would not change after abandoning the tracer.
    mask_ = static_cast<uint64_t>(tracer->trace_mask());
    // Tracer::Unref() may call lots of code to log data, and
    // we do not want that touching the tracer_ which is about
    // to be deleted.  So we first null out the tracer_ field.

    // We need to ensure a signal handler cannot read a stale or incomplete
    // Tracer ptr if it fires around this code. To do this, call set_tracer() to
    // atomically update the Tracer, then use a signal fence to ensure none of
    // the subsequent code can be reordered before it.
    set_tracer(nullptr);
    std::atomic_signal_fence(std::memory_order_seq_cst);
    tracer->Unref(get_tracer_owner());
  }
}

void TraceContext::AddTraceEventListener(
    base::ContextAccess, perftools::tracing::TraceEventListener* listener) {
  using SyncContextAccess = perftools::tracing::core::SyncContext::Access;
  auto* tc = base::internal::MutableCurrentContext::MutableCurrentTrace();
  tc->sync_context_.AddListenerToCurrent(SyncContextAccess(), tc->rpc_id_,
                                         listener);
}

void TraceContext::RemoveTraceEventListener(
    base::ContextAccess, perftools::tracing::TraceEventListener* listener) {
  using SyncContextAccess = perftools::tracing::core::SyncContext::Access;
  auto* tc = base::internal::MutableCurrentContext::MutableCurrentTrace();
  tc->sync_context_.RemoveListenerFromCurrent(SyncContextAccess(), listener);
}

uint32_t TraceContext::TracerMask() const {
  return get_raw_tracer()->trace_mask();
}

void TraceContext::set_status(const absl::Status& status) const {
  if (InternalHasTracer()) {
    get_raw_tracer()->set_status(status);
  }
}

std::optional<uint64_t> TraceContext::initiator_id() const {
  if (!is_traced_any_kind(SkeletalTracingAccess()) || !InternalHasTracer()) {
    return std::nullopt;
  }
  uint64_t tracer_initiator = get_raw_tracer()->initiator_id();
  if (tracer_initiator == base::Tracer::kNotSampled ||
      tracer_initiator == base::Tracer::kInitiatorNotSetByParent) {
    return std::nullopt;
  }
  return tracer_initiator;
}

std::optional<double> TraceContext::inverse_sampling_probability() const {
  if (!is_traced_or_speculatively_traced() || tracer_ == nullptr) {
    return std::nullopt;
  }
  return get_raw_tracer()->inverse_sampling_probability();
}

void TraceContext::ReplaceTracer(base::Tracer* tracer) {
  if (tracer == this->tracer_) {
    // Then we already have a reference to it, and Ref-ing it again
    // will confuse the reference-tracking in Tracer and check-fail.
    return;
  }

  if (nullptr != tracer) {
    tracer->Ref(get_tracer_owner());
  }
  // Now remove a reference to the old tracer (possibly freeing it).
  this->AbandonTracer();
  // Finally overwrite the old with the new.
  this->tracer_ = tracer;
}

void swap(TraceContext& lhs, TraceContext& rhs) noexcept {
  // If we are tracking ownership of trace contexts, swap our reference
  // tracking between lhs and rhs.
  TraceContext::SwapRefs(&lhs, &rhs);

  using std::swap;

  swap(lhs.rpc_id_, rhs.rpc_id_);
  swap(lhs.parent_rpc_id_, rhs.parent_rpc_id_);
  swap(lhs.global_id_, rhs.global_id_);
  swap(lhs.mask_, rhs.mask_);
  swap(lhs.census_handle_, rhs.census_handle_);
  // This operation may affect the TraceContext currently in TLS. Use
  // set_tracer() to ensure the Tracer ptr updates occur atomically when built
  // with C++20.
  base::Tracer* tmp = lhs.get_raw_tracer();
  lhs.set_tracer(rhs.get_raw_tracer());
  rhs.set_tracer(tmp);
  swap(lhs.sync_context_, rhs.sync_context_);
#ifdef ENABLE_CONTEXT_ORIGIN
  swap(lhs.origin_, rhs.origin_);
#endif
}

void TraceContext::Reset() {
  // This will implicitly call AbandonTracer():
  this->set_rpc_id(0);
  ABSL_RAW_DCHECK(this->tracer_ == nullptr, "Tracer must be null.");
  this->parent_rpc_id_ = 0;
  this->global_id_ = 0;
  this->mask_ = 0;
  // Replace current handle inside "this" by a default handle.
  this->census_handle_ = CensusHandle();
  this->sync_context_ = {};
#ifdef ENABLE_CONTEXT_ORIGIN
  this->origin_ = base::ContextOrigin();
#endif
}

void TraceContext::MoveTracer(TraceContext* from, TraceContext* to) {
  // If we are tracking ownership of trace contexts, record that we now have a
  // reference on the tracer and that &c does not.
  if (to->tracer_ != nullptr) {
    to->tracer_->SwapRefOwner(from->get_tracer_owner(), to->get_tracer_owner());
  }

  // When moving, we steal the tracer refcount from the other context and must
  // be sure that its destructor does not unref the tracer.
  from->tracer_ = nullptr;
}

void TraceContext::SwapRefs(TraceContext* lhs, TraceContext* rhs) {
  if (lhs->tracer_ == rhs->tracer_) {
    return;
  }
  if (lhs->tracer_ != nullptr) {
    lhs->tracer_->SwapRefOwner(lhs->get_tracer_owner(),
                               rhs->get_tracer_owner());
  }
  if (rhs->tracer_ != nullptr) {
    rhs->tracer_->SwapRefOwner(rhs->get_tracer_owner(),
                               lhs->get_tracer_owner());
  }
}

void TraceContext::SetTraceLevel(TraceLevel level) {
  if (level >= kTransientTracing) {
    mask_ |= kTraceMaskTransientTracingOn;
  } else {
    mask_ &= ~kTraceMaskTransientTracingOn;
  }
  if (level == kPersistentTracing) {
    mask_ |= kTraceMaskRPCTracingOn;
  } else {
    mask_ &= ~kTraceMaskRPCTracingOn;
  }
}

TraceContext& TraceContext::operator=(const TraceContext& c) {
  // We never allow assign `into` the thread local, swapping thread local
  // context should only ever happen in the designated Swap and Restore APIs
  // which do never use copy assign logic.
  DCHECK_NE(this, base::CurrentTraceContextNoAlloc());

  if (this == &c) {
    return *this;
  }
  ReplaceTracer(c.tracer_);

  this->rpc_id_ = c.rpc_id_;
  this->parent_rpc_id_ = c.parent_rpc_id_;
  this->global_id_ = c.global_id_;
  this->mask_ = c.mask_;
  this->census_handle_ = c.census_handle_;
  sync_context_ = c.sync_context_;
#ifdef ENABLE_CONTEXT_ORIGIN
  this->origin_ = c.origin_;
#endif
  return *this;
}

std::string TraceContext::DebugString() const {
  char tmp[128];
  SignalSafeDebugString(tmp, sizeof(tmp));
  return tmp;
}

int TraceContext::SignalSafeDebugString(char* out, size_t n) const {
  // Note(llib): snprintf isn't actually async-signal safe.  Note the comments
  // in raw_logging.cc.  This 'static' use of snprintf ought to be safe.
  return absl::SNPrintF(out, n,
                        "[rpc_id: 0x%016x, parent_rpc_id: 0x%016x, "
                        "global_id: 0x%016x, mask: 0x%08x]",
                        rpc_id(), parent_rpc_id(), global_id(), mask());
}

bool TraceContext::CheckTracerAttributesMatch(const base::Tracer& tracer) {
  auto check_eq = [&](const auto& target, const auto source, auto name) {
    if (target == source) {
      return true;
    }
    // TODO: b/467753116 - escalate this to DFATAL. Right now this will cause
    // too many test failures because we are sloppy when initializing Tracers
    // and TraceContexts. Once we tighten up the initialization protocol
    // (forcing tracers to set the correct, immutable attributes on
    // construction) we can get rid of this function completely.
    if (VLOG_IS_ON(1)) {
      LOG_EVERY_N_SEC(ERROR, 60)
          << "Attempted to adopt a tracer with wrong " << name
          << ". Tracer value is " << target << ", TraceContext value is "
          << source << ". Other fields may also be wrong.";
    }
    return false;
  };
  return check_eq(tracer.span_id_, rpc_id(), "span_id") &&
         check_eq(tracer.parent_span_id_, parent_rpc_id(), "parent_span_id") &&
         check_eq(tracer.trace_id_, global_id(), "trace_id") &&
         check_eq(tracer.trace_mask_.load(std::memory_order_relaxed), mask(),
                  "mask");
}

void TraceContext::UnsafeSetTracerAttributes(base::Tracer& tracer) {
  // TODO: b/467753116 - Force tracers to correctly initialize these fields and
  // remove this function.
  tracer.span_id_ = rpc_id();
  tracer.parent_span_id_ = parent_rpc_id();
  tracer.trace_id_ = global_id();
  tracer.trace_mask_.store(mask(), std::memory_order_relaxed);
}

namespace {
// String representations of the trace levels.
static constexpr absl::string_view kNoTracingStr = "NO_TRACING";
static constexpr absl::string_view kTransientTracingStr = "TRANSIENT_TRACING";
static constexpr absl::string_view kPersistentTracingStr = "PERSISTENT_TRACING";
}  // namespace

bool AbslParseFlag(absl::string_view text,
                   TraceContext::TraceLevel* trace_level, std::string* error) {
  if (text == kNoTracingStr) {
    *trace_level = TraceContext::kNoTracing;
    return true;
  }
  if (text == kTransientTracingStr) {
    *trace_level = TraceContext::kTransientTracing;
    return true;
  }
  if (text == kPersistentTracingStr) {
    *trace_level = TraceContext::kPersistentTracing;
    return true;
  }
  *error = "Unknown trace level.";
  return false;
}

std::string AbslUnparseFlag(TraceContext::TraceLevel trace_level) {
  switch (trace_level) {
    case TraceContext::kNoTracing:
      return std::string(kNoTracingStr);
    case TraceContext::kTransientTracing:
      return std::string(kTransientTracingStr);
    case TraceContext::kPersistentTracing:
      return std::string(kPersistentTracingStr);
    default:
      return absl::StrCat(trace_level);
  }
}

#endif  // BASE_HAVE_TRACECONTEXT
