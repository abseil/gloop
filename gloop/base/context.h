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

// A Context is a container for request-specific information like security
// credentials, tracing identifiers, etc. Each thread has a currently active
// Context corresponding to the received request on behalf of which this thread
// is running. The Context defaults to suitable empty values for each component.
//
// See <link> for a broad overview.
//
#ifndef THIRD_PARTY_GLOOP_BASE_CONTEXT_H_
#define THIRD_PARTY_GLOOP_BASE_CONTEXT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/time/time.h"
#include "gloop/base/censushandle.h"
#include "gloop/base/context_access.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace security {
namespace context {
class SecurityContext;
}  // namespace context
}  // namespace security
namespace thread {
class Fiber;
}  // namespace thread

// Indicates that the base::Context deadline API is supported in this build.
#if defined(BASE_CONTEXT_HAVE_DEADLINE)
#error BASE_CONTEXT_HAVE_DEADLINE cannot be directly set
#elif (defined(__APPLE__) || defined(_MSC_VER) || defined(__linux__) || \
       defined(__Fuchsia__) || defined(__wasm__) ||                     \
       defined(__EMSCRIPTEN__)) &&                                      \
    !defined(GOOGLE_UNSUPPORTED_OS_VR_WALLY)
#define BASE_CONTEXT_HAVE_DEADLINE 1
#endif

#define BASE_CONTEXT_HAVE_SECURITYCONTEXT 0

namespace base {

// Used to provide const ref access to the context's CensusHandle.
class CensusAccess;

namespace internal {
class MutableCurrentContext;
}  // namespace internal

namespace subtle {
class WithLifetimeBoundContext;
}  // namespace subtle

// This type is thread-compatible.
class ABSL_ATTRIBUTE_TRIVIAL_ABI Context {
 public:
  struct ThreadInitType {};
  inline constexpr static ThreadInitType kThread{};

  struct NoContextInitType {};
  inline constexpr static NoContextInitType kNoContextInit{};

  // Construct a context in the default (background, empty) state.
  Context() noexcept = default;

  // Constructs a Context with state inherited from the creating thread. Call
  // like so: base::Context(base::Context::kThread).
  //
  // `thread_name` can be specified and serves as an identifying name for the
  // (scheduled) execution of the code scoped by this context for the purpose of
  // causality tracing. `thread_name` defaults to the current source location.
  // For example: `base::Context(base::Context::kThread, "QueueWorker")`
  explicit Context(ThreadInitType,
                   perftools::tracing::StringRef thread_name =
                       perftools::tracing::TraceSourceLocation::current());

  Context(const Context& c);
  Context(Context&& c) noexcept;
  Context& operator=(const Context& c);
  Context& operator=(Context&& c) noexcept;

  // Copy constructs a Context with state copied from the source context
  // using `thread_name` as an identifying name for the (scheduled) execution
  // of the code scoped by this context for the purpose of causality tracing.
  Context(const Context&, perftools::tracing::StringRef thread_name);

  ~Context();

  // Swaps the contents of two `Context` instances.
  friend void swap(Context&, Context&) noexcept;

  // Returns a reference to the tracing information contained within this
  // context.
  //
  // The reference remains valid only until this object is next modified.
  const TraceContext& trace_context() const { return tc_; }

  // Set the trace information returned by the `trace_context` accessor.
  void set_trace_context(TraceContext tc) { tc_ = std::move(tc); }

  // Returns the CensusHandle for this context.
  //
  // Initialized to a default handle when constructed via Context::kDefault.
  CensusHandle census_handle() const { return tc_.census_handle(); }

  // Sets the Census handle.
  //
  // Note: don't use this to modify the handle of CurrentContext (you can't
  // because CurrentContext returns a const reference). Instead use
  // stats_census::Tagger or WithCensusHandle.
  void set_census_handle(const CensusHandle& handle) {
    tc_.set_census_handle(handle);
  }

  void set_census_handle(CensusHandle&& handle) {
    tc_.set_census_handle(std::move(handle));
  }

  // Returns a pointer to the "status string" associated with the context, or
  // nullptr if there is no status set.
  const char* thread_status() const;

