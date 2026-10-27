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

#include "gloop/thread/fiber/fiber.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "absl/base/internal/thread_identity.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/cancellation_coloring.h"
#include "gloop/base/context.h"
#include "gloop/base/googleinit.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/thread/fiber/fiber-internal.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.pb.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/per-domain-counters.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gloop/thread/thread.h"
#include "gloop/util/gtl/intrusive_list.h"

ABSL_RETIRED_FLAG(bool, fibers_use_cooperative_scheduling, true, "Retired.");

ABSL_FLAG(int32_t, fiber_scheduler_default_slots, 1,
          "Default number of CPU slots in a fiber scheduler.  Default is 1 "
          "for backwards compatibility, but a higher value, such as 4, may "
          "help avoid poor scheduling interactions that lead to deadlocks "
          "or slowdowns.  (See also <link>)");

// TODO: Remove after soaking.
ABSL_FLAG(
    bool, fiber_cancel_dynamic_on_thread_exit, true,
    "If true, cancel the dynamic fiber, if it exists, on thread exit if it "
    "still has children (children that were detached while running and are "
    "only tearing down do not count). This flag is intended for rollback "
    "only-");

// Implementation notes:
//
// Locking:
//   Lock acquisition order:
//     ancestor `mu_` -> descendant `mu_` -> `PermanentEvent::lock_`
//                    -> `Selector::mu` (or `OnTraceSignal` listener leaf locks)
//   Child Fiber construction (when inheriting cancellation) and Cancel() nest
// `parent->mu_` before `child->mu_`. Helpers that update both a child and its
// parent (MarkJoinedBeforeTeardown(), LeaveParent()) release `mu_` before
// acquiring `parent_->mu_`; `child->mu_ -> parent_->mu_` is never nested. No
// fiber `mu_` is held across base::Context teardown, Select(), or `clean_up`.
//
// State transitions:
//   RUNNING               : Initial state.
//   RUNNING  -> FINISHED  : Set by MarkFinished() or FinishBeforeTeardown().
//   FINISHED -> JOINED    : Set by MarkJoined() or, for detached fibers, by
//                           MarkJoinedBeforeTeardown() or
//                           JoinDetachedAndCleanUp().
//
// All fibers will undergo all of these transitions.
//
// Context teardown:
//   Destroying the base::Context of a fiber (the base::WithContext in Body())
// runs trace listener callbacks, and may drop the last reference to a tracer,
// which then notifies its trace consumers. These may use state that is
// destroyed by whoever waits for the fiber to become joinable (Join(),
// OnJoinable(), Bundle::JoinAll(), a parent waiting for its detached
// children), so the teardown must complete before any such waiter is woken.
// It may also block, or acquire arbitrary locks, so it runs without holding
// any fiber's `mu_`.
//
//   To this end, `pending_teardowns_` counts the in-progress teardowns tracked
// on the fiber in addition to `children_`: those of detached children, which
// leave `children_` in MarkJoinedBeforeTeardown() before their teardown, and
// the fiber's own teardown if it is attached and still has children or pending
// teardowns when its body completes (if neither remains, AfterTeardown::
// kMakeJoinable destroys the fiber's own context first without incrementing
// `pending_teardowns_`). Once the fiber is FINISHED with no children nor
// pending teardowns left, MakeJoinableLocked() notifies `joinable_` under
// `mu_`.
//
//   The corresponding TraceSignal() causality annotation is emitted earlier,
// as soon as `state_ == FINISHED && children_.empty()` holds (by
// FinishBeforeTeardown(), MarkFinished(), MarkJoinedBeforeTeardown(), or
// LeaveParent(); a detached fiber waiting only on `pending_teardowns_` also
// emits its own `joinable_` TraceSignal() before waiting). This runs while the
// context of the fiber making the event due (or of the caller of Detach() or
// MarkFinished()) is still active, except when JoinDetachedAndCleanUp(), run
// once a teardown has completed, joins a fiber that was detached after
// finishing, and LeaveParent() thereby leaves its finished parent without
// children (whether or not that parent then becomes joinable or was itself
// detached).
//
//   Detach() does not wait for descendants or teardown either: a fiber that is
// detached after finishing as an attached fiber, but before becoming joinable,
// sets `cleanup_when_joinable_`. Whoever later makes it joinable notifies
// `joinable_` in MakeJoinableLocked() and, after releasing `mu_`, joins and
// cleans up the fiber (and any finished late-detached ancestors it makes
// joinable) via JoinDetachedAndCleanUp().
namespace thread {

extern void InternalSetCurrentFiberName(absl::string_view fiber_name);
extern absl::string_view InternalGetCurrentFiberName();

struct CurrentFiber {
  Fiber* f = nullptr;
  ~CurrentFiber() {
    // This destructor is called while destroying thread-local storage. If it is
    // null, there is no dynamic fiber for this thread.
    if (f == nullptr) return;
    const bool children_finished = f->MarkFinished();
    if (!children_finished &&
        absl::GetFlag(FLAGS_fiber_cancel_dynamic_on_thread_exit)) [[unlikely]] {
      TryCancelAndWait();
    }
    f->InternalJoin();
    delete f;
  }

