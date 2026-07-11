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

// A Task object is used to coordinate an asynchronous activity.
// It allows the creator of the task to supply a callback that will
// execute when the task completes.  It also provides support for
// cancelling the asynchronous activity.
//
// A module that supports asynchronous calls will typically have
// methods of the form:
//
//     Method(const Arg& arg, Result* result, util::Task* task);
//
// Calling such a method will initiate the asynchronous activity.
// The caller must ensure that the arg, result, and task objects are valid
// until the task completes and is responsible for the memory
// management of these objects.  Moreover the arg and result should not
// be modified by the caller during the execution of the task.
//
// The callee performs the task, and when complete calls
// Task::Return().  The callee may or may not expedite
// the completion of the task if the task is cancelled.
//
// To assist with the implementation of the callee, the task
// object provides support for memory management.   The callee
// will often create state used to perform the task and must
// ensure this state sticks around until it can no longer be accessed;
// this can be difficult to do explicitly due to the nature
// of cancellation requests.  See DeleteWhenDone and UnrefWhenDone
// for details.
//
// A task is associated with a thread::Executor on which it runs its
// callbacks.  That executor is often a long-lived executor like
// thread::DefaultQueue() or thread::SingletonInlineExecutor().  It
// is also safe to use short-lived executors.  The task guarantees
// not to access its executor after the done callback has started
// running.  It is safe to delete the executor from the done callback
// if that executor tolerates deletion even when it has closures that
// are enqueued or running.  This might be the case if the executor
// is a wrapper around an underlying long-lived executor.
//
// The following examples assume we have an RPC service defined that sends
// a Request and receives a Response, with variables to hold their values.
//
// -----------------------------------------------------------------------
//
// Example: A simple RPC exchange.  (This example does not illustrate any
// significant functionality beyond what is already provided by Stubby.
// It is given just to demonstrate how to use Task.)
//
//    Task* task = new Task(
//        [](Task* task) {
//          // The initiator of a task is responsible for deleting the task.
//          // This deletes the task when task_ptr goes out of scope
//          auto task_ptr = absl::WrapUnique(task);
//          if (!task->status().ok()) {
//            LOG(ERROR) << "S failed: " << task->status().error_message();
//          } else {
//            // Response is valid
//          }
//        },
//        thread::DefaultQueue());
//
//    stub->Call(rpc, request, &response,
//               [rpc, task]() {
//                 // Application-specific conversion from rpc to Status
//                 Status status = StatusFromRpc(rpc);
//                 // When the RPC completes, it must call task->Return() to
//                 // finalize the task.
//                 task->Return(status);
//               });
//
// Once Task::Return() has been called, the Task implementation
// arranges to call the callback supplied when the task was created.
// This function can retrieve the status value passed to Task::Return.
//
// -----------------------------------------------------------------------
//
// Example: An asynchronous activity that repeatedly does a Stubby RPC
// until it does not get a deadline error:
//
//    void RPCLoop(RPC* rpc, const Request& req, Response* res, Task* task) {
//      stub->Call(rpc, req, res, [rpc, req, res, task]() {
//        if (rpc::status::IsDeadlineExceeded(rpc->util_status())) {
//          RPCLoop(rpc, req, res, task);
//        } else {
//          task->Return();
//        }
//      });
//
// -----------------------------------------------------------------------
//
// Example: a client of the RPCLoop routine:
//
//    void Send() {
//      Task* task = new Task([](Task* task) {
//          auto task_deleter = absl::WrapUnique(task);
//          ...
//        },
//        thread::DefaultQueue());
//      RPCLoop(..., task);
//    }
//
// -----------------------------------------------------------------------
//
// Example: Change RPCLoop() so it stops quickly when the task is cancelled:
// We add a callback to catch the task cancellation and cancel the rpc there.
// Note that StartCancel() must occur after the RPC has started.
//
//    void RPCLoop(RPC* rpc, const Request& req, Response* res, Task* task) {
//      TaskHold hold(task);
//      stub->Call(rpc, req, res, [rpc, req, res, task]() {
//        if (rpc::status::IsDeadlineExceeded(rpc->util_status())) {
//          RPCLoop(rpc, req, res, task);
//        } else {
//          task->Return();
//        }
//      });
//      task->WhenCancelled(absl::bind_front(&RPC::StartCancel, rpc));
//    }
//
// -----------------------------------------------------------------------
//
// A SyncTask helper class for synchronous callers:
//
//      SyncTask sync;
//      RPCLoop(..., sync.task());
//      sync.Wait();
//      if (sync.status().ok()) {
//        ...
//      }
//
// -----------------------------------------------------------------------
//
// Example: Cancel a task after a given deadline has expired.
//
// Setup a child task to complete after the specified deadline.
//
//    util::Task* deadline_task = task->AddChild(
//        [parent_task = task](Task* deadline_task) {
//            if (deadline_task->status().ok()) {
//              parent_task->Cancel();
//            }
//          });
//    util::SleepUntil(deadline, deadline_task);
//
// If the status of the child deadline task is ok, the deadline
// was reached and the parent task should be canceled.  If the status
// is not ok, then the parent has reached the PREPARED state and
// has canceled the child deadline task.
//
// -----------------------------------------------------------------------
//
// Example: the task and its children are allocated on a
// google::protobuf::Arena.
//
//    google::protobuf::Arena arena;
//    util::Task* task =
//        google::protobuf::Arena::Create<util::Task>(&arena, callback,
//        executor, &arena);
//    util::Task* child = task->AddChild(child_callback);
//    util::Task* child2 = child->AddChild(another_callback);
//
//  In this example, task, child, and child2 are all allocated on the same
//  arena.

