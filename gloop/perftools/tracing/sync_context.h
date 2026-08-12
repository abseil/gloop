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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_SYNC_CONTEXT_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_SYNC_CONTEXT_H_

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"

// Forward class definitions for access
class TraceContext;
namespace base {
class Context;
}  // namespace base

namespace perftools::tracing::core {

// `SyncContext` manages context for causality aware execution tracing.
//
// A `SyncContext` instance can either be empty, or hold one or more
// `TraceEventListener` instances that need to receive key processing events
// such as the start and end of synchronous code executions and cross-thread
// synchronization events. `SyncContext` has the same notion of a per thread
// active 'current' instance as `base::Context` and `TraceContext`.
//
// `SyncContext` is designed to be embedded into `TraceContext`.  It provides
// notification methods to be invoked by `TraceContext` on `Swap` and `Restore`
// operations taking place on the 'current' per thread instance.
//
// Swapping the contents of a non-empty `SyncContext` instance into the current
// thread instance installs the managed listener(s) for the current thread, and
// all tracing API calls will result in the invocation of the corresponding
// event notification methods on those event listeners.
//
// The basic concept of how this works can be best explained in an example:
//
//  void Foo() {
//    TraceContext tc;
//
//    // Internally invokes `SyncContext::AddListener()`, adding a heap
//    // allocated instance of `MyCoolListener` to the embedded `SyncContext`.
//    tc.AddTraceEventListener(new MyCoolListener);
//
//    // Swaps this trace context including the embedded `SyncContext` instance
//    // into the thread's current context, installing our `MyCoolListener`.
//    // The event listener will receive the `TraceBeginSyncId()` event call
//    // indicating the start of the synchronous execution scope.
//    TraceContextSwitcher scoped(std::move(tc));
//
//    // The below scoped region will emit a `TraceBeginRegion` event at the
//    // start of the scoped region, and a `TraceEndRegion` event at the end
//    // of the scope. Both these are invoked on the installed `MyCoolListener`.
//    {
//      perftools::tracing::Region my_region("CallBarAndBaz");
//      Bar();
//      Baz();
//    }
//
//    // At the end of the scope, the original context is restored into the
//    // thread local context, which also results in the `TraceEndSyncId()`
//    // event to be invoked on the `MyCoolListener`. As the trace context is
//    // destroyed, the `ReleasEventListener` method is invoked on our listener
//    // to release any resources including itself.
//  }
//
// Child instances can be created through the `CreateThread()` function for
// context associated with code scheduled on separate thread contexts, or by
// creating a plain copy if scheduling information is not available. The latter
// is typical for 'out of thin air' executions such as streaming RPC handlers.
//
// Event listeners are automatically propagated into copies of a `SyncContext`,
// and all such copies are part of the same execution graph with any number of
// causalities between the different context. For example, a context created
// through the `CreateThread()` function has a 'Spawn' causality edge.
//
// It is important to notice that `base::Context(kThread)` will result in
// the embedded `SyncContext` of the `TraceContext` instance to be created
// through a `CreateThread()` call as the assumption is that `base::Context`
// instances created this way are intended to be scoped in a separate thread
// context such as a fiber body, or a scheduled `AnyInvocable`.
//
// Again, we can best demonstrate this with an example:
//
//  void FooInAThread() {
//    TraceContext tc;
//
//    // Creates and adds the listener, receiving a 'TraceBeginSync()` event.
//    tc.AddTraceEventListener(new MyCoolListener);
//    TraceContextSwitcher scoped(std::move(tc));
//
//    // Creates a child context, invoking `GetEventListener()` on the
//    // currently active `MyCoolListener` instance to get a child listener,
//    // and emits (invokes) the 'OnTraceSpawn` event on `MyCoolListener`.
//    base::Context context(base::Context::kThread);
//
//    // Bind context to thread body
//    auto body = [context = std::move(context)] mutable {
//      // The scoped context will include the scoping of the current
//      // `TraceContext` and the embedded `SyncContext` emitting the
//      // `OnTraceBeginRegion` and `OnTraceEndRegion` events at the
//      // start and end of the scoped fiber body.
//      base::WithContext with_context(std::move(context));
//      Foo();
//    };
//    std::thread thread(std::move(body));
//    thread.join();
//  }
//
// Obviously applications should rarely need to hand-roll their own thread logic
// and executions contexts, and instead use library code and executors who do
// this automatically. The above code is purely demonstrating the core principle
// of how child contexts can be created, propagated and scoped.
//
// For example, `thread::Fiber` will already DTRT with execution contexts,
// and we could write the above example much simpler as:
//
//  void FooInAThread() {
//    TraceContext tc;
//
//    // Creates and adds the listener, receiving a 'TraceBeginSync()` event.
//    tc.AddTraceEventListener(new MyCoolListener);
//    TraceContextSwitcher scoped(std::move(tc));
//
//    // Fiber automatically propagates execution context.
//    thread::Fiber fiber([]{ Foo(); };
//    fiber.Join();
//  }
//
// See the comments for `TraceEventListener` for an exhaustive list of events.
//
// `SyncContext` uses a pimpl pattern to minimize the footprint, and behaves
// like a "safe at any speed" value class with efficient copy and move
// operations. There are no failure modes under optimized builds: all functions
// are safe to call regardless of the internal or external state. Under DEBUG
// builds, functions may check fail if some internal and/or external
// requirements or in-variants are broken.
class ABSL_ATTRIBUTE_TRIVIAL_ABI SyncContext {
 public:
  // Access token for testing purposes
  class Access;