  // This function sets a "status string" associated with this context.  This is
  // output along with all stack traces. It's most useful for keeping track of
  // what a thread was working on when it died. This function does not take
  // ownership of the argument, and the const char* must survive until it is no
  // longer the thread_status of any context, including all Callbacks created
  // while it was set. Setting this to nullptr (the default) means that no
  // status string will be printed.
  void set_thread_status(const char* thread_status);

  // Returns a deadline after which continued execution may no longer be of
  // interest.  The Context API does not specify any behavior(s) to be taken
  // relative to this deadline; it is only an advisory property that individual
  // libraries may define stronger specifications against.
  //
  // Initialized to InfiniteFuture() when constructed via Context::kDefault.
  absl::Time deadline() const;

  // Sets the value returned by the `deadline` accessor.
  void set_deadline(const absl::Time d) { deadline_ = d; }

  struct [[deprecated("Use the default constructor.")]] DefaultInitType {};

  [[deprecated("Use the default constructor.")]]
  inline constexpr static DefaultInitType kDefault{};

  // An old name for the default constructor. Do not use in new code.
  ABSL_DEPRECATE_AND_INLINE()
  explicit constexpr Context(DefaultInitType) noexcept : Context() {}

  // Like the trace_context accessor, but these return pointers. The pointers
  // are never null, and the caller doesn't take ownership.
  ABSL_DEPRECATED("Prefer to provide trace contexts by value")
  TraceContext* absl_nonnull trace() { return &tc_; }
  ABSL_DEPRECATE_AND_INLINE()
  const TraceContext* absl_nonnull trace() const { return &trace_context(); }

 private:
  // Swaps the supplied deadline with the one in this context, leaving
  // the remainder of the context intact.
  void SwapDeadline(absl::Time* deadline);

  // Swaps the supplied Census handle with the one in this context, leaving
  // the remainder of the context intact. This should only be called from
  // WithCensusHandle.
  void SwapCensusHandle(CensusHandle* handle);

  // Provides const reference access to the CensusHandle. Note that the
  // lifetime of the returned reference is tied to the lifetime of the Context
  // object, and should not outlive it.
  //
  // Do not use this API unless you first talk with <internal team>.
  const CensusHandle& census_handle_ref() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tc_.census_handle_;
  }

  TraceContext tc_{};

  absl::Time deadline_ = absl::InfiniteFuture();
  const char* thread_status_ = nullptr;

  friend class ContextBuilder;
  friend class internal::MutableCurrentContext;
  friend const Context& BackgroundContext();
  // For propagating legacy FiberOption deadlines.
  friend class ::thread::Fiber;
  // For SwapDeadline.
  friend class WithDeadline;
  // For SwapCensusHandle.
  friend class WithCensusHandle;
  // For accessing a const reference to the CensusHandle.
  friend class CensusAccess;
  // For accessing census_handle_ref().
  friend class subtle::WithLifetimeBoundContext;
  // For hiding the SharedContext manipulation details.
  friend class SharedContextAccess;
  // For accessing privacy context fields.
  friend class PrivacyContextAccess;
};

class ContextBuilder {
 public:
  // By default, use the fields from the supplied context. Overrides
  // are specified using set* and adopt* methods.  Applications should
  // prefer to use the default constructor below rather than passing in
  // `BackgroundContext()` or `Context(Context::kDefault)`.
  // See the comments on the `ThreadInitType` constructor on when to use
  // `CurrentContext()` or `Context::kThread` as a constructor argument.
  explicit ContextBuilder(Context base_context);

  // Creates a ContextBuilder based on the background context.
  ContextBuilder();

  // Deprecated. Use the default constructor instead.
  [[deprecated("Use the default constructor.")]]
  explicit ContextBuilder(Context::DefaultInitType)
      : ContextBuilder() {}

  explicit ContextBuilder(Context::NoContextInitType) : ContextBuilder() {}

