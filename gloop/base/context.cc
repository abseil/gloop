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
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional_ref.h"
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

void SetThreadContext(ThreadContextAccess, Context,
                      perftools::tracing::StringRef) {}

Context ExchangeThreadContext(ThreadContextAccess, Context,
                              perftools::tracing::StringRef) {
  return Context{};
}

WithContext::WithContext(const Context&, perftools::tracing::StringRef) {}

WithContext::~WithContext() {}

}  // namespace base

#else  // BASE_CONTEXT_HAVE_DEADLINE

namespace base {

// Keep thread-local state on the current context. The current TraceContext
// now points to the trace state within the current generic context.
STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(Context, per_thread_context, ());

// Defined in <path>
ABSL_ATTRIBUTE_WEAK void CurrentTraceContextChanging(const TraceContext*,
                                                     TraceContext*) {}

// Invokes CurrentTraceContextChanging() under copts=-DENABLE_CONTEXT_ORIGIN
static void NotifyTraceContextChanging(
    [[maybe_unused]] const TraceContext* from,
    [[maybe_unused]] TraceContext* to) {
#ifdef ENABLE_CONTEXT_ORIGIN
  CurrentTraceContextChanging(from, to);
#endif
}

namespace internal {

absl::NoDestructor<Context> background_context{Context::kDefault};

// TODO: even though this function only swaps the context pointer
// at its heart, the various pieces of notification logic do modify the trace
// context non-atomically, which may cause problems for signal handlers using
// the result of CurrentTraceContextNoAlloc if they aren't careful only to touch
// the parts not modified by that notification logic. This should at least be
// documented as a (vague) requirement/warning on CurrentTraceContextNoAlloc.
// Note that this TODO applies to pretty much all of the modification functions
// in this file (including RestoreContext), not just this one.
Context* absl_nonnull SwapContext(ContextAccess access,
                                  Context* absl_nonnull const context,
                                  const perftools::tracing::StringRef label) {
  Context* absl_nonnull const current = per_thread_context.pointer();

  // Arrange for listeners to know that the context is changing.
  NotifyTraceContextChanging(current->trace(), context->trace());
  current->trace()->BeforeSwapCurrent(access, context->trace_context());

  const absl::Cleanup after = [&] {
    context->trace()->AfterSwapCurrent(access, label);
  };

  // Publish the new context object.
  per_thread_context.set_pointer(context);

  return current;
}

Context* absl_nonnull RestoreContext(ContextAccess access,
                                     Context* absl_nonnull const context) {
  Context* absl_nonnull const current = per_thread_context.pointer();

  // Arrange for listeners to know that the context is changing.
  current->trace()->BeforeRestoreCurrent(access, context->trace_context());

  const absl::Cleanup after = [&] {
    context->trace()->AfterRestoreCurrent(access);
  };

  // Publish the new context object.
  per_thread_context.set_pointer(context);

  return current;
}

}  // namespace internal

Context::Context(ThreadInitType, perftools::tracing::StringRef thread_name)
    : Context(CurrentContext(), thread_name) {}

Context::Context(const Context& c, perftools::tracing::StringRef thread_name)
    : tc_(c.tc_), deadline_(c.deadline_) {}

Context::Context(const Context& c) = default;
Context::Context(Context&& c) noexcept = default;
Context& Context::operator=(const Context& c) = default;
Context& Context::operator=(Context&& c) noexcept = default;
Context::~Context() = default;

// Note that it's important that this function not call any code that may access
// the thread-local context. See SwapCurrentContextWithoutNotifying.
void swap(Context& lhs, Context& rhs) noexcept {
  using std::swap;

  swap(lhs.tc_, rhs.tc_);
  swap(lhs.deadline_, rhs.deadline_);
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

const Context& CurrentContext() { return *per_thread_context.pointer(); }

const TraceContext* CurrentTraceContextNoAlloc() {
  const Context* const absl_nullable context =
      per_thread_context.safe_pointer();

  return context != nullptr ? &context->trace_context() : nullptr;
}

// TODO: promiscuous use of APIs backed by this function will very
// likely remain a signal-safety issue even after fixing this file. Document
// this.
TraceContext* internal::MutableCurrentContext::MutableCurrentTrace() {
  return &per_thread_context.pointer()->tc_;
}

// Swap the current context with the given object, without doing any sort of
// trace context notifications.
static void SwapCurrentContextWithoutNotifying(Context& c) {
  // Unpublish the context while we swap it out, ensuring that
  // CurrentThreadStatusNoAlloc and CurrentTraceContextNoAlloc can't see an
  // inconsistent state if they're called from a signal handler that interrupts
  // us while swapping.
  //
  // Note that we expect to meet MutateInCriticalSection's requirements of not
  // calling back in to the thread-local from the critical section except
  // through the safe_pointer method, because the swap logic doesn't call any
  // user code (http://shortn/_zVbb0GhTpO, http://shortn/_cJGuzpwdb3,
  // http://shortn/_Ug73VeFwKT). It may take locks and allocate for reference
  // tracking (http://shortn/_vF10T3sWFp), but neither of these should be
  // accessing the current context except through *NoAlloc hooks.
  per_thread_context.MutateInCriticalSection([&c](Context& current) {
    using std::swap;
    swap(current, c);
  });
}

ABSL_XRAY_ALWAYS_INSTRUMENT void SwapCurrentContext(Context* const c) {
  Context* absl_nonnull const current = per_thread_context.pointer();

  // Arrange for listeners to know that the context is changing.
  NotifyTraceContextChanging(current->trace(), c->trace());
  current->trace()->BeforeSwapCurrent(ContextAccess(), c->trace_context());

  const absl::Cleanup after = [&] {
    current->trace()->AfterSwapCurrent(base::ContextAccess());
  };

  // Swap the contents of the two context objects.
  SwapCurrentContextWithoutNotifying(*c);
}

ABSL_XRAY_ALWAYS_INSTRUMENT void RestoreCurrentContext(Context* const c) {
  Context* absl_nonnull const current = per_thread_context.pointer();

  // Arrange for listeners to know that the context is changing.
  current->trace()->BeforeRestoreCurrent(ContextAccess(), *c->trace());

  const absl::Cleanup after = [&] {
    current->trace()->AfterRestoreCurrent(ContextAccess());
  };

  // Swap the contents of the two context objects, leaving it up to the caller
  // to destroy the swapped-out version.
  SwapCurrentContextWithoutNotifying(*c);
}

ABSL_XRAY_ALWAYS_INSTRUMENT void SetThreadContext(
    ThreadContextAccess access, Context context,
    const perftools::tracing::StringRef label) {
  TraceContext* absl_nonnull const trace =
      per_thread_context.pointer()->trace();

  // Arrange for listeners to know that the context is changing.
  NotifyTraceContextChanging(trace, context.trace());
  trace->BeforeSetThreadContext(access, context.trace_context(), label);

  const absl::Cleanup after = [&] {
    trace->AfterSetThreadContext(access, label);
  };

  // Swap the contents of the two context objects, ensuring that we destroy the
  // old one outside of the critical section. Various destructors may call into
  // user code that must not access the current context within the critical
  // section.
  SwapCurrentContextWithoutNotifying(context);
}

ABSL_XRAY_ALWAYS_INSTRUMENT
Context ExchangeThreadContext(ThreadContextAccess access, Context context,
                              const perftools::tracing::StringRef label) {
  TraceContext* absl_nonnull const trace =
      per_thread_context.pointer()->trace();

  // Arrange for listeners to know that the context is changing.
  NotifyTraceContextChanging(trace, context.trace());
  trace->BeforeExchangeThreadContext(access, context.trace_context(), label);

  const absl::Cleanup after = [&] {
    trace->AfterExchangeThreadContext(access, label);
  };

  // Swap the contents of the two context objects.
  SwapCurrentContextWithoutNotifying(context);

  return context;
}

// HACK(jacobsa): provide a definition of the function declared in
// tracecontext.h, despite that header not being part of this target. The code
// there is, and long has been, incorrect unless context.cc is also linked into
// the binary.
//
// We avoid just forward declaring CurrentTraceContextNoAlloc there because it
// causes include-cleaner to fail to add an include for context.h.
const TraceContext* CurrentTraceContextNoAlloc_ForTraceContext() {
  return CurrentTraceContextNoAlloc();
}

ContextBuilder& ContextBuilder::set_trace_context(TraceContext tc) {
  context_.set_trace_context(std::move(tc));
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

Context ContextBuilder::BuildValue() { return std::move(context_); }

absl::Time Context::deadline() const { return deadline_; }

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

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context. Note that the TraceContext copy
// constructor may invoke arbitrary user code via e.g.
// TraceEventListener::GetEventListener, so can't be done inside the critical
// section.
WithTraceContext::WithTraceContext(const TraceContext& switch_to,
                                   const perftools::tracing::StringRef label) {
  TraceContext* absl_nonnull current = per_thread_context.pointer()->trace();

  // Arrange for listeners to know that the trace context is changing.
  current->BeforeSwapCurrent(ContextAccess(), switch_to);

  const absl::Cleanup after = [&] {
    NotifyTraceContextChanging(&previous_, current);
    current->AfterSwapCurrent(ContextAccess(), label);
  };

  // Rare case (which should ideally be illegal): if someone has told us to
  // switch the current context to itself then we can't use the logic below,
  // which invalidates the `switch_to` reference's contents by moving from it
  // before later using it. Just make a copy and bail out.
  if (&switch_to == current) {
    new (&previous_) TraceContext(*current);
    return;
  }

  // Move the current context into previous_, so we can restore it later in our
  // destructor.
  new (&previous_) TraceContext(std::move(*current));

  // Construct over the current context with the contents of the input.
  //
  // TODO: this is wrong for an arbitrary type, because we haven't
  // destroyed `*current` yet. Document what makes it okay for TraceContext.
  current = new (current) TraceContext(switch_to);
}

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context.
WithTraceContext::WithTraceContext(TraceContext&& switch_to,
                                   const perftools::tracing::StringRef label) {
  TraceContext* absl_nonnull current = per_thread_context.pointer()->trace();

  // Arrange for listeners to know that the context is changing.
  NotifyTraceContextChanging(current, &switch_to);
  current->BeforeSwapCurrent(ContextAccess(), switch_to);

  const absl::Cleanup after = [&] {
    current->AfterSwapCurrent(ContextAccess(), label);
  };

  // Illegal case (which we still choose to handle in optimized builds): nobody
  // should ever "consume" the current trace context.
  if (&switch_to == current) {
    DLOG(FATAL) << "Illegal call consuming <CurrentTraceContext>";
    new (&previous_) TraceContext(*current);
    return;
  }

  // Move the current context into previous_, so we can restore it later in our
  // destructor.
  new (&previous_) TraceContext(std::move(*current));

  // Construct over the current context with the contents of the input.
  //
  // TODO: this is wrong for an arbitrary type, because we haven't
  // destroyed `*current` yet. Document what makes it okay for TraceContext.
  current = new (current) TraceContext(std::move(switch_to));
}

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context.
//
// TODO: for this particular function the TODO above is not
// sufficient to avoid re-entrancy because the move assignment operator may run
// arbitrary user code in destructors.
WithTraceContext::~WithTraceContext() {
  TraceContext* absl_nonnull const current =
      per_thread_context.pointer()->trace();

  // Arrange for listeners to know that the trace context is changing.
  current->BeforeRestoreCurrent(ContextAccess(), previous_);

  const absl::Cleanup after = [&] {
    current->AfterRestoreCurrent(ContextAccess());
  };

  // Overwrite the current trace context's contents.
  *current = std::move(previous_);

  DCHECK(!previous_.CanRecordAnnotations() &&
         !previous_.has_sync_tracer());  // NOLINT
}

WithDeadline::WithDeadline(const absl::Time new_deadline)
    : swapped_deadline_(new_deadline), is_deadline_swapped_(false) {
  Context* absl_nonnull const current_context = per_thread_context.pointer();

  // If the existing deadline is nearer than the new one, then there's nothing
  // for us to do.
  if (current_context->deadline() <= new_deadline) {
    return;
  }

  // Otherwise swap the deadline (and remember that we've done so).
  //
  // Note that there are no signal-safety concerns with the non-atomic
  // modification here because we don't expose the deadline to signal handlers.
  is_deadline_swapped_ = true;
  current_context->SwapDeadline(&swapped_deadline_);
}

WithDeadline::~WithDeadline() {
  // If we didn't change anything in the constructor then there is nothing for
  // us to do.
  if (!is_deadline_swapped_) {
    return;
  }

  // Restore the old deadline to the current context. There are no signal-safety
  // concerns here for the same reason as in the constructor.
  per_thread_context.pointer()->SwapDeadline(&swapped_deadline_);
}

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context. This probably requires refactoring
// SwapCensusHandle so that the notification logic runs outside of the critical
// section.
WithCensusHandle::WithCensusHandle(const CensusHandle& handle)
    : swapped_handle_(handle) {
  per_thread_context.pointer()->SwapCensusHandle(&swapped_handle_);
}

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context. This probably requires refactoring
// SwapCensusHandle so that the notification logic runs outside of the critical
// section.
WithCensusHandle::WithCensusHandle(CensusHandle&& handle)
    // Keep std::move() here, in preparation for refcounted handles.
    : swapped_handle_(std::move(handle)) {
  per_thread_context.pointer()->SwapCensusHandle(&swapped_handle_);
}

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context. This probably requires refactoring
// SwapCensusHandle so that the notification logic runs outside of the critical
// section.
WithCensusHandle::~WithCensusHandle() {
  per_thread_context.pointer()->SwapCensusHandle(&swapped_handle_);
}

const CensusHandle& CensusAccess::GetCurrentHandle() {
#if BASE_HAVE_TRACECONTEXT
  return CurrentContext().trace()->census_handle_;
#else
  static CensusHandle* dummy_ch = new CensusHandle;
  return *dummy_ch;
#endif
}

// TODO: this function needs to use an "unpublish while modifying"
// critical section to avoid allowing CurrentTraceContextNoAlloc to see a
// potentially inconsistent trace context.
void CensusAccess::SwapCurrentHandle(CensusHandle* const h) {
#if BASE_HAVE_TRACECONTEXT
  using std::swap;
  swap(per_thread_context.pointer()->trace()->census_handle_, *h);
#endif
}

const CensusHandle& CensusAccess::GetHandleFromContext(const Context& context) {
  return context.census_handle_ref();
}

}  // namespace base

#endif  // !BASE_CONTEXT_HAVE_DEADLINE