#ifndef THIRD_PARTY_GLOOP_UTIL_TASK_TASK_H_
#define THIRD_PARTY_GLOOP_UTIL_TASK_TASK_H_

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "gloop/base/context.h"
#include "gloop/base/spinlock.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/transitional.h"
#include "google/protobuf/arena.h"

ABSL_DECLARE_FLAG(bool, task_legacy_capture_context);

class TraceContext;

namespace thread {
class Executor;
}

namespace util {

class ReferenceCounted;

class Task {
 public:
  // --- Explanation of Task states ---
  //
  // A Task goes through the following sequence of states:
  //      ACTIVE       -- this is the initial state
  //      PREPARED     -- task now has a status (possibly an error-status)
  //      DONE         -- the task callback can now run
  //
  // Knowledge of these states is very useful when interpreting the
  // specifications of various Task methods, and reasoning about the
  // completion of asynchronous activities and the execution of
  // callbacks.
  //
  // PREPARED: A task enters this state on the first Return() call.
  //
  // DONE: A task enters this state when all of the following conditions
  // are met:
  //    (a) the task was in the PREPARED state
  //    (b) there are no remaining holds on the task (see AddHold/RemoveHold)
  //    (c) all cancellation callbacks have finished running
  //    (d) all child task callbacks have finished running
  //
  // The general life of a task is:
  // ACTIVE: Task is created by caller with callback.
  // ACTIVE: Caller sets executor
  // ACTIVE: Caller passes task to callee.
  // ACTIVE: Callee creates local state and arranges for it
  //         to be deleted via DeleteWhenDone() or UnrefWhenDone()
  // ACTIVE: Callee registers interest in cancellation requests
  //         via WhenCancelled().
  // ACTIVE: Callee uses AddHold(), RemoveHold() to avoid
  //         early completion of a task.
  // ACTIVE: Callee initiates work to complete task, returning
  //         flow control back to caller.
  // ACTIVE: Task enters PREPARED state when callee invokes Return()
  // PREPARED: Prepared callbacks are invoked.
  // PREPARED: When no holds remain and no prepared callbacks are left,
  //         task enters DONE state.
  // DONE:   Task callback and deletion callbacks are invoked.
  // DONE:   Caller deletes the task - often in the callback.
  //
  // During the above, the caller may invoke Cancel() on the task: if
  // the task is ACTIVE or PREPARED, it places a hold on the task and
  // schedules any cancellation callbacks registered by the callee. Such
  // callbacks can attempt to expedite the completion of the task.
  //
  // -------------------------------------------------------------------
  // Methods used by the initiator of a task.  i.e. the caller.
  // -------------------------------------------------------------------

#ifndef SWIG
  using Callback ABSL_DEPRECATE_AND_INLINE() =
      ::util::functional::CallbackFunctor<Task*>;
#endif  // SWIG