  // Creates a ContextBuilder based on the current thread's active context.
  // This constructor should be used to construct a `Context` intended to be
  // scoped on a different thread base on the current context.
  //
  // For example:
  //   base::ContextBuilder builder(base::Context::kThread);
  //   builder.set_deadline(absl::InfiniteFuture());
  //   thread::Detach(TreeOptions().set_context(builder.BuildValue())), [this] {
  //     ProcessRequest();
  //   };
  //
  // For cases where the new context gets inlined, applications should prefer to
  // use `base::CurrentContext()`: `Context::kThread` implies the expectation
  // that the resulting context will most likely run on a different thread,
  // whereas the copy constructor implies the caller wants a value copy without
  // any scheduling intent. The resulting context is the same, but it provides
  // extra intent for tracing purposes such as Dapper / Causality Aware traces.
  //
  // For example:
  //   base::ContextBuilder builder(base::CurrentContext());
  //   builder.set_security_context(RequestSpecificSecurity());
  //   base::WithContext with_security_context(builder.BuildValue());
  //
  explicit ContextBuilder(
      Context::ThreadInitType,
      perftools::tracing::StringRef label =
          perftools::tracing::TraceSourceLocation::current());

  ~ContextBuilder() = default;

  // Uses the supplied TraceContext, overriding that of the base context.
  ContextBuilder& set_trace_context(TraceContext tc);

  // Uses the supplied deadline, overriding that of the base context.
  ContextBuilder& set_deadline(absl::Time deadline);

  // Sets the census handle for the context.
  ContextBuilder& set_census_handle(CensusHandle handle);

  // Sets a "status string" associated with this context. This is output along
  // with all stack traces and most useful for keeping track of what a thread
  // was working on when it died. This function does not take ownership of the
  // argument, and the const char* must outlive any context it is referenced in
  // including any callbacks created while it was set.
  ContextBuilder& set_thread_status(const char* absl_nullable thread_status);

  // Returns the new Context value. May only be called once. This method
  // invalidates the internal state of this instance. Subsequent calls to
  // either `BuildValue()` or `Build()` may fail under debug builds, and
  // return an unspecified context value under release builds.
  Context BuildValue();

  // Returns the heap allocated new Context value. This function is logically
  // equivalent to `return new Context(BuildValue())`. Applications should
  // prefer to use the `BuildValue() method instead. Caller takes ownership of
  // the returned handle. May only be called once. This method invalidates the
  // internal state if this instance. Subsequent calls to either `BuildValue()`
  // or `Build()` may fail under debug builds, and return an unspecified context
  // value under release builds.
  ABSL_DEPRECATE_AND_INLINE()
  Context* Build() { return new Context(BuildValue()); }

 private:
  Context context_;

  // We are not copyable.
  ContextBuilder(const ContextBuilder&) = delete;
  ContextBuilder& operator=(const ContextBuilder&) = delete;
};

// Return a handle to the ambient background context. This context is
// initialized with default values and identical to a context created using
// `Context::kDefault`. This context represents the intrinsic state of the
// application at startup time, independent of any incoming request state.
const Context& BackgroundContext();

// Identical to `BackgroundContext()`. This function can be used to more clearly
// self document that the context holds a `Context::kDefault` initialized value.
const Context& DefaultContext();

// Return a handle to the current thread's context. The reference may be
// invalidated by actions that change the current context.
const Context& CurrentContext();

// Returns a pointer to the "status string" associated with the current thread,
// or nullptr if there is no status set. This function is safe to call from a
// signal handler.
const char* CurrentThreadStatus();

// Sets a "status string" associated with the current context. Use of this
// method is discouraged; use the scoped base::WithThreadStatus instead.
//
// The thread status is output along with all stack traces. It's mostly useful
// for keeping track of what a thread was working on when it died. This function
// does not take ownership of the argument, and the const char* must survive
// until it is no longer the thread_status of any context, including all
// Callbacks created while it was set. Setting this to nullptr (the default)
// means that no status string will be printed.
void SetCurrentThreadStatus(const char* thread_status);

// Return the TraceContext of the current thread, or nullptr if one cannot be
// found. This function is safe to call inside signal handlers.
const TraceContext* CurrentTraceContextNoAlloc();