  // State of this context. The default state of a context is `kDefault`.
  // Possible state transitions are:
  //
  //                    Nested  -----
  //                      ▲          |
  //                      |          |
  //                      ▼          ▼
  //    --► Default --► Active  ----► Zombie
  //                      ▲          ▲
  //                      |          |
  //                      ▼          |
  //                  Suspended -----
  //
  enum class State : uint8_t {
    // A newly created instance that has never been scoped.
    kDefault,

    // An instance that is the current active (TLS) instance for some thread.
    kActive,

    // An instance that was suspended by an instance for a different trace.
    // "Different trace" here includes "swapped out by an empty instance."
    kSuspended,

    // An instance that was swapped out by an instance for the same trace.
    kNested,

    // An instance that will be deleted. The `kZombie` state is assigned
    // in the 'BeforeRestore()` call which takes place shortly before the
    // swapped out instance is forever deleted.
    kZombie,
  };

  // Releases any contained resources. Notably, if the current instance
  // contains active listeners it will release these listeners by invoking
  // `ReleaseEventListener` on each such instance.
  ~SyncContext() noexcept;

  // Creates an empty SyncContext instance.
  constexpr SyncContext() noexcept = default;

  // SyncContext is movable, copyable and assignable. Notice that
  // the 'sync_id` value is moved, but assigned a new value on copy.
  SyncContext(const SyncContext&);
  SyncContext(SyncContext&&) noexcept;
  SyncContext& operator=(const SyncContext&);
  SyncContext& operator=(SyncContext&&) noexcept;

  // Returns a copy of this `SyncContext` instance with the intention
  // of this new instance to be scoped on a separate thread context.
  // The returned instance will have a new `sync_id` value, which is used to
  // identify the execution context in 'Spawn', 'Begin' and 'End' trace events.
  SyncContext CreateThread(StringRef label = {}) const;

  // Adds a processing event listener to this instance.
  // Requires the current instance not to be the (embedded) current thread
  // local active context.
  // This function is a no-op if `listener` is null.
  void AddListener(TraceEventListener* listener);

  // Removes `listener` from this instance.
  // Requires the current instance to not be the (embedded) current thread
  // local active context and `listener` to be present in this instance.
  // This function is a no-op if `listener` is null.
  // If `listener` is not present in this instance, then this function will
  // check fail in debug builds, and release the listener in production builds.
  void RemoveListener(TraceEventListener* listener);

  // Adds a processing event listener to this instance where this instance is
  // the (embedded) current thread local active context. The `OnTraceBeginSync`
  // event will be emitted on `listener` (but not any already present listeners)
  // This function is a no-op if `listener` is null.
  // `label` identifies the code beginning or resuming the execution of the
  // active instance and is typically either the source location of the calling
  // code, or an explicit label provided in the code originating the call.
  void AddListenerToCurrent(Access, TraceSpanId span_id,
                            TraceEventListener* listener,
                            StringRef label = TraceSourceLocation::current());