  // Creates a new Task that will invoke "callback(this)" when it is DONE,
  // using the specified executor. If unsure what executor to use, a
  // reasonable default is thread::Executor::DefaultExecutor(). The current
  // base::Context is captured by this constructor, so the callback will run
  // under that Context. If provided, the arena will be used to allocate this
  // task's Children, and transitively their children.
#ifndef SWIG
  Task(absl::AnyInvocable<void(Task*) &&> callback, thread::Executor* executor,
       google::protobuf::Arena* arena = nullptr,
       perftools::tracing::StringRef label =
           perftools::tracing::TraceSourceLocation::current());
#endif  // SWIG

  // REQUIRES: task is in the DONE state.
  // Tasks can be deleted in their callback.
  ~Task();

  google::protobuf::Arena* arena() const { return arena_; }

  // Change the executor on which the task callbacks are run.
  // Note: This method is not thread safe and should be called soon after the
  // task creation.
  void set_executor(thread::Executor* executor);

  // Returns the current executor in which the callbacks will
  // be executed.
  thread::Executor* executor() const { return executor_; }

  // Specify whether the done callback can be called inline when the task
  // becomes done, i.e. invoke it directly during a Return() or RemoveHold()
  // call.  The default is false.  Setting to true is intended for callbacks
  // that are trival in nature - such as the SyncTask callback - where it is
  // clear that deadlock will not result.  Setting this true does not effect
  // other task callbacks, such as the callback passed to WhenCancelled(), or
  // the destructors registered by DeleteWhenDone().
  // Note: the done callback may still be called on the task executor in certain
  // situation.
  // Note: This method is not thread safe and should be called soon after the
  // task creation.
  void set_inline_done_callback(bool inline_done_callback);

  // Returns if the done callback is to be run inline rather
  // than in the tasks executor.
  bool inline_done_callback() const { return inline_done_callback_; }

  // Request that this task (and all its descendant tasks) be cancelled.
  // The asynchronous computation running on behalf of this task may notice
  // this cancellation request and cause the computation to finish early.
  //
  // Note that there is no guarantee that this task is PREPARED or DONE by the
  // time this call returns.  Furthermore, the cancellation request may be
  // completely ignored.
  void Cancel();

  // Return the status object for this task.
  // REQUIRES: Return() has been called; i.e. !IsActive()
  const absl::Status& status() const;

  // -------------------------------------------------------------------
  // Methods used by the implementer of a task.  i.e. the callee.
  // -------------------------------------------------------------------

  // If this task is not already prepared, prepares it with the specified Status
  // object and returns true.  Otherwise, does not change the task and returns
  // false.
  //
  // Note that after Return() is called, the task will (usually) asynchronously
  // reach the DONE state, and run the callback for this task, which may in turn
  // delete the task and local state used by the callee.  One must be very
  // careful with the code that follows Return, including destructors of locally
  // scoped objects.  For example, the following can be problematic:
  //
  //   MutexLock l(&mutex_);  // lock of local state
  //   if (...) {
  //     task_->Return(...);
  //     // Unlock of mutex will happen here, potentially
  //     // accessing the mutex after it has been deleted.
  //     return;
  //   }
  //
  // One can rewrite this as
  //
  //   mutex_.Lock();
  //   if (...) {
  //     mutex_.Unlock();
  //     task_->Return();
  //   }
  //
  // though this may also have problems if Return can be called concurrently in
  // another part of the code.  Another option is to use a TaskHold object to
  // ensure the task does not reach the DONE state before the lock is released.
  //
  //   TaskHold hold(task_);  // Must precede MutexLock
  //   MutexLock l(&mutex_);
  //   if (...) {
  //     task_->Return(...);
  //     return;
  //   }
  //
  bool Return();
  bool Return(absl::Status status);

  // A hold can be placed on a task, preventing it from entering the done state
  // until the hold is removed. Multiple holds can be placed on a task and each
  // must be removed before the task can complete.  Holds are used to control
  // concurrency in the callee. For example, before invoking a cancellation
  // callback, the task adds a hold to itself. This ensures the task does not
  // reach the DONE state and potentially be deallocated before the callback has
  // a chance to run. Holds can also be used to avoid local race conditions
  // such as in the example with MutexLock above.
  void AddHold();
  void RemoveHold();

