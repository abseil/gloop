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

#include "gloop/util/task/task.h"

#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/inlined_vector.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/base/spinlock.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/thread/executor.h"
#include "gloop/util/functional/with_context.h"
#include "gloop/util/random/shared_bit_gen.h"
#include "gloop/util/refcount/reference_counted.h"
#include "google/protobuf/arena.h"

template class Callback1<::util::Task*>;

// TODO: Remove this flag.
ABSL_FLAG(
    bool, task_legacy_capture_context, true,
    "Whether to capture the ambient Context for invocables added to Task");

namespace util {

// --------------------------------------------------------
// Nested classes

// Structure kept per child task
struct Task::Child final {
  int refs_;     // +1 for child task; +1 for pending cancellation
  Child* next_;  // nullptr or points to next sibling
  Child* prev_;  // nullptr or points to previous sibling
  Task* parent_;
  absl::AnyInvocable<void(Task*) &&> user_cb_;
  Task child_task_;

  explicit Child(thread::Executor* executor, Task* parent)
      : parent_(parent),
        child_task_([this](util::Task* task) { Run(task); }, executor,
                    parent->arena()) {}
  void Ref() { ++refs_; }
  void Unref() {
    DCHECK_GT(refs_, 0);
    --refs_;
    if (refs_ == 0) {
      // Faster than letting Arena figure out ownership - we know whether
      // that's the case by-construction.
      if (!parent_->arena()) {
        delete this;
      }
    }
  }
  void Run(util::Task* t) {
    DCHECK(t == &child_task_);
    parent_->RunChildCallback(this);
  }
};

inline Task::ChildList::ChildList() : list_(nullptr) {}

inline void Task::ChildList::Add(Child* child) {
  child->prev_ = nullptr;
  child->next_ = list_;
  if (list_ != nullptr) {
    list_->prev_ = child;
  }
  list_ = child;
}

inline void Task::ChildList::Remove(Child* child) {
  Child* next = child->next_;
  if (child->prev_ != nullptr) {
    child->prev_->next_ = next;
  } else {
    list_ = next;
  }
  if (next != nullptr) {
    next->prev_ = child->prev_;
  }
}

void Task::CallbackHolder::Clear() { callbacks_.clear(); }

void Task::CallbackHolder::ExecuteCallbacks(SpinLock* lock, const char* type) {
  CHECK(running_);
  while (!callbacks_.empty()) {
    absl::AnyInvocable<void() &&> callback = std::move(callbacks_.back());
    callbacks_.pop_back();
    lock->unlock();
    std::move(callback)();
    // Explicitly deallocate `callback` to immediately release any captured
    // resources.
    callback = nullptr;
    lock->lock();
  }
}

// -------------------------------------------------------
// Task
Task::Task(absl::AnyInvocable<void(Task*) &&> callback,
           perftools::tracing::StringRef label)
    : Task(std::move(callback), thread::Executor::DefaultExecutor(), nullptr,
           label) {}

Task::Task(absl::AnyInvocable<void(Task*) &&> callback,
           thread::Executor* executor, google::protobuf::Arena* arena,
           perftools::tracing::StringRef label)
    : context_(base::Context::kThread, label),
      status_(),
      done_callback_(std::move(callback)),
      cancel_callbacks_(),
      prepared_callbacks_(),
      state_(ACTIVE),
      cancelled_(false),
      cancelled_children_(false),
      inline_done_callback_(false),
      holds_(0),
      children_(),
      executor_(executor),
      arena_(arena) {
  DCHECK(executor_ != nullptr);
  DCHECK(done_callback_ != nullptr);
  DCHECK(Invariants());
}

Task::~Task() {
  // We cast state_ to int here so that numbers are printed in human-readable
  // form instead of being interpreted as chars.
  CHECK_EQ(static_cast<int>(state_), DONE);
  CHECK(done_callback_ == nullptr);
  CHECK(children_.first() == nullptr);
  DCHECK(Invariants());
}

void Task::CleanupWhenDone(void (*cleanup_function)(const void*),
                           const void* object) {
  CHECK_NE(object, this)
      << "This is an improper use of the `WhenDone` series of functions for "
         "util::Task. This is inherently racy and may destroy the task before "
         "the done function is called with a pointer to the task (now a "
         "use-after-free to access). Instead, delete the task in the done "
         "function.";
  SpinLockHolder l(lock_);
  CHECK_LE(static_cast<int>(state_), PREPARED);
  deleters_.push_back({object, cleanup_function});
}

bool Task::IsActive() const {
  SpinLockHolder l(lock_);
  return state_ == ACTIVE;
}

bool Task::IsDone() const {
  SpinLockHolder l(lock_);
  return state_ == DONE;
}

void Task::set_context(const base::Context& ctx) { context_ = ctx; }

#ifndef SWIG
void Task::set_context(base::Context&& ctx) { context_ = std::move(ctx); }
#endif

const absl::Status& Task::status() const {
  // Since we require that the state is prepared, the caller must
  // have synchronized with the Return() call somehow.  So no need
  // to grab the lock.
  DCHECK(!IsActive()) << " fetching status before calling Return()";
  return status_;
}

void Task::set_executor(thread::Executor* executor) {
  // We assume the method is called early in the
  // life of a task, so we don't grab the lock.
  executor_ = executor;
}

void Task::set_inline_done_callback(bool inline_done_callback) {
  // We assume the method is called early in the
  // life of a task, so we don't grab the lock.
  inline_done_callback_ = inline_done_callback;
}

bool Task::Return() { return Return(absl::OkStatus()); }

bool Task::Return(absl::Status status) {
  lock_.lock();
  if (state_ >= PREPARED) {
    // No effect on multiple calls
    lock_.unlock();
    return false;
  }
  return ReturnActive(std::move(status));
}

// Prepare the Status object and returns true.
//
// We always return true to keep the same return type as its callers
// (Task::Return) to enable tail-call-optimization.
//
// The task must be in active state and lock_ must be acquired before calling
// this function. The lock will be released before the function returns.
bool Task::ReturnActive(absl::Status status) {
  status_ = std::move(status);
  state_ = PREPARED;
  if (!cancel_callbacks_.IsRunning()) {
    // There is no need to fire cancel callbacks since we have reached
    // the PREPARED state: cancellations are now moot.  If Cancel()
    // has already initiated the running of the cancel callbacks, we
    // let them finish, however.
    cancel_callbacks_.Clear();
  }

  if (SchedulePreparedCallbacks()) {
    DCHECK(Invariants());
    lock_.unlock();

    executor_->Schedule(absl::bind_front(&Task::ExecuteCallbacks, this,
                                         &prepared_callbacks_,
                                         "prepared in Return()"));
    return true;
  }

  const bool done = TryDoneTransition();
  DCHECK(Invariants());
  lock_.unlock();

  if (done) {
    AddDone();
  }
  return true;
}

void Task::AddHold() {
  SpinLockHolder l(lock_);
  CHECK(state_ <= PREPARED);
  holds_++;
}

void Task::AddMultipleHolds(int N) {
  SpinLockHolder l(lock_);
  CHECK(state_ <= PREPARED);
  DCHECK_GE(N, 0);
  holds_ += N;
}

void Task::RemoveHold() {
  lock_.lock();
  RemoveHoldAndUnlock();
}

void Task::RemoveHoldAndUnlock() {
  CHECK(state_ <= PREPARED);
  CHECK_GT(holds_, 0);
  holds_--;
  const bool done = TryDoneTransition();
  DCHECK(Invariants());
  lock_.unlock();

  if (done) {
    AddDone();
  }
}

void Task::Cancel() {
  bool run_cancel_callbacks = false;
  {
    SpinLockHolder l(lock_);
    if (state_ == DONE) return;  // No effect once DONE
    if (cancelled_) return;      // No effect with multiple calls

    cancelled_ = true;
    run_cancel_callbacks = ScheduleCancelCallbacks();
    DCHECK(Invariants());
  }
  if (run_cancel_callbacks) {
    executor_->Schedule(absl::bind_front(&Task::ExecuteCallbacks, this,
                                         &cancel_callbacks_,
                                         "cancel in Cancel()"));
  }
}

bool Task::CancelRequested() const {
  SpinLockHolder l(lock_);
  return cancelled_;
}

void Task::WhenCancelled(absl::AnyInvocable<void() &&> cancel_callback) {
  bool run_cancel_callbacks = false;
  {
    SpinLockHolder l(lock_);
    if (state_ != ACTIVE) {
      return;
    }

    cancel_callbacks_.Insert(std::move(cancel_callback));
    run_cancel_callbacks = ScheduleCancelCallbacks();
    DCHECK(Invariants());
  }

  if (run_cancel_callbacks) {
    executor_->Schedule([this] {
      ExecuteCallbacks(&cancel_callbacks_, "cancel in WhenCancelled()");
    });
  }
}

void Task::WhenPrepared(absl::AnyInvocable<void() &&> prepared_callback) {
  bool run_prepared_callbacks = false;
  {
    SpinLockHolder l(lock_);
    CHECK_NE(static_cast<int>(state_), DONE);

    prepared_callbacks_.Insert(std::move(prepared_callback));
    run_prepared_callbacks = SchedulePreparedCallbacks();
    DCHECK(Invariants());
  }

  if (run_prepared_callbacks) {
    executor_->Schedule([this] {
      ExecuteCallbacks(&prepared_callbacks_, "prepared in WhenPrepared()");
    });
  }
}

// L >= lock_
bool Task::TryDoneTransition() {
  if (state_ == PREPARED && holds_ == 0 && !cancel_callbacks_.IsRunning() &&
      !prepared_callbacks_.IsRunning()) {
    DCHECK(prepared_callbacks_.IsEmpty());
    DCHECK(cancel_callbacks_.IsEmpty());
    DCHECK(children_.first() == nullptr);
    state_ = DONE;
    return true;
  }
  return false;
}

// L >= lock_
bool Task::NeedToCancelChildren() const {
  return !cancelled_children_ && (children_.first() != nullptr);
}

// L >= lock_
bool Task::ScheduleCancelCallbacks() {
  if (cancelled_ && !cancel_callbacks_.IsRunning() &&
      (!cancel_callbacks_.IsEmpty() || NeedToCancelChildren())) {
    cancel_callbacks_.SetRunning(true);
    // We can't do
    //   executor_->Add(NewCallback(this, &Task::ExecuteCallbacks,
    //                              &cancel_callbacks_, ""));
    // here, since executor_ might be an inline executor,
    // which could easily result in a deadlock.  Instead, the
    // caller must do it releases lock_.
    return true;
  }
  return false;
}

// L >= lock_
bool Task::SchedulePreparedCallbacks() {
  if (state_ == PREPARED && !prepared_callbacks_.IsRunning() &&
      (!prepared_callbacks_.IsEmpty() || NeedToCancelChildren())) {
    prepared_callbacks_.SetRunning(true);
    // We can't do
    //   executor_->Add(NewCallback(this, &Task::ExecuteCallbacks,
    //                              &prepared_callbacks_, ""));
    // here, since executor_ might be an inline executor,
    // which could easily result in a deadlock.  Instead, the
    // caller must do it releases lock_.
    return true;
  }
  return false;
}

// L < lock_
void Task::ExecuteCallbacks(CallbackHolder* holder, const char* type) {
  lock_.lock();
  CancelChildren();  // First since cancellation may add callbacks to holder
  holder->ExecuteCallbacks(&lock_, type);
  holder->SetRunning(false);

  const bool done = TryDoneTransition();
  DCHECK(Invariants());
  lock_.unlock();

  if (done) {
    // We are already in the correct executor, so
    // might as well run the Done callback synchronously.
    RunDone();
  }
}

void Task::CancelChildren() {
  if (cancelled_children_) {
    // No need to make multiple passes over the list of children since
    // after the task is cancelled or prepared, any new children are
    // immediately cancelled by AddChildWithExecutor().
    return;
  }
  cancelled_children_ = true;

  absl::InlinedVector<Child*, 4> to_cancel;
  for (Child* child = children_.first(); child != nullptr;
       child = child->next_) {
    child->Ref();
    to_cancel.push_back(child);
  }

  if (!to_cancel.empty()) {
    lock_.unlock();
    for (int i = 0; i < to_cancel.size(); i++) {
      to_cancel[i]->child_task_.Cancel();
    }
    lock_.lock();
    for (int i = 0; i < to_cancel.size(); i++) {
      to_cancel[i]->Unref();
    }
  }
}

Task* Task::AddChildWithExecutor(absl::AnyInvocable<void(Task*) &&> callback,
                                 thread::Executor* e) {
  Child* child = google::protobuf::Arena::Create<Child>(
      arena_, ((e != nullptr) ? e : executor_), this);

  child->refs_ = 1;

  // Child tasks wrap the user callback; propagate the current
  // Context to the outer callback.
  child->user_cb_ = util::functional::WithCurrentContext(std::move(callback));

  bool cancel_immediately = false;
  {
    SpinLockHolder l(lock_);
    CHECK_NE(static_cast<int>(state_), DONE);
    holds_++;
    children_.Add(child);
    if (cancelled_ || (state_ >= PREPARED)) {
      cancel_immediately = true;
    }
    DCHECK(Invariants());
  }

  if (cancel_immediately) {
    child->child_task_.Cancel();
  }
  return &child->child_task_;
}

Task* Task::AddChild(absl::AnyInvocable<void(Task*) &&> callback) {
  return AddChildWithExecutor(std::move(callback), nullptr);
}

void Task::RunChildCallback(Child* child) {
  // If cancellation hasn't already happened, prevent it from
  // happening in the future.  We do this before calling the user
  // callback, as the user-callback may call Return() on the parent,
  // which in turn will attempt to cancel children.  So removing the
  // child here means we will have one less child to cancel, often
  // going from one to zero.
  {
    SpinLockHolder l(lock_);
    children_.Remove(child);
  }

  // Run user callback
  std::move(child->user_cb_)(&child->child_task_);
  // Reset user_cb_ so that any resources associated with it are freed now,
  // rather than when the child is destroyed under lock with Unref().
  child->user_cb_ = nullptr;

  lock_.lock();
  DCHECK(Invariants());
  child->Unref();  // Paired with initial reference count of 1
  RemoveHoldAndUnlock();
}

void Task::AddDone() {
#ifndef NDEBUG
  // Debug mode shuffle to catch tests relying on execution order.
  absl::c_shuffle(deleters_, util_random::SharedBitGen());
#endif
  if (!inline_done_callback_) {
    executor_->Schedule([this] { RunDone(); });
    return;
  }

  // Optimization for trivial callbacks such as in SyncTask.

  // AddDone is called at most once per task once it
  // reaches the DONE state.  Moreover, once in the
  // DONE state, both the state and the delete list
  // cannot change.  These conditions allow us to
  // do the following without holding the lock.
  CHECK_EQ(static_cast<int>(state_), DONE);
  // Ensure the callback is destroyed after running even if the task lives
  // longer.
  auto callback = std::move(done_callback_);

  if (!deleters_.empty()) {
    // Perform the deletes in the task's executor to avoid any surprises that
    // may occur if we attempt to run inline here.  Since the task may go away
    // at the end of this method, we need to move deleters_.
    //
    // Move to the Context of the 'done' closure, which is probably the same
    // context 'this' was created in. This causes the deleters to be run in a
    // reasonable Context. Even though we immediately move to another executor,
    // WithContext will pick up the context and use it when executing deleters.
    executor_->Schedule(util::functional::WithContext(
        [d = std::move(deleters_)]() mutable { d.clear(); }, context_));
  }

  // Execute the user's done callback
  base::WithContext wc(std::move(context_));
  std::move(callback)(this);
  // We may have been deleted by "callback".
}

void Task::RunDone() {
  // RunDone is called exactly once per task once it
  // reaches the DONE state.  Moreover, once in the
  // DONE state, both the state and the delete list
  // cannot change.  These conditions allow us to
  // do the following without holding the lock.
  CHECK_EQ(static_cast<int>(state_), DONE);
  auto callback = std::move(done_callback_);
  // Move to the context 'this' was created in. This causes the deleters to
  // be run in a reasonable Context.
  base::WithContext wc(std::move(context_));

  // We delete the objects associated with the task before calling the user
  // callback to expose errors in the use of DeleteWhenDone.  We could do the
  // deletions after the callback, which would reduce latency a little, or do
  // the deletion in a completely separate thread.
  // TODO: figure out what to do here.
  deleters_.clear();

  // Execute the user's done callback
  std::move(callback)(this);
  // We may have been deleted by "callback".
}

void Task::UnrefObject(const void* x) {
  util::ReferenceCounted* r =
      reinterpret_cast<util::ReferenceCounted*>(const_cast<void*>(x));
  r->Unref();
}

bool Task::Invariants() const {
  // We use CHECK() here instead of DCHECK() since typically this
  // entire routine is wrapped inside a DCHECK() call.  However if
  // somebody calls CHECK(Invariants()), we should follow along
  // and crash even in NDEBUG mode.

  CHECK_GE(static_cast<int>(state_), ACTIVE);
  CHECK_LE(static_cast<int>(state_), DONE);

  if (cancelled_) {
    // Check that cancel callbacks are not pending
    if (!cancel_callbacks_.IsRunning()) {
      CHECK(cancel_callbacks_.IsEmpty()) << DebugStringLocked();
    }
  }

  switch (state_) {
    case ACTIVE:
      break;
    case PREPARED:
      if (!prepared_callbacks_.IsRunning()) {
        // Check that prepared callbacks are not pending
        CHECK(prepared_callbacks_.IsEmpty()) << DebugStringLocked();
      }
      // Check that TryDoneTransition() would return false.
      CHECK(holds_ > 0 || cancel_callbacks_.IsRunning() ||
            prepared_callbacks_.IsRunning())
          << " should be DONE " << DebugStringLocked();
      break;
    case DONE:
      CHECK(cancel_callbacks_.IsEmpty()) << DebugStringLocked();
      CHECK(!cancel_callbacks_.IsRunning()) << DebugStringLocked();
      CHECK(prepared_callbacks_.IsEmpty()) << DebugStringLocked();
      CHECK(!prepared_callbacks_.IsRunning()) << DebugStringLocked();
      CHECK(children_.first() == nullptr) << DebugStringLocked();
      CHECK_EQ(holds_, 0) << DebugStringLocked();
      break;
  }

  return true;
}

std::string Task::DebugStringLocked() const {
  DCHECK(lock_.IsHeld());
  std::string s;
  absl::StrAppendFormat(&s, "%p state=%d%s holds=%d%s%s%s%s%s", this,
                        static_cast<int>(state_),
                        (cancelled_ ? " cancelled" : ""), holds_,
                        (cancel_callbacks_.IsRunning() ? " crunning" : ""),
                        (cancel_callbacks_.IsEmpty() ? "" : " ccbs"),
                        (prepared_callbacks_.IsRunning() ? " prunning" : ""),
                        (prepared_callbacks_.IsEmpty() ? "" : " pcbs"),
                        (cancelled_children_ ? " children-cancelled" : ""));
  for (Child* c = children_.first(); c != nullptr; c = c->next_) {
    absl::StrAppendFormat(&s, " child=%p", &c->child_task_);
  }
  return s;
}

}  // namespace util