// Scoped object that sets the current thread's context to switch_to until
// the object is destroyed.
//
// NEVER allocate in one thread and deallocate in another.  A simple way to
// guarantee this is to only allocate directly on the stack.
//
// Example 1: temporarily switch to a particular request context:
//    {  WithContext c(request_context_handle); ... }
//
// Example 2: temporarily switch to the background context
//    {  WithContext c(BackgroundContext()); ... }
//
// Example 3: use a new context
//    { WithContext c(ContextBuilder(...).BuildValue()); ... }
//
// The constructor accepts a 2nd argument `label` that helps identify the
// code executing under the scoped context for tracing purposes such as
// Dapper traces / causality tracing. `label` will by default capture the
// current source location, but applications can provide explicit values.
//
// For example:
//
//    WithContext with(my_context, "MergeAllFrobbers");
//    ...
//
// See also WithSecurityContext, LocalTraceSpan and other specializations that
// may be easier to use and better-performing if they fit your needs:
//   http://<path>
//   http://<path>
//
class WithContext {
 public:
  explicit WithContext(const Context& switch_to,
                       perftools::tracing::StringRef label =
                           perftools::tracing::TraceSourceLocation::current());
  explicit WithContext(Context&& switch_to,
                       perftools::tracing::StringRef label =
                           perftools::tracing::TraceSourceLocation::current());
  ~WithContext();

 private:
  // `current_`  will hold the new heap allocated thread local `Context`
  // pointer that we copy from the input and install as the new thread local.
  // We verify at destruction that the current thread local still exactly
  // matches `current_`, enforcing that the `WithContext` instance is destroyed
  // on the same thread it was created on, and properly scoped / nested.
  Context* const current_ = nullptr;

  // `previous_` will hold the previous heap allocated thread local `Context`
  // pointer as it was before we performed the internal swap, making this
  // instance 'own' that instance until the destructor swaps it back into
  // TLS, deleting the `Context` instance created and scoped at construction.
  Context* const previous_ = nullptr;

  WithContext(const WithContext&) = delete;
  WithContext& operator=(const WithContext&) = delete;
};

// Scoped object for enforcing the given deadline.
// If the new deadline is less than the current Context's deadline, a new
// Context is set with the reduced deadline, until the object is destroyed.
// Otherwise, this does nothing.
//
// NEVER allocate in one thread and deallocate in another.  A simple way to
// guarantee this is to only allocate directly on the stack.
//
// Example:
//   { WithDeadline deadline(Now() + Seconds(10)); ... }
//
// Recent benchmark data:
//
// Run on lpac6 (32 X 2600 MHz CPUs); 2016-10-27T15:03:28.246585047-07:00
// CPU: Intel Sandybridge with HyperThreading (16 cores) dL1:32KB dL2:256KB
// dL3:20MB
// Benchmark               Time(ns)    CPU(ns) Iterations
// ------------------------------------------------------
// BM_WithDeadline                9          9   75167908
//
class WithDeadline {
 public:
  explicit WithDeadline(absl::Time new_deadline);
  ~WithDeadline();

 private:
  absl::Time swapped_deadline_;
  bool is_deadline_swapped_;

  WithDeadline(const WithDeadline&) = delete;
  WithDeadline& operator=(const WithDeadline&) = delete;
};

// Scoped object that sets the current thread's trace context to `switch_to`.
//
// `WithTraceContext` must always be scoped in a synchronous execution context.
// It is an error to create a `WithTraceContext` instance in one thread, and
// delete it in another thread. Heap allocating a `WithTraceContext` is usually
// an error, and should preferably only be done by lower level library code and
// special support routines such as co-routines.
//
// Example: temporarily switch to a particular trace context:
//    WithTraceContext with(std::move(trace_context));
//    ...
//
// The constructor accepts a 2nd argument `label` that helps identify the
// code executing under the scoped context for tracing purposes such as
// Dapper traces / causality tracing. `label` will by default capture the
// current source location, but applications can provide explicit values.
//
// For example:
//
//    WithTraceContext with(my_trace_context, "DocumentScoringThread");
//    ...
//
class WithTraceContext {
 public:
  explicit WithTraceContext(
      const TraceContext& switch_to,
      perftools::tracing::StringRef label =
          perftools::tracing::TraceSourceLocation::current());
  explicit WithTraceContext(
      TraceContext&& switch_to,
      perftools::tracing::StringRef label =
          perftools::tracing::TraceSourceLocation::current());
  ~WithTraceContext();

  // WithTraceContext can not be copied, moved or assigned to.
  WithTraceContext(const WithTraceContext&) = delete;
  WithTraceContext& operator=(const WithTraceContext&) = delete;

