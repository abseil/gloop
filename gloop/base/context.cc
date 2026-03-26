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

#include "gloop/base/context.h"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/time.h"
#include "gloop/base/censushandle.h"
#include "gloop/base/context_access.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/util/refcount/reffed_ptr.h"

// TODO: remove once properly soaked in production.
ABSL_FLAG(bool, harden_with_context, true,
          "Harden WithContext against invalid use cases");

// We do not currently have a preprocessor symbol for "Context is stubbed out".
// This is expressed in the BUILD file using select() but the same criteria are
// not revealed to the preprocessor (it is roughly rooted in "static thread
// local is supported"). For now, BASE_CONTEXT_HAVE_DEADLINE is the simplest
// feature offered by the real Context implementation, which serves as a
// reasonable signal that we do not need the stubs.
#if !BASE_CONTEXT_HAVE_DEADLINE

// This file provides an empty implementation of Context that enables
// base/callback.h to be used in mobile platforms. Note that this is possible
// because callbacks do not actually use Context. Any code that relies on
// Context functionality will *not* work.

namespace base {

// Note that in this stub implementation we default initialize the TraceContext
// even though thread initialization was requested. This is ok as Context
// provides no functionality in this configuration.
Context::Context(ThreadInitType, perftools::tracing::StringRef) {}

Context::~Context() {}

void swap(Context&, Context&) noexcept {}

void SwapCurrentContext(Context* c) {}

void RestoreCurrentContext(Context* c) {}

WithContext::WithContext(const Context&, perftools::tracing::StringRef) {}

WithContext::~WithContext() {}

const TraceContext* CurrentTraceContextNoAlloc() { return nullptr; }

}  // namespace base

#else  // BASE_CONTEXT_HAVE_DEADLINE

namespace base {

namespace {

// Keep thread-local state on the current context. The current TraceContext
// now points to the trace state within the current generic context.
STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(Context, per_thread_context, ());

Context* InlineCurrent() { return per_thread_context.pointer(); }

}  // namespace

namespace internal {

absl::NoDestructor<Context> background_context{Context::kDefault};

Context* absl_nonnull SwapContext(ContextAccess access,
                                  Context* absl_nonnull context,
                                  perftools::tracing::StringRef label) {
  Context* current = per_thread_context.pointer();
  per_thread_context.set_pointer(context);
  return current;
}

Context* absl_nonnull RestoreContext(ContextAccess access,
                                     Context* absl_nonnull context) {
  Context* current = per_thread_context.pointer();
  per_thread_context.set_pointer(context);
  return current;
}

}  // namespace internal

Context::Context(ThreadInitType, perftools::tracing::StringRef thread_name)
    : Context(*InlineCurrent(), thread_name) {}

Context::Context(const Context& c, perftools::tracing::StringRef thread_name)
    : tc_(c.tc_), deadline_(c.deadline_), thread_status_(c.thread_status_) {}

Context::Context(const Context& c) = default;
Context::Context(Context&& c) noexcept = default;
Context& Context::operator=(const Context& c) = default;
Context& Context::operator=(Context&& c) noexcept = default;
Context::~Context() = default;

void swap(Context& lhs, Context& rhs) noexcept {
  using std::swap;

  swap(lhs.tc_, rhs.tc_);
  swap(lhs.deadline_, rhs.deadline_);
  swap(lhs.thread_status_, rhs.thread_status_);
}

void Context::SwapDeadline(absl::Time* deadline) {
  using std::swap;
  swap(deadline_, *deadline);
}

// Defined in <path>
ABSL_ATTRIBUTE_WEAK void CurrentCensusHandleChanging(TraceContext*,
                                                     CensusHandle*) {}

void Context::SwapCensusHandle(CensusHandle* handle) {
  using std::swap;
#ifdef ENABLE_CONTEXT_ORIGIN
  CurrentCensusHandleChanging(&tc_, handle);
#endif
  swap(tc_.census_handle_, *handle);
}

const Context& CurrentContext() { return *InlineCurrent(); }

const char* CurrentThreadStatus() {
  const Context* ctx = per_thread_context.safe_pointer();
  return (ctx == nullptr) ? nullptr : ctx->thread_status();
}

void SetCurrentThreadStatus(const char* thread_status) {
  InlineCurrent()->set_thread_status(thread_status);
}

TraceContext* internal::MutableCurrentContext::MutableCurrentTrace() {
  return &InlineCurrent()->tc_;
}

const TraceContext* CurrentTraceContextNoAlloc() {
  const Context* c = per_thread_context.safe_pointer();
  return c ? c->trace() : nullptr;
}

// Defined in <path>
ABSL_ATTRIBUTE_WEAK void CurrentTraceContextChanging(const TraceContext*,
                                                     TraceContext*) {}

ABSL_XRAY_ALWAYS_INSTRUMENT void SwapCurrentContext(Context* c) {
  using std::swap;
  Context* current = InlineCurrent();
  swap(*current, *c);
}

ABSL_XRAY_ALWAYS_INSTRUMENT void RestoreCurrentContext(Context* c) {
  using std::swap;
  Context* current = InlineCurrent();
  *current = std::move(*c);
}

ContextBuilder& ContextBuilder::set_trace_context(TraceContext tc) {
  context_.tc_ = std::move(tc);
  return *this;
}

ContextBuilder& ContextBuilder::set_deadline(const absl::Time deadline) {
  context_.set_deadline(deadline);
  return *this;
}

ContextBuilder& ContextBuilder::set_census_handle(CensusHandle handle) {
  context_.set_census_handle(std::move(handle));
  return *this;
}

ContextBuilder& ContextBuilder::set_thread_status(
    const char* absl_nullable const thread_status) {
  context_.thread_status_ = thread_status;
  return *this;
}

Context ContextBuilder::BuildValue() { return std::move(context_); }

absl::Time Context::deadline() const { return deadline_; }

const char* Context::thread_status() const { return thread_status_; }

void Context::set_thread_status(const char* thread_status) {
  thread_status_ = thread_status;
}

WithContext::WithContext(const Context& switch_to,
                         perftools::tracing::StringRef label)
    : current_(new Context(switch_to)),
      previous_(internal::SwapContext(ContextAccess(), current_, label)) {}

WithContext::WithContext(Context&& switch_to,
                         perftools::tracing::StringRef label)