  // More efficient form for adding "N" holds. N must be non-negative.
  void AddMultipleHolds(int N);

  // --- State accessors ---

  // Returns true iff this->Return() has not been called, i.e.,
  // the task is in the ACTIVE state.
  // Once IsActive() returns false, it always returns false.
  bool IsActive() const;

  // Returns true iff this task state has reached the DONE state.
  // Once IsDone() returns true, it always returns true.
  bool IsDone() const;

  // Sets the Context bound to the task callback.
  void set_context(const base::Context& ctx);
#ifndef SWIG
  void set_context(base::Context&& ctx);
#endif

  // Returns the Context bound to the task callback.
  // REQUIRES: !IsDone()
  base::Context& context();
  const base::Context& context() const;

  // --- Cancellation ---

  // Returns true iff this task received a Cancel() request while not DONE.
  // Even if true, cancellation callbacks are not guaranteed to run.
  bool CancelRequested() const;

  // Arranges to invoke the specified callback the first time
  // this->Cancel() is invoked.  If this->Cancel() is never called,
  // this callback will be deleted without being executed.  Therefore
  // this callback may run zero or one times, and therefore should not
  // be solely responsible for any memory management.
  //
  // It is okay to call WhenCancelled() more than once on the same task.
  // All of the supplied callbacks will be eventually executed, in
  // some arbitrary order, on executor(), perhaps concurrently
  // with each other.
  //
  // If this method is called after the task has been cancelled, the
  // callback will run asynchronously some time in the near future.
  // If this method is called after the task is PREPARED, the callback
  // will be deleted without running.
  //
  // All cancellation callbacks will complete before the task
  // enters the DONE state.  In effect, cancellation callbacks
  // place a hold on the task before invoking the callback.
  //
  // The task object is guaranteed not to reach the DONE state while
  // the cancellation callbacks are being run.  However, one should
  // expect the task object and any state associated with the task to
  // be deleted soon after the callback returns.
#ifndef SWIG
  void WhenCancelled(absl::AnyInvocable<void() &&> cancel_callback);
#endif  // SWIG

  // --- Preparation ---

  // Arranges to invoke the specified callback when the task reaches
  // the prepared state.
  //
  // It is okay to call WhenPrepared() more than once on the same task.
  // All of the supplied callbacks will be eventually executed, in
  // some arbitrary order, on executor(), perhaps concurrently
  // with each other.
  //
  // If this method is called after the task has been prepared, the
  // callback will run asynchronously some time in the near future. It
  // is a fatal error to call this method after the task is DONE.
  //
  // All prepared callbacks will complete before the task
  // enters the DONE state.  In effect, prepared callbacks
  // place a hold on the task before invoking the callback.
  //
  // The task object is guaranteed not to reach the DONE state while
  // the prepared callbacks are being run.  However, one should expect
  // the task object and any state associated with the task to be
  // deleted soon after the callback returns.
  //
  // REQUIRES: !IsDone()
#ifndef SWIG
  void WhenPrepared(absl::AnyInvocable<void() &&> prepared_callback);
#endif  // SWIG

  // --- Children ---

  // Returns a task with the specified callback.  The returned task is
  // considered a child of this task.  The child task object is the
  // property of the parent task and will be automatically deleted
  // some time after callback has finished running.  The child task inherits
  // the parent's executor.  The child task will be automatically
  // cancelled whenever the parent is cancelled or prepared.  The
  // parent task does not leave the prepared state until the child
  // task's callback has finished running.  The current base::Context
  // is captured by this constructor, so the callback will run under
  // that Context. The callback is called with a pointer to the child task
  // object.
#ifndef SWIG
  util::Task* AddChild(absl::AnyInvocable<void(Task*) &&> callback);
#endif  // SWIG

  // Variant of AddChild() that runs child related callbacks in
  // a specified executor, instead of in the parent task's executor.
  // If the passed executor is nullptr, the parent task's executor is used.
#ifndef SWIG
  util::Task* AddChildWithExecutor(absl::AnyInvocable<void(Task*) &&> callback,
                                   thread::Executor*);
#endif  // SWIG

  // --- Memory management ---

