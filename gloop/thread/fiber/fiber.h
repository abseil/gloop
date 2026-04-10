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

// A Fiber is a light-weight thread.  It supports cancellations, deadlines,
// priorities, etc.  A typical use is that a Fiber is created when an RPC
// arrives at a server, and the RPC is considered finished when the fiber stops
// running.
//
// Fibers are arranged in a tree.  E.g., when an RPC handler needs to
// run several computations (or sub-RPCs) in parallel, it should
// create a set of child fibers and run the sub-computations in those
// child fibers. It must Join() them before it finishes.
//
//   void ServerHandler(...) {
//     thread::Fiber worker1([] { ... });  // Lambdas work great.
//     thread::Fiber worker2(BindFront(SubComputation, ...));
//     worker1.Join();
//     worker2.Join();
//   }
//
// Note that ServerHandler() itself is running within some fiber and the two
// fibers (worker1 and worker2) are created as children of that fiber.
//
// Methods on the same fiber can be safely called concurrently from multiple
// threads (any exceptions will be documented on a per-method basis).
//
// Cancellations
// -------------
// A fiber can be cancelled.  The cancellation is automatically
// propagated to all descendants of the fiber.  I.e., sub-computations
// are also cancelled.  This cancellation should be considered a
// request to the code to stop quickly.  Nothing is killed
// automatically; it is up to the cancelled code to detect the
// cancellation and stop doing what it was doing.
//
// Example:
//    // Start a child fiber and cancel it after a second.
//    void Parent(...) {
//      thread::Fiber child(absl::bind_front(Child, ...));
//      absl::SleepFor(absl::Seconds(1));
//      child.Cancel();
//      child.Join();  // Wait for "child" to observe cancellation and complete.
//    }
//
//    // The child code that loops doing stuff until it is cancelled
//    void Child(...) {
//      while (!thread::Cancelled()) {
//        DoSomeWork();
//      }
//    }
//
// A child may want to detect cancellation while reading from some channels.  To
// support this, fibers provide a Case (see thread::OnCancel) that can be
// included in a Select statement (in addition to the ability to pass Reader or
// Writer objects to Select).  The Case becomes ready when cancellation is
// requested.
//
// Example: read and process data from a channel until cancelled.
//
//   void Process(thread::Reader<Value>* r, ...) {
//     Value v;
//     bool ok;
//     thread::CaseArray cases =
//         {r->OnRead(&v, &ok), thread::OnCancel()};
//
//     while (thread::Select(cases) == 0 && ok) {
//       Process(v);
//     }
//   }
//
// Note that since cancellation is automatically propagated to the entire
// sub-tree of fibers, a fiber that is just waiting for child fibers to finish
// does not need to have any special cancellation handling.  It is up to the
// code running in the child fibers to stop in response to the cancellation,
// which will automatically allow the parent to finish. This feature should
// allow most code that uses fibers to automatically be cancellation aware
// without requiring any custom cancellation handling code.
//
// Parallelism
// -----------
// The parallelism of the fiber's tree determines the parallelism of the
// children. For example, if ServerHandler() above runs within the scope of an
// inbound Stubby4 RPC, the above fibers will typically run in parallel (exactly
// how many run at once depends on Stubby's configuration). But if
// ServerHandler() is run from the scope of main, the above fibers may or may
// not run in parallel since main has an unspecified parallelism.
//
// You can use thread::NewTree() to create a fiber tree with parallelism
// specified by thread::TreeOptions().
//
// TODO:
// . request flow tracing
// . properties of fibers

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_H_

#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/context.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/fiber-internal.h"  // IWYU pragma: export
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.pb.h"
#include "gloop/thread/fiber/per-domain-counters.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gloop/util/gtl/intrusive_list.h"

namespace thread {

namespace subtle {
void Detach(Fiber&, void (&)(Fiber*));
}

// The basic unit of work.
using Invocable = internal::InvocableImpl;
using Scheduler = base::scheduling::Scheduler;

// A private scope to ensure that only Fiber internals can access the intrusive
// cancellation list.
class CancellationList {
  struct Tag {};
  friend class Fiber;
};

class Fiber : private gtl::intrusive_link<Fiber, CancellationList::Tag> {
 public:
  // A tag type that disambiguates the root fiber constructor.
  struct RootFiber final {};

  // Create a new fiber tree. This creates a new independent fiber with no
  // parent (referred to as a 'root' fiber). Trees are commonly used to
  // represent top-level concurrency, such as a request or background worker.
  // Child fibers may be created in turn to represent possible sub-concurrency.
  //
  // The new tree is guaranteed to be independent with respect to all fibers
  // belonging to other trees. E.g. No Join() or Cancel() relationships will be
  // shared.
  //
  // Unless TreeOptions::context() is set, the new root fiber, and any children,
  // will execute under base::BackgroundContext().
  //
  // REQUIRES: *this must be Join()-ed before it may be destroyed.
  template <int&..., std::convertible_to<Invocable> F>
    requires std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
  explicit Fiber(RootFiber, TreeOptions tree_options, F&& f)
      : Fiber(Unstarted{}, std::forward<F>(f), std::move(tree_options)) {
    Start();
  }