  void TryCancelAndWait() {
    f->Cancel();
    if (SelectUntil(absl::Now() + absl::Seconds(5), {f->OnJoinable()}) == -1) {
      LOG(DFATAL)
          << "Cancelled root fiber failed to become joinable in 5 seconds.";
    }
  }
};

STATIC_THREAD_LOCAL(CurrentFiber, current_fiber);

static Fiber** GetPerThreadFiberPtr() { return &current_fiber.get().f; }

namespace internal {

// Only called in debug builds.
static bool ValidateSchedulerTreeOptions(const TreeOptions& tree_options) {
  if (tree_options.scheduler() != nullptr) {
    CHECK(tree_options.parent_scheduler() == nullptr)
        << "Specifying parent scheduler with scheduler() set";
    CHECK(tree_options.max_cpu_slots() == 0)
        << "Specifying max_cpu_slots with scheduler() set";
  } else {
    CHECK_LE(0, tree_options.max_cpu_slots())
        << "Invalid max_cpu_slots specified";
  }
  return true;
}

static Scheduler* absl_nonnull InitTreeScheduler(
    const TreeOptions& tree_options) {
  DCHECK(ValidateSchedulerTreeOptions(tree_options));
  base::scheduling::Scheduler* scheduler = tree_options.scheduler();
  if (scheduler) {
    scheduler->Ref();  // Creator responsible for calling Orphan in this case.
    return scheduler;
  }

  base::scheduling::Scheduler* parent = tree_options.parent_scheduler();
  if (parent == nullptr) {
    parent = DefaultDomain()->root_scheduler();
  }

  // Determine the number of slots from the tree options and the parent
  // scheduler.
  int num_slots = std::min(absl::GetFlag(FLAGS_fiber_scheduler_default_slots),
                           parent->num_slots());
  if (tree_options.max_cpu_slots() > 0) {
    num_slots = std::min(tree_options.max_cpu_slots(), parent->num_slots());
  }

  // By default we allocate a LIFO scheduler for sub-trees as they are
  // expected to typically correspond to a single request.  In this scenario,
  // the parent depends solely on the completion of child work -- not its
  // ordering -- allowing LIFO to be more cache efficient.
  scheduler = NewChildLIFOScheduler(parent, num_slots);
  scheduler->Ref();
  // We immediately orphan; LeaveParent() or FinishAfterTeardown() will release
  // the reference above.
  scheduler->Orphan();
  return scheduler;
}

extern int InternalRequestedStackSizeToStackSizeClass(
    size_t requested_stack_size, int flag_requested_stack_size);

// static
const Fiber* FiberHelpers::FiberFromSchedulable(
    const base::scheduling::Schedulable* sched) {
  if (!base::scheduling::IsFiberAttached(sched)) return nullptr;
  return reinterpret_cast<const Fiber*>(sched->managed_arg());
}

// static
bool FiberHelpers::IsFiberDetached(const Fiber* fiber) {
  return ABSL_TS_UNCHECKED_READ(fiber->detached_cleanup_)
             .load(std::memory_order_relaxed) != nullptr;
}

// static
fiber::FiberType FiberHelpers::GetFiberType(const Fiber* fiber) {
  return fiber->type_;
}

// static
base::scheduling::Domain* FiberHelpers::GetFiberDomain(const Fiber* fiber) {
  return fiber->tree_scheduler_.domain();
}

// static
Scheduler* FiberHelpers::GetScheduler(const Fiber* fiber) {
  return &fiber->tree_scheduler_;
}

// static
void FiberHelpers::StartDetachedChildFiber(
    Fiber* parent, absl::AnyInvocable<void() &&> invocable) {
  Fiber* const fiber = new Fiber{
      Fiber::Unstarted{},
      parent->options(),
      std::move(invocable),
      parent,
  };

  Detach(absl::WrapUnique(fiber));
  fiber->Start();
}

// static
void FiberHelpers::StartDetachedTree(TreeOptions tree_options,
                                     absl::AnyInvocable<void() &&> invocable) {
  Fiber* const fiber = new Fiber{
      Fiber::Unstarted{},
      std::move(invocable),
      std::move(tree_options),
  };

  Detach(absl::WrapUnique(fiber));
  fiber->Start();
}

absl::AnyInvocable<void() &&> InternalChildFiberScoped(
    const FiberOptions& options, absl::AnyInvocable<void() &&> invocable) {
  Fiber* const fiber = new Fiber{
      Fiber::Unstarted{},
      options,
      std::move(invocable),
      Fiber::Current(),
  };

  return [fiber]() {
    // Detach before starting: see FiberHelpers::StartDetachedChildFiber().
    Detach(absl::WrapUnique(fiber));
    fiber->Start();
  };
}

absl::AnyInvocable<void() &&> InternalChildFiberScoped(
    absl::AnyInvocable<void() &&> invocable) {
  return InternalChildFiberScoped(Fiber::Current()->options(),
                                  std::move(invocable));
}

}  // namespace internal

// Adapter for Domain::CreateExectuableSchedulable()
void Fiber::InvokeFiberBody(void* fiber_ptr) {  // static
  Fiber* fiber = reinterpret_cast<Fiber*>(fiber_ptr);
  fiber->Body();
}

Fiber::Fiber(Unstarted, const FiberOptions& options, Invocable invocable,
             Fiber* parent)
    : type_(invocable ? fiber::FIBER_TYPE_NORMAL : fiber::FIBER_TYPE_DYNAMIC),
      parent_(parent),
      work_(std::move(invocable)),
      tree_scheduler_(parent_->tree_scheduler_),
      context_([&] {
        // For normal fibers, create a copy of the current context using
        // `kThread` and the assigned `name` for tracing purposes.
        // The `Context::kThread` constructor emits Dapper causality annotations
        // for the code execution scheduled in this context.
        // Dynamic fibers have no executing code associated with it, so we just
        // use `base::BackgroundContext()` which emits no causality events.
        // See `ThreadInitType` constructor in `base::Context` for more info.
        return type_ == fiber::FIBER_TYPE_NORMAL
                   ? base::Context(base::Context::kThread, options.name())
                   : base::BackgroundContext();
      }()),
      options_(options),
      min_expiry_deadline_(std::min(base::CurrentContext().deadline(),
                                    parent_->min_expiry_deadline_)),
      expiry_(
          // Only set alarms that will fire before the parent's.
          min_expiry_deadline_ < parent_->min_expiry_deadline_
              ? min_expiry_deadline_
              : absl::InfiniteFuture(),
          [this] { HandleDeadlineExpiry(); }) {
  PerDomainCounters().fibers_created.fetch_add(1, std::memory_order_relaxed);
  PerDomainCounters().num_fibers.fetch_add(1, std::memory_order_relaxed);
  // Note: We become visible to cancellation as soon as we're added to parent.
  absl::MutexLock l(parent_->mu_);
  CHECK_EQ(parent_->state_, RUNNING);
  parent_->children_.push_back(this);
  if (parent_->cancellation_.HasBeenNotified()) {
    // Fibers adjoined to a cancelled tree inherit implicit cancellation.
    DVLOG(2) << "F " << this << " joining cancelled sub-tree.";
    Cancel();
  }
}

Fiber::Fiber(Unstarted, Invocable invocable, TreeOptions&& tree_options)
    : type_(invocable ? fiber::FIBER_TYPE_NORMAL : fiber::FIBER_TYPE_DYNAMIC),
      parent_(nullptr),
      work_(std::move(invocable)),
      tree_scheduler_(*internal::InitTreeScheduler(tree_options)),
      context_(type_ == fiber::FIBER_TYPE_NORMAL
                   ? tree_options.consume_context(this)
                   : base::BackgroundContext()),
      options_(tree_options.fiber_options()),
      // For dynamic fibers, this will be `InfiniteFuture` since
      // `BackgroundContext` has no deadline.
      min_expiry_deadline_(context_.deadline()),
      expiry_(min_expiry_deadline_, [this] { HandleDeadlineExpiry(); }) {
  PerDomainCounters().fibers_created.fetch_add(1, std::memory_order_relaxed);
  PerDomainCounters().num_fibers.fetch_add(1, std::memory_order_relaxed);
}

internal::PerDomainCounters& Fiber::PerDomainCounters() {
  static_assert(internal::kNumFiberTypes == fiber::FiberType_ARRAYSIZE);
  return tree_scheduler_.domain()->MutableCounters(type_);
}

void Fiber::HandleDeadlineExpiry() {
  {
    absl::MutexLock l(mu_);
    // Already joined, unsafe to access counters, no cancellation needed.
    if (state_ == JOINED) return;
    PerDomainCounters().fibers_expired.fetch_add(1, std::memory_order_relaxed);
  }
  Cancel();
}

Fiber::~Fiber() {
  CHECK_EQ(JOINED, state_) << "F " << this << " attempting to destroy an "
                           << "unjoined Fiber.  (Did you forget to Join() "
                           << "on a child?)";
  DCHECK(children_.empty());
  DCHECK_EQ(pending_teardowns_, 0);

  DVLOG(2) << "F " << this << " destroyed";
}

// Either returns a reference to the fiber owned by the current execution
// context, or creates a new one about it.  Fiber's constructors above [and
// ::Current()] use this to bind implicit Fibers when called from a non-fiber
// entity at run-time.
Fiber* Fiber::Current() {
  Fiber** fiber_ptr = GetPerThreadFiberPtr();

  if (ABSL_PREDICT_FALSE(*fiber_ptr == nullptr)) {
    *fiber_ptr = new Fiber(Unstarted{}, /*invocable=*/nullptr, TreeOptions{});
  }

  return *fiber_ptr;
}

bool Fiber::IsFiber() { return *GetPerThreadFiberPtr() != nullptr; }

// Runs in the thread of the execution entity spawning this new fiber.
void Fiber::Start() {
  DVLOG(2) << "F " << this << " starting; parent = " << parent_;

  base::scheduling::Schedulable* schedulable;
  schedulable = tree_scheduler_.domain()->CreateExecutableSchedulable(
      &tree_scheduler_, InvokeFiberBody, this);
  DCHECK(schedulable->managed_arg() == reinterpret_cast<intptr_t>(this));
  // The fiber is detached from the schedulable at the end of Fiber::Body().
  base::scheduling::InternalAttachFiber(schedulable, this);

  base::scheduling::Downcalls::Post(schedulable);
}

#ifndef NDEBUG
static void CheckCancellationColorForFiberBody() {
  // Assumption check: we're about to block on fiber-colored work, making no
  // effort to respect another ecosystem's cancellation semantics. So we'd
  // better not be currently running under another cancellation color.
  //
  // In practice this should always be satisfied, because we are running near
  // the top of the thread call stack, with no ability for other ecosystems to
  // interpose.
  switch (const base::internal::CancellationColor c =
              base::internal::GetActiveCancellationColor()) {
    case base::internal::CancellationColor::kUnknown:
    case base::internal::CancellationColor::kFibers:
      break;

    default:
      LOG(FATAL) << "Unexpected cancellation color: " << c;
  }
}
#endif

// Runs the work associated with this fiber, in the thread associated with this
// fiber.
//
// Not invoked for dynamic fibers.
void Fiber::Body() {
  DVLOG(2) << "F " << this << " running ";
  Fiber** ptr = GetPerThreadFiberPtr();
  Fiber* old = *ptr;  // TODO: Better checking for !(should donate)?
  *ptr = this;

  thread::InternalSetCurrentFiberName(options_.name());

  PerDomainCounters().num_active_fibers.fetch_add(1, std::memory_order_relaxed);

#ifndef NDEBUG
  // Run the work under the fibers cancellation color.
  CheckCancellationColorForFiberBody();
  const base::internal::WithCancellationColor wcc(
      base::internal::CancellationColor::kFibers);
#endif

  FinishResult finish_result;
  {
    // The fiber's context remains active until the end of this scope, so that
    // the trace events of FinishBeforeTeardown() are attributed to it.
    base::WithContext with_context(std::move(context_));
    std::move(work_)();
    // Immediately free the storage.  This matches the behavior of NewCallback
    // and allows ref-counting arguments to be scoped to the fiber's body (e.g.
    // auto-closing channels).
    work_ = nullptr;

    DCHECK(*ptr == this);
    *ptr = old;
    DVLOG(2) << "F " << this << " finished";

    PerDomainCounters().num_active_fibers.fetch_sub(1,
                                                    std::memory_order_relaxed);

    // A call to fiber_stats::Get() pulls fibers out of schedulables; but
    // after this call the fiber can be destroyed, so we "detach" this fiber
    // from its schedulable (attached in Fiber::Start()).
    base::scheduling::InternalDetachFiber(this);

    // Reset current fiber name as this thread may be recycled, and we don't
    // want misattribution until someone else sets an explicit name for this
    // thread. This also alleviates lifetime concerns for the name itself.
    thread::InternalSetCurrentFiberName("");

    finish_result = FinishBeforeTeardown();
  }  // Destroys the fiber's context: see "Context teardown" above.
  FinishAfterTeardown(finish_result);
}

// Marks *this FINISHED, with its context still active, emitting the
// TraceSignal() of any joinable event that this makes due. A detached fiber
// then also waits for its children (including their context teardown), and
// leaves its parent. Returns what is left to do once the fiber's context has
// been destroyed.
Fiber::FinishResult Fiber::FinishBeforeTeardown() {
  CleanupFn* clean_up;
  bool wait_for_children;
  {
    absl::MutexLock l(mu_);
    MarkFinishedLocked();
    clean_up = detached_cleanup_.load(std::memory_order_relaxed);
    if (clean_up == nullptr) {
      if (children_.empty()) {
        joinable_.TraceSignalForNotify();
        if (pending_teardowns_ == 0) {
          // No child can make us joinable anymore (and none can be added), so
          // we will, once our context has been destroyed.
          return {AfterTeardown::kMakeJoinable, nullptr};
        }
      }
      // A child may finish last: make it wait for our own teardown.
      ++pending_teardowns_;
      return {AfterTeardown::kEndOwnPendingTeardown, nullptr};
    }
    // This is a self-joining fiber, but we may need to wait for children.
    wait_for_children = !children_.empty() || pending_teardowns_ != 0;
    if (wait_for_children && children_.empty()) {
      joinable_.TraceSignalForNotify();
    }
  }
  if (wait_for_children) Select({joinable_.OnEvent()});
  const AfterTeardown after_teardown =
      MarkJoinedBeforeTeardown() ? AfterTeardown::kEndPendingTeardownInParent
                                 : AfterTeardown::kReleaseTree;
  return {after_teardown, clean_up};
}

// Marks *this, a finished detached fiber with no children nor pending
// teardowns left, JOINED. If it has a parent, moves it from the parent's
// children to the parent's pending teardowns, emitting the TraceSignal() of
// the parent's joinable event if this makes it due. Returns whether *this has
// a parent.
bool Fiber::MarkJoinedBeforeTeardown() {
  {
    absl::MutexLock l(mu_);
    DCHECK(children_.empty());
    DCHECK_EQ(pending_teardowns_, 0);
    DCHECK_EQ(state_, FINISHED);
    DVLOG(2) << "F " << this << " joined";
    state_ = JOINED;
  }
  if (parent_ == nullptr) return false;
  absl::MutexLock l(parent_->mu_);
  parent_->children_.erase(this);
  ++parent_->pending_teardowns_;
  if (parent_->children_.empty() && parent_->state_ == FINISHED) {
    parent_->joinable_.TraceSignalForNotify();
  }
  return true;
}

// Completes Body() once the fiber's context has been destroyed, without
// holding any fiber's `mu_`. This may wake a joiner which then destroys *this,
// so attached fibers must not be accessed afterwards.
void Fiber::FinishAfterTeardown(const FinishResult result) {
  switch (result.after_teardown) {
    case AfterTeardown::kMakeJoinable: {
      {
        absl::MutexLock l(mu_);
        if (!MakeJoinableLocked()) return;
      }
      JoinDetachedAndCleanUp();
      return;
    }
    case AfterTeardown::kEndOwnPendingTeardown:
      EndPendingTeardown();
      return;
    case AfterTeardown::kEndPendingTeardownInParent:
      parent_->EndPendingTeardown();
      result.clean_up(this);
      return;
    case AfterTeardown::kReleaseTree:
      // All of our descendants have finished, including their teardown.
      // Release our ref on the scheduler.
      tree_scheduler_.Unref();
      result.clean_up(this);
      return;
  }
  ABSL_UNREACHABLE();
}

// Ends one of the teardowns counted by `pending_teardowns_`, making *this
// joinable if it is now due. Its TraceSignal() has already been emitted when
// *this became FINISHED without children.
void Fiber::EndPendingTeardown() {
  {
    absl::MutexLock l(mu_);
    DCHECK_GT(pending_teardowns_, 0);
    if (--pending_teardowns_ != 0 || state_ != FINISHED || !children_.empty()) {
      return;
    }
    if (!MakeJoinableLocked()) return;
  }
  JoinDetachedAndCleanUp();
}

// Makes *this, a FINISHED fiber with no children nor pending teardowns left,
// joinable by notifying `joinable_`. Returns `cleanup_when_joinable_`; if true,
// the caller must call JoinDetachedAndCleanUp() once it has released `mu_`.
bool Fiber::MakeJoinableLocked() {
  DCHECK_EQ(state_, FINISHED);
  DCHECK(children_.empty());
  DCHECK_EQ(pending_teardowns_, 0);
  DCHECK(!joinable_.HasBeenNotified(
      PermanentEvent::SuppressCancellationColorCheckTag{}));
  joinable_.NotifyWithoutTraceSignal();
  return cleanup_when_joinable_;
}

// Joins and cleans up *this, which was detached after finishing as an attached
// fiber and has now had `joinable_` notified, and likewise for each finished
// late-detached ancestor that leaving its parent makes joinable.
void Fiber::JoinDetachedAndCleanUp() {
  Fiber* fiber = this;
  do {
    CleanupFn* clean_up;
    {
      absl::MutexLock l(fiber->mu_);
      DCHECK(fiber->cleanup_when_joinable_);
      DCHECK(fiber->children_.empty());
      DCHECK_EQ(fiber->pending_teardowns_, 0);
      DCHECK_EQ(fiber->state_, FINISHED);
      DVLOG(2) << "F " << fiber << " joined";
      fiber->state_ = JOINED;
      clean_up = fiber->detached_cleanup_.load(std::memory_order_relaxed);
    }
    Fiber* const parent = fiber->LeaveParent();
    clean_up(fiber);
    fiber = parent;
  } while (fiber != nullptr);
}

// Removes *this, which has just been joined, from its parent's children, or
// releases the scheduler if *this is a root fiber. If this leaves a FINISHED
// parent with no children, emits `parent_->joinable_`'s TraceSignal() (note:
// when called from JoinDetachedAndCleanUp(), the descendant's context has
// already been destroyed) and, if no teardowns are pending on the parent,
// makes it joinable. Returns the parent if it now needs
// JoinDetachedAndCleanUp(), or nullptr otherwise.
Fiber* Fiber::LeaveParent() {
  if (parent_ == nullptr) {
    // All of our descendants have been joined and torn down. Release our ref
    // on the scheduler.
    tree_scheduler_.Unref();
    return nullptr;
  }
  absl::MutexLock l(parent_->mu_);
  parent_->children_.erase(this);
  if (!parent_->children_.empty() || parent_->state_ != FINISHED) {
    return nullptr;
  }
  // Our own context teardown completed before we became joinable, so the
  // parent does not need to wait for it (though other detached siblings'
  // teardowns may still be pending in `parent_->pending_teardowns_`).
  parent_->joinable_.TraceSignalForNotify();
  if (parent_->pending_teardowns_ != 0 || !parent_->MakeJoinableLocked()) {
    return nullptr;
  }
  return parent_;
}

void Fiber::InternalJoin() {
  Select({joinable_.OnEvent()});
  MarkJoined();
}

void Fiber::Join() {
  // Join must be externally called and so can never be valid when detached.  It
  // is important to detect this since we may not safely proceed beyond Select()
  // in this case.
  DCHECK(!internal::FiberHelpers::IsFiberDetached(this))
      << "Join() on detached fiber.";

  Fiber* current_fiber = *GetPerThreadFiberPtr();
  CHECK(this != current_fiber) << "Fiber trying to join itself!";
  if (parent_ != nullptr) {
    CHECK(parent_ == current_fiber) << "Join() called from non-parent fiber";
  }

  InternalJoin();
}

// Update *this, a dynamic fiber, to a FINISHED state, notifying any waiters if
// it has become joinable. Returns whether `children_` is empty.
//
// State transition is a no-op if *this has already been marked finished, e.g.
// by a concurrent DynamicFiber::Finish().
bool Fiber::MarkFinished() {
  absl::MutexLock l(mu_);
  DCHECK_EQ(type_, fiber::FIBER_TYPE_DYNAMIC);
  DCHECK_EQ(detached_cleanup_.load(std::memory_order_relaxed), nullptr);
  if (state_ != RUNNING) return children_.empty();
  MarkFinishedLocked();

  if (children_.empty()) {
    // All children have finished; `joinable_` is notified now unless detached
    // children are still tearing down.
    joinable_.TraceSignalForNotify();
    if (pending_teardowns_ == 0) {
      joinable_.NotifyWithoutTraceSignal();
    }
    return true;
  }
  return false;
}

void Fiber::MarkFinishedLocked() {
  DCHECK_EQ(state_, RUNNING);
  {
    internal::PerDomainCounters& counters = PerDomainCounters();
    counters.fibers_finished.fetch_add(1, std::memory_order_relaxed);
    counters.num_fibers.fetch_sub(1, std::memory_order_relaxed);
  }

  state_ = FINISHED;
}

// Record that the Join() requirement has been satisfied, by Join() or by
// detaching a fiber that had already become joinable. (Detached fibers
// otherwise join themselves with MarkJoinedBeforeTeardown(), or are joined by
// JoinDetachedAndCleanUp().)
//
// If *this was a child fiber it will be removed from its parent's active
// children.
//
// No-op if *this has already been Join()-ed.
void Fiber::MarkJoined() {
  DCHECK(joinable_.HasBeenNotified());

  {
    absl::MutexLock l(mu_);
    DCHECK(children_.empty());
    DCHECK_EQ(pending_teardowns_, 0);
    if (state_ == JOINED) return;  // Already joined.
    DCHECK_EQ(state_, FINISHED);
    DVLOG(2) << "F " << this << " joined";
    state_ = JOINED;
  }
  if (Fiber* const parent = LeaveParent(); parent != nullptr) {
    parent->JoinDetachedAndCleanUp();
  }
}

void Fiber::Cancel() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  Fiber* fiber = this;
  Fiber* fiber_parent = fiber->parent_;
  while (true) {
    DCHECK(fiber != nullptr);
    // We visit nodes in post-order, traversing each child sub-tree by sibling
    // position before operating on the parent.  We hold all "mu_"s up to and
    // including the initiating parent fiber node.
    fiber->mu_.lock();

    // Check whether the fiber we're currently visiting has already been
    // cancelled.
    //
    // We don't want to do a cancellation coloring check here because the
    // currently-running thread is checking the cancellation status of a
    // potentially different fiber, not its own. It's fine to call Cancel on
    // some other fiber.
    //
    // Indeed as far as the user is concerned we're not even checking the
    // cancellation status, we're setting it -- this check is here only for use
    // in an optimization below.
    bool cancelled = fiber->cancellation_.HasBeenNotified(
        PermanentEvent::SuppressCancellationColorCheckTag{});

    // If we have children, and we're already cancelled, then they must be also.
    // We don't need to re-walk our children as future descendants will be
    // spawned into a cancelled state.
    // If we have children, and we're not cancelled, we must visit them before
    // operating on "fiber".
    if (!cancelled && !fiber->children_.empty()) {
      // Equivalent recursion note: recursive call.
      fiber_parent = fiber;
      fiber = &fiber->children_.front();
      continue;
    }

    while (true) {
      if (!cancelled) {
        // It is only valid to access the domain counters if the domain is still
        // alive, which is only guaranteed if the root fiber has not been
        // joined. In theory, all users should abide by this, but in practice
        // there are many cases of cancelling already joined fibers.
        if (state_ != Fiber::JOINED) {
          // Each iteration of the while loop corresponds to visiting a single
          // node (fiber). As such, increment our cancellation counter, here,
          // inside the while loop and guarded by !cancelled is correct as
          // we will only increment out cancelled counter once per fiber.
          fiber->PerDomainCounters().fibers_cancelled.fetch_add(
              1, std::memory_order_relaxed);
        }
        fiber->cancellation_.Notify();
      }

      class ScopedMutexUnlocker {
       public:
        explicit ScopedMutexUnlocker(absl::Mutex* mu) : mu_(*mu) {}
        ~ScopedMutexUnlocker() { mu_.unlock(); }

       private:
        absl::Mutex& mu_;
      };
      ScopedMutexUnlocker unlock_mu(&fiber->mu_);

      // Once we reach the fiber (*this) parenting cancellation, we're finished.
      if (fiber == this) return;

      DCHECK_EQ(fiber_parent, fiber->parent_);
      DCHECK(fiber_parent != nullptr);
      DCHECK(!fiber_parent->children_.empty());

      // Construct an iterator to the child list this is a part of.
      gtl::intrusive_list<Fiber, CancellationList::Tag>::iterator it(fiber);
      ++it;

      // If there is a unvisited sibling, we go there to process it.
      // Equivalent recursion note: recursive call return and recursive call.
      if (it != fiber_parent->children_.end()) {
        fiber = &*it;  // No change to `fiber_parent`.
        break;
      }

      // We've reached the final sibling in this subtree.  Continue traversing
      // from our parent, whom has no more unvisited children and is next in the
      // traversal order.
      // Equivalent recursion note: recursive call return.
      fiber = fiber_parent;
      fiber_parent = fiber_parent->parent_;

      // Reached child => traversal spans our parent, which must need
      // cancellation.
      cancelled = false;
    }
  }
}