  // Removes 'listener` from this instance where this instance is the (embedded)
  // current thread local active context. The `OnTraceEndSync` event will be
  // emitted on `listener` (but not on any already present listeners).
  // Requires `listener` to have been previously added to this instance.
  // This function is a no-op if `listener` is null.
  // If `listener` is not present in this instance, then this function will
  // check fail in debug builds, and release the listener in production builds.
  // Any call to `ReleaseEventListener()` on the provided `listener` or any
  // embedded (multiplexed) listeners inside `listeners` is guaranteed to
  // occur _after_ the current active listener for the instance has been
  // updated. I.e.: The listener is no longer invoked from traced calls from
  // non trivial code executed by any listener specific release logic.
  void RemoveListenerFromCurrent(Access, TraceEventListener* listener);

  // Returns `true` if the provided listener is present in this instance.
  // This function is mostly intended for global trace event listeners such
  // as the Dapper PE processing event listener which route the processing
  // events to the thread-local tracer instance. Such a listener is typically
  // only added to the top-most trace span, and inherited by nested local spans.
  // Always returns false if `listener` is nullptr.
  bool ContainsListener(TraceEventListener* listener) const;

  // Returns the 'synchronous execution id' for this context, or `kNosyncId`
  // if this instance is empty. A unique `sync_id` value is assigned when
  // an instance goes from empty to non empty as the result of a listener
  // being added to the instance. This unique `sync_id` value identifies
  // the (planned or active) code execution.
  SyncId sync_id() const;

  // Returns true if this instance is part of a causality trace graph and
  // contains event listeners requiring processing event notifications.
  bool has_listeners() const { return impl_ != nullptr; }

  // The listener managed by this instance. This listener can be a multiplexing
  // listener created through `MultiplexTraceEventListener`, holding multiple
  // listener instances all requiring processing event notifications.
  // The return value is null if this instance does not hold any listeners.
  TraceEventListener* listener() const;

  // Returns true if `this` and `other` are part of the same execution graph.
  //
  // The notion of 'context belonging to the same execution graph' is important
  // for both causality and isolation of traces. An execution context is started
  // at some root execution, typically an RPC handler of a server, and all
  // subsequent executions are scoped by this context or copies created from it.
  // All such copies are part of the same overall execution context. Executors
  // scope these contexts around scheduled code, guaranteeing that events are
  // only delivered to contexts belonging to that execution graph. As contexts
  // are scoped (swapped and restored), the tracing API verifies if the 'old'
  // and 'new' context belong to the same execution graph, in which case the
  // scoped context is considered to be 'inlined' and have no effect on the
  // scope of the synchronous code span. If the 'old' and 'new' context do not
  // belong to the same execution graph, then the 'old' context is suspended
  // (emitting `OnTraceSuspend` events) before the 'new' context is started or
  // resumed.
  bool same_trace(const SyncContext& other) const;

  // swaps lhs and rhs. Does neither verify nor change the TLS active sync id.
  void swap(SyncContext& lhs, SyncContext& rhs);

  // `BeforeSwapCurrent` and `BeforeRestoreCurrent` manage the 'per thread'
  // tracing state used by the tracing event listener framework, and must be
  // invoked directly before the current thread's context is changed from this
  // instance to the instance referenced in `to`. For example, when restoring
  // the current thread's context from an active context to an empty context
  // this function may emit a `OnTraceEndSync` or `OnTraceSuspendSync` event
  // and reset 'active_sync_id` and `active_span_id` for the current thread.
  void BeforeSwapCurrent(Access, const SyncContext& to);
  void BeforeRestoreCurrent(Access, const SyncContext& to);

  // `AfterSwapCurrent` and `AfterRestoreCurrent` manage the 'per thread'
  // tracing state used by the tracing event listener framework, and must be
  // invoked after this instance has been installed as the current thread's
  // context. For example, swapping in a scheduled context without a current
  // active context sets the `active_sync_id` and `active_span_id` for the
  // current thread and emits an `OnTraceBeginSync` event.
  void AfterSwapCurrent(Access, TraceSpanId span_id,
                        StringRef label = TraceSourceLocation::current());
  void AfterRestoreCurrent(Access, TraceSpanId span_id,
                           StringRef label = TraceSourceLocation::current());