  // Construct a fiber that is the child of the currently running fiber.  This
  // represents a single thread of control which will run the specified function
  // exactly once.
  //
  // If this fiber was created from a thread, then FiberOptions() is used
  // implicitly.  Otherwise, the parent's options() are inherited.
  //
  // The new fiber will execute within (a copy of) the active base::Context at
  // point of creation.  This may include a deadline.
  //
  // The fiber body 'f' is released immediately after execution, to free up any
  // potential resources attached to it.
  //
  // REQUIRES: *this must be Join()-ed before it may be destroyed.
  template <int&..., std::convertible_to<Invocable> F>
    requires std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
  explicit Fiber(F&& f)
      : Fiber(Unstarted{}, Current()->options(), std::forward<F>(f)) {
    Start();
  }

  // Equivalent to the semantics above, except that options may be specified.
  template <int&..., std::convertible_to<Invocable> F>
    requires std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
  explicit Fiber(const FiberOptions& options, F&& f)
      : Fiber(Unstarted{}, options, std::forward<F>(f)) {
    Start();
  }

  Fiber(const Fiber&) = delete;
  Fiber& operator=(const Fiber&) = delete;

  // REQUIRES: Join() must have been called.
  ~Fiber();

  // Return a pointer to the currently running fiber.
  //
  // This method works even for threads that were not originally created using a
  // Fiber (e.g. instead using the Thread or Executor interfaces).  The return
  // value does not change for the lifetime of the hosting execution context.
  //
  // Unlike the method below, a "dynamic" fiber is created if the current thread
  // is not currently associated with a fiber.
  //
  // IMPORTANT: Never call this method unless you know Fiber::IsFiber is true or
  // actually want a fiber to be created for the current thread, as described
  // above.
  static Fiber* Current();

  // Returns true if the current thread has a fiber attached to it.
  //
  // Note: The return value of this function is not guaranteed to be consistent
  // over execution.  We may observe changes in the return value as a thread
  // transitions between Fiber contexts.  For example, calling Current() above
  // may result in a false -> true transition.
  static bool IsFiber();

  // Wait until *this and all of its descendants have finished running. When a
  // fiber is created with one of the constructors above, its Join() method must
  // be called before it may be destructed. Second and further calls to Join()
  // are no-ops.
  //
  // REQUIRES: Must be called by the parent, unless this is a root fiber.
  void Join();

  // When called on a fiber f, cancels f and all of its descendants.
  //
  // Regarding the creation of new fibers in a cancelled tree:
  // A fiber's function is always executed, regardless of cancellation state.
  // If a fiber is created within a sub-tree for which cancellation has been
  // delivered, then it will execute as usual, with its cancellation event
  // pre-notified.
  //
  // Cancel() does not 'kill' or 'interrupt' the Fiber; it is up to the
  // cancelled code to detect the cancellation (e.g., with thread::OnCancel() in
  // composition with Select() or thread::Cancelled()) and respond.
  //
  // NOTE: This method only initiates cancellation.  To synchronize with
  // completion callers should use Join() or OnJoinable().
  //
  // Note: avoid calling Fiber::Cancel() while holding a mutex that
  // might be acquired during processing of thread::OnCancel(), as this
  // could lead to a deadlock.
  // See b/274462039 for details.
  void Cancel();

  // When called on a fiber f, returns whether f (or any ancestor) has been
  // cancelled.
  //
  // When querying Fiber::Current() prefer:
  // - thread::Cancelled(), when checking instantaneously, or
  // - thread::OnCancel(), which may be passed to a Select call to wait for the
  //   calling fiber to be cancelled.
  bool Cancelled() const { return cancellation_.HasBeenNotified(); }

  // Returns an event for this fiber's cancellation.
  Case OnCancel() const { return cancellation_.OnEvent(); }

  // When called on a fiber f, returns a token that may be used to Select()
  // against the completion of f and its descendant children.  It is guaranteed
  // at the time of return that f, and all descendants, are no longer running.
  //
  // Will always complete immediately when called against a finished fiber.
  // Does not count as a Join() against the referenced fiber.
  //
  // CAUTION:
  // There is no guarantee made that the fiber f was not immediately deleted
  // prior to Select() returning.  Unless callers are explicitly synchronized
  // against the existence of f's storage, they may NOT reference it again.
  Case OnJoinable() const;