    : current_(new Context(std::move(switch_to))),
      previous_(internal::SwapContext(ContextAccess(), current_, label)) {}

WithContext::~WithContext() {
  Context* current = internal::RestoreContext(ContextAccess(), previous_);
  if (current != current_) {
    if (absl::GetFlag(FLAGS_harden_with_context)) {
      LOG(FATAL) << "Illegally scoped `base::WithContext`. "
                    "Use --noharden_with_context to disable this hardening";
    } else {
      LOG_EVERY_N_SEC(ERROR, 60) << "Illegally scoped `base::WithContext`.";
    }
  }
  delete current;
}

WithTraceContext::WithTraceContext(const TraceContext& switch_to,
                                   perftools::tracing::StringRef label) {
  TraceContext* current = InlineCurrent()->trace();
  if (&switch_to != current) {
    new (&previous_) TraceContext(std::move(*current));
    current = new (current) TraceContext(switch_to);
  } else {
    // This should be rare (and ideally illegal). Just make a copy of current.
    new (&previous_) TraceContext(*current);
  }
#ifdef ENABLE_CONTEXT_ORIGIN
  base::CurrentTraceContextChanging(&previous_, current);
#endif
}

WithTraceContext::WithTraceContext(TraceContext&& switch_to,
                                   perftools::tracing::StringRef label) {
  TraceContext* current = InlineCurrent()->trace();
  if (&switch_to != current) {
    new (&previous_) TraceContext(std::move(*current));
    current = new (current) TraceContext(std::move(switch_to));
  } else {
    // Nobody should ever 'consume' the current context. Still, DTRT.
    DLOG(FATAL) << "Illegal call consuming <CurrentTraceContext>";
    new (&previous_) TraceContext(*current);
  }
}

WithTraceContext::~WithTraceContext() {
  TraceContext* current = InlineCurrent()->trace();
  *current = std::move(previous_);
}

WithDeadline::WithDeadline(absl::Time new_deadline)
    : swapped_deadline_(new_deadline), is_deadline_swapped_(false) {
  Context* current_context = InlineCurrent();
  // Check to see if the existing deadline is nearer than the new one.
  if (current_context->deadline() <= new_deadline) {
    return;
  }
  is_deadline_swapped_ = true;
  current_context->SwapDeadline(&swapped_deadline_);
}

WithDeadline::~WithDeadline() {
  if (is_deadline_swapped_) {
    Context* current_context = InlineCurrent();
    current_context->SwapDeadline(&swapped_deadline_);
  }
}

WithCensusHandle::WithCensusHandle(const CensusHandle& handle)
    : swapped_handle_(handle) {
  InlineCurrent()->SwapCensusHandle(&swapped_handle_);
}

WithCensusHandle::WithCensusHandle(CensusHandle&& handle)
    // Keep std::move() here, in preparation for refcounted handles.
    : swapped_handle_(std::move(handle)) {
  InlineCurrent()->SwapCensusHandle(&swapped_handle_);
}

WithCensusHandle::~WithCensusHandle() {
  InlineCurrent()->SwapCensusHandle(&swapped_handle_);
}

WithThreadStatus::WithThreadStatus(const char* status)
    : swapped_status_(InlineCurrent()->thread_status()) {
  InlineCurrent()->set_thread_status(status);
}

WithThreadStatus::~WithThreadStatus() {
  InlineCurrent()->set_thread_status(swapped_status_);
}

const CensusHandle& CensusAccess::GetCurrentHandle() {
#if BASE_HAVE_TRACECONTEXT
  return InlineCurrent()->trace()->census_handle_;
#else
  static CensusHandle* dummy_ch = new CensusHandle;
  return *dummy_ch;
#endif
}

void CensusAccess::SwapCurrentHandle(CensusHandle* h) {
#if BASE_HAVE_TRACECONTEXT
  using std::swap;
  swap(InlineCurrent()->trace()->census_handle_, *h);
#endif
}

const CensusHandle& CensusAccess::GetHandleFromContext(const Context& context) {
  return context.census_handle_ref();
}

}  // namespace base

#endif  // !BASE_CONTEXT_HAVE_DEADLINE