  static SyncContext CreateForTesting(Access, TraceEventListener* listener,
                                      SyncId sync_id, SyncId active_sync_id);

 private:
  // SyncContext follows a pimpl pattern for two main reasons:
  // - the fast path is that no execution tracers are installed. Using a pimpl
  //   pattern minimizes the footprint of the SyncContext class.
  // - the pimpl implementation allows for very efficient zero initialization,
  //   and move, Swap and Restore operations.
  class Impl;

  explicit constexpr SyncContext(Impl* context);

  Impl* impl_ = nullptr;
};

// ------------------------ SharedContext --------------------------------

// ------------------------ Context --------------------------------

class SyncContext::Impl {
 public:
  ~Impl();

  // Impl can not be moved, copied or assigned to.
  Impl() = delete;
  Impl(const Impl& rhs) = delete;
  Impl& operator=(const Impl& rhs) = delete;

  // Creates a new instance with the specified listener.
  // `listener` must not be null.
  static Impl* New(TraceEventListener* listener);

  // Implementation used for SyncContext copy operations.
  Impl* Copy() const;

  // Implementation of SyncContext::CreateThread.
  Impl* CreateThread(StringRef label) const;

  // Implementation of SyncContext::AddListenerToCurrent()
  void AddListenerToCurrent(TraceEventListener* listener, StringRef label);

  // Implementation of SyncContext::AddListener()
  void AddListener(TraceEventListener* listener);

  // Implementation of SyncContext::RemoveListener()
  Impl* RemoveListener(TraceEventListener* listener);

  // Implementation of SyncContext::RemoveListenerFromCurrent()
  Impl* RemoveListenerFromCurrent(TraceEventListener* listener);

  // Implementation of SyncContext::ContainsListener()
  bool ContainsListener(TraceEventListener* listener) const;

  // Implementation of SyncContext accessors
  SyncId sync_id() const;
  TraceEventListener* listener() const;

  // Implementation of SyncContext::same_trace()
  bool same_trace(const Impl* other) const;

  // Implementation of BeforeSwapCurrent and BeforeRestoreCurrent.
  void BeforeSwapCurrent(const Impl* to);
  void BeforeRestoreCurrent(const Impl* to);

  // Implementation of AfterSwapCurrent and AfterRestoreCurrent.
  // Returns true on success, false in case of an error or an invalid state,
  // in which case the caller must delete and clear the implementation for
  // the current context.
  bool AfterSwapCurrent(StringRef label);
  bool AfterRestoreCurrent(StringRef label);

  // Returns the active `sync_id` for this context which is set if this thread
  // is either the active scoped context for the current thread, or the context
  // was the active context but is suspended, for example by a `BreakTraceSpan`
  // Otherwise returns `kNoSyncId`
  //
  // Typically, `active_sync_id` is equal to `sync_id` when this context is
  // either active or suspended. However, when scoped contexts of the same
  // execution graph are nested, the inner context will adopt the active
  // `sync_id` value of the outer scope. The latter can happen in certain
  // situations where a function or `AnyInvocable` was scheduled to run on a
  // different thread, but was eventually executed (and scoped) inlined with
  // the currently executing (and traced) code.
  SyncId active_sync_id() const;

  static Impl* CreateForTesting(TraceEventListener* listener, SyncId sync_id,
                                SyncId active_sync_id);

 private:
  class Shared;
  using SharedPtr = std::shared_ptr<Shared>;

  enum SwapOrRestore { kSwap, kRestore };

  Impl(SharedPtr shared, TraceEventListener* listener, SyncId sync_id) noexcept;

  template <SwapOrRestore swap_or_restore>
  void BeforeSwap(const Impl* to);

  // Implements 'AfterSwap/AfterRestore' logic. Returns true on success, false
  // in case of error in which case the caller should delete this instance.
  template <SwapOrRestore swap_or_restore>
  bool AfterSwap(StringRef label);

  // Verifies that the current instance is the active instance for the current
  // thread, and returns the current thread's listener. Returns a nullptr on
  // error and check fails in debug builds if either of those are not set.
  TraceEventListener* ActiveListener() const;

  // Returns the listener for this  sync context instance which may or may not
  // be the currently active listener for the active thread. This function could
  // return nullptr if any of the invariants of SyncContext are badly broken.
  TraceEventListener* ThisListener() const;