  // Arrange to delete "x", unref "x", or call the supplied cleanup function
  // when this task enters the DONE state.
  // The destructors/unrefs/cleanup-functions will be called in an arbitrary
  // order and potentially concurrently with the task completion callback, and
  // therefore this mechanism should only be used to delete state that is not
  // visible to the callback; in other words, state used by the callee, not the
  // initiator of the task.
  //
  // REQUIRES: !this->isDone()
  // REQUIRES: x != this

  // DeleteWhenDone returns the raw-pointer form of `x`. This allows to write:
  //   Foo& my_foo = *task.DeleteWhenDone(std::make_unique<Foo>(...));
  template <class T>
  T* DeleteWhenDone(std::unique_ptr<T> x);
  template <class T>
  T* DeleteWhenDone(T* x);
#ifndef SWIG
  template <class Cbf, typename = std::enable_if_t<
                           ::util::functional::internal::
                               IsResultCallbackFunctor<std::decay_t<Cbf>>>>
  ABSL_DEPRECATE_AND_INLINE()
  auto&& DeleteWhenDone(Cbf&& x) {
    return std::forward<Cbf>(x);
  }
#endif  // SWIG

  void UnrefWhenDone(util::ReferenceCounted* x);
  void CleanupWhenDone(void (*cleanup_function)(const void*),
                       const void* object);

  // ---- Deprecated ---

  // Create a new Task that will invoke "callback(this)" when it is DONE.
  //
  // ** DEPRECATED **
  // The default constructor would use thread::Executor::DefaultExecutor()
  // which is often a bad choice, especially given a parent task whose executor
  // should be inherited. Removing this constructor forces people to think
  // about what executor is appropriate.
#ifndef SWIG
  ABSL_DEPRECATED("Use the constructor with an explicit executor.")
  explicit Task(absl::AnyInvocable<void(Task*) &&> callback,
                perftools::tracing::StringRef label =
                    perftools::tracing::TraceSourceLocation::current());

  // This type is neither copyable nor movable.
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

#endif  // SWIG

 private:
  // Possible states
  enum State { ACTIVE = 0, PREPARED = 1, DONE = 2 };

  // Structure kept per child task
  struct Child;

  // Doubly linked list of children
  class ChildList {
   private:
    Child* list_;  // nullptr if empty
   public:
    ChildList();
    void Add(Child* child);
    void Remove(Child* child);
    Child* first() const { return list_; }
  };

  // Inner classes are not supported by SWIG.
#ifndef SWIG
  // Locking controlled by Task::lock_.
  // Contains a bunch of callbacks for execution.
  class CallbackHolder {
   public:
    inline CallbackHolder() : running_(false), callbacks_() {}
    CallbackHolder(const CallbackHolder&) = delete;
    CallbackHolder& operator=(const CallbackHolder&) = delete;

    void Insert(absl::AnyInvocable<void() &&> c) {
      callbacks_.emplace_back(
          util::functional::transitional::MaybeWithCurrentContext(
              absl::GetFlag(FLAGS_task_legacy_capture_context), std::move(c)));
    }

    bool IsEmpty() const { return callbacks_.empty(); }

    // Clears the list of callbacks without running them.
    void Clear();

    // Clears the list of callbacks and executes them.
    // Must be invoked with *lock held.
    // The annotation is required to make 'annotalysis  util/task/...' pass:
    // util/task/task_force.cc:24: error: Reading variable 'next_' requires
    // lock 'task_force_->lock_'
    // "type" is only used for debugging output
    void ExecuteCallbacks(SpinLock* lock, const char* type)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock);

    // Bit used to indicate (for the caller's purposes) whether we are
    // going to run callbacks.  (We cannot just use the emptiness of
    // the CallbackHolder, because ExecuteCallbacks()
    // releases/reacquires the lock while running callbacks.)
    //
    // The caller is responsible for the protocol:
    //    acquire Task lock
    //      SetRunning(true);
    //      ExecuteCallbacks(); // maybe diff. thread, releases/reacquires lock
    //      SetRunning(false);
    //    release Task lock
    bool IsRunning() const { return running_; }
    void SetRunning(bool running) { running_ = running; }

   private:
    bool running_;
    absl::InlinedVector<absl::AnyInvocable<void() &&>, 2> callbacks_;
  };

  bool ReturnActive(absl::Status status) ABSL_UNLOCK_FUNCTION(lock_);
#endif  // SWIG