  // The set of options specified when this fiber was created.  Fibers which do
  // not specify their own options inherit from their parent.
  const FiberOptions& options() const { return options_; }

  // Returns the parent fiber of this fiber, or nullptr if this is a
  // root fiber.
  Fiber* parent() const { return parent_; }

 private:
  using CleanupFn = void(Fiber*);

  // No internal constructor starts the fiber. It is the caller's responsibility
  // to call Start() on the fiber.
  struct Unstarted {};

  // Internal c'tor for child fibers.
  explicit Fiber(Unstarted, const FiberOptions& options, Invocable invocable,
                 Fiber* parent = Current());

  // Internal c'tor for root fibers.
  explicit Fiber(Unstarted, Invocable invocable, TreeOptions&& tree_options);

  void Start();
  void Body();
  CleanupFn* MarkFinished();
  void MarkJoined();
  void HandleDeadlineExpiry();
  void InternalJoin();

  static void InvokeFiberBody(void* fiber_ptr);

  internal::PerDomainCounters& PerDomainCounters();

  mutable absl::Mutex mu_;

  // The type of this fiber for stats and debug logging only.
  const fiber::FiberType type_;

  // If non-null, this fiber is self-joining and the supplied cleanup function
  // will be called after it joins itself.
  //
  // This is always set under lock, but is an atomic to allow for reads during
  // stats collection, which cannot acquire mutexes.
  std::atomic<CleanupFn*> detached_cleanup_ ABSL_GUARDED_BY(mu_) = nullptr;

  enum State : uint8_t { RUNNING, FINISHED, JOINED };
  State state_ ABSL_GUARDED_BY(mu_) = RUNNING;

  Fiber* const parent_;
  // List of child fibers.
  gtl::intrusive_list<Fiber, CancellationList::Tag> children_
      ABSL_GUARDED_BY(mu_);

  PermanentEvent cancellation_{PermanentEvent::CancellationEventTag()};
  PermanentEvent joinable_;

  Invocable work_;

  // The scheduler is Ref'd by the root fiber and is held until the root fiber
  // is joined. All children (including detached children) must finish before
  // the root fiber can be joined, so they implicitly know that a reference must
  // exist for their lifetime.
  Scheduler& tree_scheduler_;
  base::Context context_;       // See comments in Body().
  const FiberOptions options_;  // Initialized at creation.

  // Contains min(local deadline, parent deadline) [or only local when !parent].
  const absl::Time min_expiry_deadline_;

  // Must be the last member, we want expiry_'s destructor to run first in the
  // case that it's racing with termination.
  //
  // Triggers Cancel() at deadline expiry.
  const internal::OneShotAlarm expiry_;

  friend class CancelAfterDeadlineTest;
  friend class internal::FiberHelpers;
  friend class internal::DynamicFiber;
  friend struct CurrentFiber;
  friend class gtl::intrusive_list<Fiber, CancellationList::Tag>;
  friend class gtl::intrusive_link<Fiber, CancellationList::Tag>;
  friend void subtle::Detach(Fiber&, CleanupFn&);
  friend absl::AnyInvocable<void() &&> internal::InternalChildFiberScoped(
      const FiberOptions& options, absl::AnyInvocable<void() &&> invocable);
};

namespace internal {

// DynamicFiber represents a fiber without a running body. It can be finished
// manually. It is an internal implementation detail of the fiber library and
// should not be used directly.
class DynamicFiber {
 public:
  // Creates a root DynamicFiber.
  explicit DynamicFiber(TreeOptions options);
  // Creates a child DynamicFiber.
  DynamicFiber(const FiberOptions& options, Fiber* parent);
  // REQUIRES: Finish() and Fiber()->Join() must have been called.
  ~DynamicFiber() = default;

  // Finish the DynamicFiber lifetime if not yet finished.
  void Finish();

  Fiber& GetFiber() { return fiber_; }
  const Fiber& GetFiber() const { return fiber_; }