Case Fiber::OnJoinable() const { return joinable_.OnEvent(); }

void Detach(std::unique_ptr<Fiber> fiber) {
  DCHECK(fiber != nullptr);
  return subtle::Detach(
      *fiber.release(), *+[](Fiber* const fiber) { delete fiber; });
}

void subtle::Detach(Fiber& fiber, Fiber::CleanupFn& clean_up) {
  {
    absl::MutexLock l(fiber.mu_);

    // Check our requirement that the fiber not already be detached. It doesn't
    // make sense to do it twice, because we'd have two functions that want to
    // clean it up afterward.
    DCHECK_EQ(fiber.detached_cleanup_.load(std::memory_order_relaxed), nullptr)
        << "fiber already detached";
    fiber.detached_cleanup_.store(&clean_up, std::memory_order_relaxed);
    if (ABSL_PREDICT_TRUE(fiber.state_ == Fiber::RUNNING)) return;

    // Special case: the fiber has already finished, as an attached fiber, so it
    // won't join and clean itself up. If it is not joinable yet, whoever makes
    // it joinable will do it instead (see MakeJoinableLocked()), so that we
    // never wait for its descendants and its context teardown.
    DCHECK_EQ(fiber.state_, Fiber::FINISHED) << "fiber already joined";
    if (!fiber.joinable_.HasBeenNotified()) {
      fiber.cleanup_when_joinable_ = true;
      return;
    }
  }

  // Otherwise, it's our job to join and clean up the fiber. This does not wait,
  // as the fiber is joinable. Call InternalJoin to bypass the check that the
  // current fiber is the parent: any fiber may detach it.
  fiber.InternalJoin();
  clean_up(&fiber);
}