 private:
  // `previous_` captures the current thread local `TraceContext` value in the
  // constructor directly before we swap it with the `switch_to` instance.
  // We explicitly manage its life-cycle and do not explicitly destroy it as
  // we always leave it in a 'moved from' state, and `TraceContext` explicitly
  // guarantees a 'moved from' instance does not hold non-trivial data.
  // http://eel.is/c++draft/basic.life#5
  union {
    TraceContext previous_;
  };
};

// Scoped object that sets the current thread's handle until the object is
// destroyed.
//
// NEVER allocate in one thread and deallocate in another.  A simple way to
// guarantee this is to only allocate directly on the stack.
//
// To set Census tags, consider using stats_census::Tagger instead of this.
//
// Example: temporarily switch to a particular handle:
//    {  WithCensusHandle wch(some_stored_handle); ... }
//
class WithCensusHandle {
 public:
  explicit WithCensusHandle(const CensusHandle& handle);
  explicit WithCensusHandle(CensusHandle&& handle);
  ~WithCensusHandle();

 private:
  CensusHandle swapped_handle_;

  WithCensusHandle(const WithCensusHandle&) = delete;
  WithCensusHandle& operator=(const WithCensusHandle&) = delete;
};

// Scoped object that sets the current thread's status until the object is
// destroyed. The thread status is displayed in /threadz.
//
// NEVER allocate in one thread and deallocate in another.  A simple way to
// guarantee this is to only allocate directly on the stack.
//
// Example:
//    {  WithThreadStatus wts("processing"); ... }
//
class WithThreadStatus {
 public:
  explicit WithThreadStatus(const char* status);
  ~WithThreadStatus();

 private:
  const char* const swapped_status_;

  WithThreadStatus(const WithThreadStatus&) = delete;
  WithThreadStatus& operator=(const WithThreadStatus&) = delete;
};

// Low-level APIs for changing the current context.
//
// NOTE: Callers are strongly advised to use WithContext, LocalTraceSpan, or
// other scoped APIs instead. Misuse of the APIs below could lead to
// memory leaks and lost credentials.

// Completely swaps the current context with the supplied one.
[[deprecated("Use ONLY the scoping classes as documented above")]]
void SwapCurrentContext(Context* c);

// Sets the current context from the specified one. It is assumed that c will
// be deleted immediately, and so may be modified arbitrarily.
[[deprecated("Use ONLY the scoping classes as documented above")]]
void RestoreCurrentContext(Context* c);

// The below functions and declarations are for internal use by public `Context`
// APIs only. Using any of these outside of //gloop/base and `Context` APIs is
// explicitly verboten and will lead to subtle errors and undefined behaviors.
namespace internal {

// Directly swaps the 'per thread' pointer to reference the provided context.
// The caller must guarantee the lifetime of `context` until the context is
// swapped or restored to the previous pointer by a subsequent call to either
// `SwapContext()` or `RestoreContext()`. `label` is an identifying label that
// is captured at the public API, i.e.: `WithContext` for tracing purposes.
// Returns the previous context.
Context* absl_nonnull SwapContext(
    ContextAccess, Context* absl_nonnull context ABSL_ATTRIBUTE_LIFETIME_BOUND,
    perftools::tracing::StringRef label);

// Directly restores the 'per thread' pointer to reference the provided context.
// This call is similar to `SwapContext` except that the explicit intent is that
// the returned context will be destroyed by the caller. This is relevant for
// logic such as causality tracing as it signals that no more code will execute
// in the presence of this context. I.e.: execution of code with the current
// context has 'ended' where a 'SwapContext` implies execution of code with
// the current context is suspended, and the returned tracecontext can be
// re-used to scope (resume) other code executions.
Context* absl_nonnull RestoreContext(
    ContextAccess, Context* absl_nonnull context ABSL_ATTRIBUTE_LIFETIME_BOUND);

extern absl::NoDestructor<Context> background_context;

}  // namespace internal

inline const Context& BackgroundContext() {
  return *internal::background_context;
}

inline const Context& DefaultContext() { return *internal::background_context; }

inline ContextBuilder::ContextBuilder(Context context)
    : context_(std::move(context)) {}

inline ContextBuilder::ContextBuilder() : context_() {}

inline ContextBuilder::ContextBuilder(Context::ThreadInitType,
                                      perftools::tracing::StringRef label)
    : context_(base::Context::kThread, label) {}

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_CONTEXT_H_