 private:
  Fiber fiber_;
  bool finished_ = false;
};

}  // namespace internal

// An older name for the Fiber constructor that creates a root fiber. The result
// is provided as a unique pointer.
template <typename F>
ABSL_MUST_USE_RESULT std::unique_ptr<Fiber> NewTree(TreeOptions tree_options,
                                                    F&& f) {
  return std::make_unique<Fiber>(Fiber::RootFiber{}, std::move(tree_options),
                                 std::forward<F>(f));
}

// Detach() creates a new independent fiber tree just like NewTree().  However,
// unlike NewTree(), the detached fiber does not need to be Join()-ed and its
// resources will automatically be released once execution is finished.
template <typename F>
void Detach(TreeOptions tree_options, F&& f) {
  Detach(NewTree(std::move(tree_options), std::forward<F>(f)));
}

// Detach a fiber which is already running. This allows for access to a Fiber*
// for detached fibers without accessing the current fiber from their body. This
// function does not create a new fiber, and all properties of the passed in
// fiber are unchanged.
//
// This function logically takes ownership of the fiber and gives it to the
// fiber itself. The fiber can either be created with NewTree or heap allocated
// with new or std::make_unique.
//
// REQUIRES: The fiber is not currently detached (if it is, it is not possible
// to construct a unique_ptr to it without double free).
void Detach(std::unique_ptr<Fiber> fiber);

// Returns true if the current thread or fiber has been cancelled.
bool Cancelled();

// Returns a Case that only becomes ready after the current fiber is cancelled.
// Note that when this Case becomes ready, it is only an indication that the
// fiber has been cancelled, not that it has finished (the fiber may still be
// running).
//
// NOTE: Returns a valid case regardless of whether caller is a Fiber.  Token is
// still valid in the case that the calling thread is subsequently promoted to
// become a fiber.
//
// Note: avoid calling Fiber::Cancel() while holding a mutex that
// might be acquired during processing of thread::OnCancel(), as this
// could lead to a deadlock.
// See b/274462039 for details.
Case OnCancel();

// API for running some scoped code as a distinct fiber hosted by the current
// thread.
//
// The cancellation for parent fibers will not affect the child fibers, and vise
// versa.
//
// Example:
//    {
//      thread::DistinctFiberScope scope((thread::FiberOptions()));
//      ... some code ...
//    }
//
// This API is designed for executor implementations that want to run
// some user code as an independent fiber in a pre-existing thread,
// but do not want the cancellation of the user code fiber to have any
// affect on the pre-existing thread.
class DistinctFiberScope {
 public:
  explicit DistinctFiberScope(const FiberOptions& options);
  ~DistinctFiberScope();

  DistinctFiberScope(const DistinctFiberScope&) = delete;
  DistinctFiberScope& operator=(const DistinctFiberScope&) = delete;

  // Returns the fiber created by this scope.
  Fiber& fiber() { return inner_.GetFiber(); }

 private:
  Fiber* outer_;
  internal::DynamicFiber inner_;
};

// Wrap a functor in DistinctFiberScope. Can be used with
// non-fiber-aware executors: executor->Schedule(thread::FiberScoped(callback)).
template <typename F>
ABSL_MUST_USE_RESULT inline absl::AnyInvocable<void() &&> FiberScoped(
    const FiberOptions& options, F&& fn) {
  return [fn = std::forward<F>(fn), options]() mutable {
    DistinctFiberScope scope(options);
    std::move(fn)();
  };
}

template <typename F>
ABSL_MUST_USE_RESULT inline absl::AnyInvocable<void() &&> FiberScoped(F&& fn) {
  return FiberScoped(FiberOptions(), std::forward<F>(fn));
}

// Wrap a functor so that, upon execution, it runs in a fiber that is
// the child of the current fiber.
//
// The intended/common use case is to post a continuation of a non-fiber-aware
// asynchronous operation back into the fiber context/scheduler of the
// currently running fiber:
//
//   <parent fiber context>
//     asyncOp->RunOnCompletion(ChildFiberScoped(continuation));
//   </parent fiber context>
//
// Note: the result _must_ be used exactly _once_.
//
// REQUIRES: Fiber::IsFiber().
template <typename F>
ABSL_MUST_USE_RESULT absl::AnyInvocable<void() &&> ChildFiberScoped(
    const FiberOptions& options, F&& invocable) {
  return internal::InternalChildFiberScoped(options,
                                            std::forward<F>(invocable));
}

template <typename F>
ABSL_MUST_USE_RESULT absl::AnyInvocable<void() &&> ChildFiberScoped(
    F&& invocable) {
  return internal::InternalChildFiberScoped(std::forward<F>(invocable));
}

// Creates an Invocable, checking that it actually returns void.
template <typename F, typename = std::invoke_result_t<F>>
ABSL_DEPRECATED("Feed the callable directly to the Fiber constructor")
Invocable MakeInvocable(F&& f) {
  static_assert(std::is_void_v<std::invoke_result_t<std::decay_t<F>>>,
                "Functions passed to MakeInvocable must return void.");
  return Invocable(std::forward<F>(f));
}

namespace subtle {

// The most generic overload of Detach. Accepts the fiber to detach, and a
// cleanup function to be called after it joins itself.
//
// The cleanup function runs under an unspecified context. It shouldn't do
// anything but destroy the fiber object and any associated state. For example
// it must not wait for anything (how would fiber cancellation work?), or rely
// on a particular base::Context being active.
//
// REQUIRES: the fiber hasn't already been detached.
void Detach(Fiber& fiber, void (&clean_up)(Fiber*));

}  // namespace subtle

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_H_