Case OnCancel() { return Fiber::Current()->OnCancel(); }

bool Cancelled() {
  internal::CheckActiveCancellationColor();

  Fiber** fiber_ptr = GetPerThreadFiberPtr();
  if (*fiber_ptr == nullptr) {
    // Only threads which are already fibers could be cancelled.
    return false;
  }
  return (*fiber_ptr)->Cancelled();
}

namespace internal {

DynamicFiber::DynamicFiber(TreeOptions options)
    : fiber_(Fiber::Unstarted{}, /*invocable=*/nullptr, std::move(options)) {}

DynamicFiber::DynamicFiber(const FiberOptions& options, class Fiber* parent)
    : fiber_(Fiber::Unstarted{}, options, /*invocable=*/nullptr, parent) {}

// May be called concurrently, e.g. by Bundle::OnJoinable() in several fibers:
// MarkFinished() only finishes the fiber once.
void DynamicFiber::Finish() { fiber_.MarkFinished(); }

}  // namespace internal

static Scheduler* GetCurrentScheduler() {
  const auto* const identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  if (!identity) {
    return nullptr;
  }
  const auto* const schedulable =
      base::scheduling::Schedulable::GetBoundSchedulable(identity);
  if (!schedulable) {
    return nullptr;
  }
  return schedulable->manager;
}

DistinctFiberScope::DistinctFiberScope(const FiberOptions& options)
    : outer_(*GetPerThreadFiberPtr()),
      inner_(TreeOptions{}.set_fiber_options(options).set_scheduler(
          GetCurrentScheduler())) {
  *GetPerThreadFiberPtr() = &inner_.GetFiber();
}

DistinctFiberScope::~DistinctFiberScope() {
  inner_.Finish();
  // Swap back to the outer fiber before Join.
  *GetPerThreadFiberPtr() = outer_;
  inner_.GetFiber().Join();
}

REGISTER_MODULE_INITIALIZER(fiber_thread_exit_handler, {
  // If a thread exits in the middle fo a fiber execution via pthread_exit,
  // ReleaseDynamicallyCreatedFiber() will often check-fail, thus potentially
  // clobbering useful debug output, so we clear fiber_ptr below to
  // disable all checks. See b/154611512.
  Thread::RegisterExitHandler([]() {
    Fiber** fiber_ptr = GetPerThreadFiberPtr();
    *fiber_ptr = nullptr;
  });
});

}  // namespace thread
