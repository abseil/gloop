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
    "If true, cancel the dynamic fiber if it exists on thread exit if it is "
    "not already joinable. This flag is intended for rollback only-");

// Implementation notes:
//
// Locking:
//   There is currently no nesting of mu_(s) in parent/child interactions.
// Should this arise, parent should be ordered before child.
//
// State transitions:
//   RUNNING               : Initial state.
//   RUNNING  -> FINISHED  : Set by MarkFinished()
//   FINISHED -> JOINED    : Set by MarkJoined()
//
// All fibers will undergo all of these transitions.
namespace thread {

extern void InternalSetCurrentFiberName(absl::string_view fiber_name);
extern absl::string_view InternalGetCurrentFiberName();

struct CurrentFiber {
  Fiber* f = nullptr;
  ~CurrentFiber() {
    // This destructor is called while destroying thread-local storage. If it is
    // null, there is no dynamic fiber for this thread.
    if (f == nullptr) return;
    f->MarkFinished();
    if (absl::GetFlag(FLAGS_fiber_cancel_dynamic_on_thread_exit) &&
        !f->joinable_.HasBeenNotified()) [[unlikely]] {
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
  // We immediately orphan, MarkFinished() will release the reference above.
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
std::unique_ptr<Fiber> FiberHelpers::CreateChildFiber(
    Fiber* parent, absl::AnyInvocable<void() &&> invocable) {
  Fiber* const fiber = new Fiber{
      Fiber::Unstarted{},
      parent->options(),
      std::move(invocable),
      parent,
  };

  fiber->Start();
  return absl::WrapUnique(fiber);
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
    fiber->Start();
    Detach(absl::WrapUnique(fiber));
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
        // For dynamic fibers, we use a plain copy of the current context.
        // Dynamic fibers have no executing code associated with it, so we just
        // use the plain copy constructor which emits no causality events.
        // See `ThreadInitType` constructor in `base::Context` for more info.
        return type_ == fiber::FIBER_TYPE_NORMAL
                   ? base::Context(base::Context::kThread, options.name())
                   : base::CurrentContext();
      }()),
      options_(options),
      min_expiry_deadline_(
          std::min(context_.deadline(), parent_->min_expiry_deadline_)),
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
      context_(tree_options.consume_context(this)),
      options_(tree_options.fiber_options()),
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

  base::WithContext with_context(std::move(context_));
  std::move(work_)();
  // Immediately free the storage.  This matches the behavior of NewCallback
  // and allows ref-counting arguments to be scoped to the fiber's body (e.g.
  // auto-closing channels).
  work_ = nullptr;

  DCHECK(*ptr == this);
  *ptr = old;
  DVLOG(2) << "F " << this << " finished";

  PerDomainCounters().num_active_fibers.fetch_sub(1, std::memory_order_relaxed);

  // A call to fiber_stats::Get() pulls fibers out of schedulables; but
  // after this call the fiber can be destroyed, so we "detach" this fiber
  // from its schedulable (attached in Fiber::Start()).
  base::scheduling::InternalDetachFiber(this);

  // Reset current fiber name as this thread may be recycled, and we don't want
  // misattribution until someone else sets an explicit name for this thread.
  // This also alleviates lifetime concerns for the name itself.
  thread::InternalSetCurrentFiberName("");

  if (auto [clean_up, joinable] = MarkFinished(); clean_up != nullptr) {
    // This is a self-joining fiber, but we may need to wait for children.
    if (joinable) {
      MarkJoined();
    } else {
      InternalJoin();
    }
    clean_up(this);
  }
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

// Update *this to a FINISHED state, preparing it to be Join()-ed (and notifying
// any waiters) when applicable. If we've been detached, returns a function that
// should be called with `this`.
//
// REQUIRES: *this has not already been marked finished.
std::pair<Fiber::CleanupFn*, bool> Fiber::MarkFinished() {
  absl::MutexLock l(mu_);
  DCHECK_EQ(state_, RUNNING);
  {
    internal::PerDomainCounters& counters = PerDomainCounters();
    counters.fibers_finished.fetch_add(1, std::memory_order_relaxed);
    counters.num_fibers.fetch_sub(1, std::memory_order_relaxed);
  }

  state_ = FINISHED;
  Fiber::CleanupFn* cleanup = detached_cleanup_.load(std::memory_order_relaxed);

  if (children_.empty()) {
    // This fiber has become joinable as it, and all children, are finished.
    // We only signal attached fibers as detached fibers are self-joining.
    if (cleanup == nullptr) {
      joinable_.Notify();
    }
    return {cleanup, true};
  }
  return {cleanup, false};
}

// Record that the Join() requirement has been satisfied.  In the case of a
// detached fiber this may have been internally generated.
//
// If *this was a child fiber it will be removed from its parent's active
// children.
//
// No-op if *this has already been Join()-ed.
void Fiber::MarkJoined() {
  DCHECK(internal::FiberHelpers::IsFiberDetached(this) ||
         joinable_.HasBeenNotified());

  bool has_parent;
  {
    absl::MutexLock l(mu_);
    DCHECK(children_.empty());
    if (state_ == JOINED) return;  // Already joined.
    DCHECK_EQ(state_, FINISHED);
    DVLOG(2) << "F " << this << " joined";
    state_ = JOINED;
    has_parent = parent_ != nullptr;
  }
  if (has_parent) {
    absl::MutexLock l(parent_->mu_);
    parent_->children_.erase(this);
    if (parent_->children_.empty() && parent_->state_ == FINISHED) {
      parent_->joinable_.Notify();
    }
  } else {
    // We were joined and have no parent. All of our children must already be
    // joined. Release our ref on the scheduler.
    tree_scheduler_.Unref();
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
  fiber.mu_.lock();

  // Check our requirement that the fiber not already be detached. It doesn't
  // make sense to do it twice, because we'd have two functions that want to
  // clean it up afterward.
  DCHECK_EQ(fiber.detached_cleanup_.load(std::memory_order_relaxed), nullptr)
      << "fiber already detached";

  // Special case: if the fiber has already finished then we need to join it and
  // clean it up here, because MarkFinished has already run and won't do the
  // cleanup logic for us.
  if (ABSL_PREDICT_FALSE(fiber.state_ == Fiber::FINISHED)) {
    fiber.mu_.unlock();

    // Call InternalJoin to bypass the check that the current fiber is a parent.
    // It may be a non-parent ancestor in the case of bundle fibers.
    fiber.InternalJoin();

    // It's our job to clean up the fiber.
    return clean_up(&fiber);
  }

  fiber.detached_cleanup_.store(&clean_up, std::memory_order_relaxed);
  fiber.mu_.unlock();
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

void DynamicFiber::Finish() {
  if (!finished_) {
    finished_ = true;
    fiber_.MarkFinished();
  }
}

}  // namespace internal

DistinctFiberScope::DistinctFiberScope(const FiberOptions& options)
    : outer_(*GetPerThreadFiberPtr()),
      inner_(TreeOptions{}.set_fiber_options(options)) {
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