  base::Context context_;
  absl::Status status_;
#ifndef SWIG
  absl::AnyInvocable<void(Task*) &&> done_callback_;
#endif  // SWIG
  CallbackHolder cancel_callbacks_;
  CallbackHolder prepared_callbacks_;
  // Protect the state of the task
  mutable SpinLock lock_;

  int8_t state_;             // holds a State value
  bool cancelled_;           // Cancel() has been called.
  bool cancelled_children_;  // Children have been cancelled?
  bool inline_done_callback_;
  int holds_;

  ChildList children_;
  // Space for a single deleter inline.
  absl::InlinedVector<std::unique_ptr<const void, void (*)(const void*)>, 1>
      deleters_;
  thread::Executor* executor_;
  google::protobuf::Arena* const arena_;

  void AddDone();
  void RunDone();
  bool Invariants() const;
  bool ScheduleCancelCallbacks();
  bool SchedulePreparedCallbacks();
  void ExecuteCallbacks(CallbackHolder* holder, const char* type);
  void RemoveHoldAndUnlock() ABSL_UNLOCK_FUNCTION(lock_);
  bool NeedToCancelChildren() const;
  void CancelChildren() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void RunChildCallback(Child* child);

  // If the transition to DONE is possible, makes the transition and
  // returns true; otherwise, returns false.
  bool TryDoneTransition();

  // Special deleter routine for objects of type T
  template <class T>
  static void DefaultDelete(const void* x);

  // Special delete routine that unrefs an object
  static void UnrefObject(const void* x);

  // L >= lock_
  std::string DebugStringLocked() const;
};

// Adapts a callback that returns a status to a task.
//
// This can be used to adapt `ABSL_RETURN_IF_ERROR` macros for use with `Task`.
// For example:
//
//   void ProcessWidgets(const Widget& w, Task* t) {
//     ABSL_RETURN_IF_ERROR(PrepareWidget(w)).With(TaskReturn(t));
//     ABSL_RETURN_IF_ERROR(PackageWidget(w)).With(TaskReturn(t));
//     ABSL_RETURN_IF_ERROR(ShipWidget(w)).With(TaskReturn(t));
//     t->Return();
//
class TaskReturn {
 public:
  explicit TaskReturn(Task* t) : task_(t) {}
  void operator()(absl::Status status) const {
    task_->Return(std::move(status));
  }

 private:
  Task* task_;
};

#ifndef SWIG

// TaskHold object that Holds up a task for its lifetime.
// Moveable but not copyable.
class ABSL_MUST_USE_RESULT TaskHold {
 public:
  inline explicit TaskHold(Task* t) : task_(t) { t->AddHold(); }
  TaskHold(const TaskHold&) = delete;
  TaskHold& operator=(const TaskHold&) = delete;
  TaskHold(TaskHold&& t) noexcept : task_(std::exchange(t.task_, nullptr)) {}
  TaskHold& operator=(TaskHold&& t) noexcept {
    if (this == &t) return *this;
    if (task_ != nullptr) task_->RemoveHold();
    task_ = std::exchange(t.task_, nullptr);
    return *this;
  }
  ~TaskHold() {
    if (task_ != nullptr) task_->RemoveHold();
  }

 private:
  Task* task_;
};

// ----------------------------------------------------------------------
// Implementation details follow.  Please ignore.

// Since we will take this function's address, do not mark it inline
template <class T>
void Task::DefaultDelete(const void* x) {
  delete static_cast<const T*>(x);
}

template <class T>
inline T* Task::DeleteWhenDone(T* x) {
  CleanupWhenDone(&Task::DefaultDelete<T>, x);
  return x;
}

template <class T>
inline T* Task::DeleteWhenDone(std::unique_ptr<T> x) {
  T* ptr = x.release();
  CleanupWhenDone(&Task::DefaultDelete<T>, ptr);
  return ptr;
}

inline void Task::UnrefWhenDone(util::ReferenceCounted* x) {
  CleanupWhenDone(&Task::UnrefObject, x);
}

inline base::Context& Task::context() {
  DCHECK(!IsDone());
  return context_;
}

inline const base::Context& Task::context() const {
  DCHECK(!IsDone());
  return context_;
}

#endif  // SWIG

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TASK_TASK_H_