  const SyncId sync_id_;
  State state_ = State::kDefault;
  SyncId active_sync_id_ = kNoSyncId;
  TraceEventListener* listener_ = nullptr;
  SharedPtr shared_;
};

std::ostream& operator<<(std::ostream&, SyncContext::State);

inline SyncId SyncContext::Impl::sync_id() const { return sync_id_; }

inline SyncId SyncContext::Impl::active_sync_id() const {
  return active_sync_id_;
}

inline TraceEventListener* SyncContext::Impl::listener() const {
  return listener_;
}

inline bool SyncContext::Impl::same_trace(const Impl* other) const {
  return other != nullptr && shared_ == other->shared_;
}

// ------------------------ SyncContext --------------------------------
class SyncContext::Access {
 private:
  constexpr Access() noexcept;
  friend class SyncContextTestPeer;
  friend class ::TraceContext;
  friend class ::base::Context;
};
constexpr SyncContext::Access::Access() noexcept = default;

inline SyncContext::~SyncContext() noexcept {
  if (impl_ != nullptr) {
    delete impl_;
  }
}

inline SyncContext::SyncContext(SyncContext&& rhs) noexcept : impl_(rhs.impl_) {
  rhs.impl_ = nullptr;
}

inline SyncContext::SyncContext(const SyncContext& rhs)
    : impl_(rhs.impl_ ? rhs.impl_->Copy() : nullptr) {}

inline SyncContext& SyncContext::operator=(SyncContext&& rhs) noexcept {
  if (this == &rhs) return *this;
  if (impl_ != nullptr) {
    delete impl_;
  }
  impl_ = rhs.impl_;
  rhs.impl_ = nullptr;
  return *this;
}

inline SyncContext& SyncContext::operator=(const SyncContext& rhs) {
  if (this == &rhs) return *this;
  if (impl_) delete impl_;
  impl_ = rhs.impl_ ? rhs.impl_->Copy() : nullptr;
  return *this;
}

inline constexpr SyncContext::SyncContext(Impl* context) : impl_(context) {}

inline TraceEventListener* SyncContext::listener() const {
  return impl_ ? impl_->listener() : nullptr;
}

inline SyncId SyncContext::sync_id() const {
  return impl_ ? impl_->sync_id() : kNoSyncId;
}

inline bool SyncContext::ContainsListener(TraceEventListener* listener) const {
  return impl_ != nullptr && impl_->ContainsListener(listener);
}

inline bool SyncContext::same_trace(const SyncContext& other) const {
  return impl_ && impl_->same_trace(other.impl_);
}

inline SyncContext SyncContext::CreateThread(StringRef label) const {
  return SyncContext(impl_ ? impl_->CreateThread(label) : nullptr);
}

inline void SyncContext::BeforeSwapCurrent(Access, const SyncContext& to) {
  if (impl_ != nullptr) {
    impl_->BeforeSwapCurrent(to.impl_);
  }
}

inline void SyncContext::AfterSwapCurrent(Access, TraceSpanId,
                                          StringRef label) {
  if (impl_ != nullptr) {
    if (ABSL_PREDICT_FALSE(!impl_->AfterSwapCurrent(label))) {
      delete impl_;
      impl_ = nullptr;
    }
  }
}

inline void SyncContext::BeforeRestoreCurrent(Access, const SyncContext& to) {
  if (impl_ != nullptr) {
    impl_->BeforeRestoreCurrent(to.impl_);
  }
}

inline void SyncContext::AfterRestoreCurrent(Access, TraceSpanId,
                                             StringRef label) {
  if (impl_ != nullptr) {
    if (ABSL_PREDICT_FALSE(!impl_->AfterRestoreCurrent(label))) {
      delete impl_;
      impl_ = nullptr;
    }
  }
}

inline void SyncContext::swap(SyncContext& lhs, SyncContext& rhs) {
  using std::swap;
  swap(lhs.impl_, rhs.impl_);
}

inline SyncContext SyncContext::CreateForTesting(Access,
                                                 TraceEventListener* listener,
                                                 SyncId sync_id,
                                                 SyncId active_sync_id) {
  return SyncContext(Impl::CreateForTesting(listener, sync_id, active_sync_id));
}

}  // namespace perftools::tracing::core

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_SYNC_CONTEXT_H_
