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

// An abstract object that can execute closures.
// Some concrete implementations are ThreadPools and SelectServers.
// Implementations of Executor must be thread-safe.
// Implementations must ensure that any reads/writes done by the caller before
// calling Schedule() are serialized before any reads/writes done inside the
// function.
//
// On base::Context
// ----------------
// Callbacks will use the context active when they are put on the executor,
// i.e. when Schedule*() is called.

#ifndef THIRD_PARTY_GLOOP_THREAD_EXECUTOR_H_
#define THIRD_PARTY_GLOOP_THREAD_EXECUTOR_H_

#include <cstdint>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/callback.h"

namespace util {
class SleepUntilImpl;
}

namespace thread {

class ExecutorInternal;  // Internal helper class.  Clients should ignore

// REQUIRES: Implementations inheriting from "Executor" directly must override
// the function taking "AnyInvocable".  Any calls made using "Closure*"
// interfaces will be automatically converted using the default implementations.
class Executor {
 public:
  virtual ~Executor();

  // Schedule the specified "callback" for execution in this executor.
  // Depending on the subclass implementation, this may block in some
  // situations.
  //
  // Note: most executor implementations are not fiber-aware, i.e.
  // if thread::Fiber::Current()->Cancel() is called inside the callback,
  // the cancellation will leak out into the executing thread/fiber.
  // To avoid such leakage, wrap the callback in thread::FiberScoped().
#ifndef SWIG
  virtual void Schedule(absl::AnyInvocable<void() &&> callback) = 0;

  virtual void ScheduleMany(
      absl::Span<absl::AnyInvocable<void() &&>> callbacks);

  // Like Schedule(), except that if the attempt to add the callback would
  // cause the caller to block, do nothing and return false.  When using
  // TrySchedule() the caller should make sure that the callback is deleted if
  // necessary in this case.
  [[nodiscard]] virtual bool TrySchedule(
      absl::AnyInvocable<void() &&> callback) = 0;

  // Schedule given callback for execution in this executor no sooner than
  // 'delay' from time of invocation.  The caller should make sure that
  // this Executor isn't deallocated before closure is run.
  // Implemented here using ScheduleAt().
  //
  // This method is strongly discouraged: callers should prefer using
  // absolute deadlines with `ScheduleAt` and implementations which do not use
  // custom clocks should not override it.
  ABSL_DEPRECATE_AND_INLINE()
  virtual void ScheduleAfterForMigration(
      absl::Duration delay, absl::AnyInvocable<void() &&> callback) {
    ScheduleAt(clock()->TimeNow() + delay, std::move(callback));
  }

  // Schedule given callback for execution in this executor at time 'when'.
  // The caller should make sure that this Executor isn't deallocated before
  // the callback is run.
  virtual void ScheduleAt(absl::Time when,
                          absl::AnyInvocable<void() &&> callback) = 0;
#endif

  // Return an estimate of the number of queued callbacks awaiting execution
  virtual int num_pending_closures() const = 0;

  // Return a pointer to the default executor for this process.  This
  // executor is usually a thread-pool created automatically by the
  // thread library, but can be changed by SetDefaultExecutor().
  //
  // The default executor is very likely to contain multiple threads
  // and therefore callers should not rely on single-threaded
  // execution of callbacks added to the default executor.
  static Executor* DefaultExecutor();

  // Change the default executor for this process to "executor".
#ifndef SWIG
  ABSL_DEPRECATED(
      "The ability to change the default executor to an arbitrary "
      "implementation is being removed since it affects everything running in "
      "the process by allowing switching to an executor with properties some "
      "libraries are not expecting.")
#endif
  static void SetDefaultExecutor(Executor* executor);

  // Return a pointer to some Executor that is running the current thread, or
  // nullptr if this thread is not associated with an Executor.  The intent is
  // to allow callbacks to Schedule() work to an Executor that is
  // guaranteed to exist and to have a priority high enough for the current
  // task.
  //
  // *** Do not assume that CurrentExecutor() points to the initial Executor
  // *** used to queue the caller.  The following code may fail:
  //  static void CheckExecutor(Executor* e) { CHECK(e==CurrentExecutor()); }
  //  x->Add(NewCallback(&CheckExecutor, x));
  // *x may use Executor *y internally, allowing CurrentExecutor() to return
  // either x or y.  When Executors are nested (via SelectServer::Loop()),
  // CurrentExecutor() will point to the most-recently-entered active Executor.
  static Executor* CurrentExecutor();

  // -----------------------------------------------------------------------
  // To be used only by implementations of Executor().
  // CurrentExecutorPointerInternal() returns a pointer to a per-thread
  // field in which the current executor pointer should be stored.
  // When a thread starts work on behalf of this_executor, it should do:
  //    *CurrentExecutorPointerInternal() = this_executor;
  // If the thread continues to run after ceasing to work for the current
  // executor, the previous value of the pointer should be restored.
  static Executor** CurrentExecutorPointerInternal();

  virtual absl::Clock* absl_nonnull clock() {
    return &absl::Clock::GetRealClock();
  }

 protected:
  // Executes 'callback' after 'when'. The callback is executed by a thread
  // internal to the implementation, so the caller must make sure that
  // 'callback' doesn't block or take too long.
  //
  // This is an implementation helper for use by ScheduleAt().  eg:
  //   void ThreadPool::ScheduleAt(absl::Time when,
  //                               absl::AnyInvocable<void() &&> cl) {
  //     DelayUntil(when, absl::bind_front(&ThreadPool::NonBlockingSchedule,
  //                                       this, std::move(cl)));
  //   }
#ifndef SWIG
  void DelayUntil(absl::Time when, absl::AnyInvocable<void() &&> callback);

  // Similar to above, but uses a relative delay and Closure.
  void Delay(absl::Duration delay, absl::AnyInvocable<void() &&> callback);
#endif
};

//  An "inline" executor is a trivial Executor that immediately
//  executes the closure given to it.  It's useful as a mock, and in
//  cases where an Executor is needed, but multi-threadedness is not.
//  The returned object is owned by the caller.
Executor* NewInlineExecutor();

//  You most likely won't need more than one InlineExecutor.
//  Note that this is not true for the synchronized version.
//  The returned object is NOT owned by the caller.
Executor* SingletonInlineExecutor();

//  A "synchronized" inline executor is exactly the same, except that
//  it guarantees that two closures can't be executing at the same
//  time.  You could use this in a multi-threaded program to guarantee
//  serialization of a sequence of actions - without forcing them to
//  execute in a single thread.
//  The returned object is owned by the caller.
Executor* NewSynchronizedInlineExecutor();

// -------------------------
// Cancellation support
//
// Example:
//   thread::ExecutorHandle handle;
//   thread::AddCancellable(executor, 0, NewCallback(...), &handle);
//   ...
//   if (want_to_cancel) {
//     Closure* cb;
//     thread::Cancel(handle, absl::InfiniteDuration(), &cb);
//     delete cb;  // Cleanup in case closure has not run yet
//   }
//
// Cancel() can be invoked with different delay parameters to provide
// a non-blocking or a bounded-delay cancellation.  See below.
//
// Note that due to the way they are implemented, cancelled callbacks
// continue to consume a small amount of memory until the original
// delay time is exceeded. Avoid large numbers of cancellable closures
// with long delays.

// Opaque handle to a cancellable closure added to some executor.
// Clients may freely copy these handles around and access them
// without worrying about whether or not the corresponding closure is
// pending, currently running, finished, or cancelled.
//
// This class is thread-compatible. <link>
class ExecutorHandle {
 public:
  // A default-constructed handle never refers to any closure, and is
  // very cheap to cancel.
  ExecutorHandle() : key_(0) {}
  // Copyable by design

  // Returns true if this ExecutorHandle is empty (has never been scheduled).
  // If this returns false, this handle has been scheduled.  It may have already
  // run, be waiting to run, be currently running, or be already cancelled.
  bool empty() const { return key_ == 0; }

 private:
  friend class ExecutorInternal;
  uint64_t key_;
};

#ifndef SWIG

// Arrange to run the specified callback on executor after a delay of "delay".
// (Actual delay may be longer if the executor is busy.) A delay <= 0 means
// arrange to run the callback as soon as possible. Stores a handle to this
// scheduled activity in *handle. The caller can cancel this scheduled activity
// using thread::Cancel().
//
// NOTE: when this method is used to schedule a callback, the Closure overload
// of thread::Cancel will return a non-repeatable Closure that wraps that
// callback.
//
// REQUIRES: executor != nullptr && closure != nullptr && handle != nullptr
void AddCancellable(Executor* executor, absl::Duration delay,
                    absl::AnyInvocable<void() &&> callback,
                    ExecutorHandle* handle);

// As above, except more efficient when the callback should be scheduled
// immediately.
void AddCancellable(Executor* executor, absl::AnyInvocable<void() &&> callback,
                    ExecutorHandle* handle);

#if ABSL_HAVE_ATTRIBUTE(enable_if)
// Overload to automatically move statically-known zero-delay callbacks to the
// above overload.
ABSL_DEPRECATE_AND_INLINE()
inline void AddCancellable(Executor* executor, absl::Duration delay,
                           absl::AnyInvocable<void() &&> callback,
                           ExecutorHandle* handle)
    __attribute__((enable_if(delay == absl::ZeroDuration(), "zero-delay"))) {
  AddCancellable(executor, std::move(callback), handle);
}
#endif  // ABSL_HAVE_ATTRIBUTE(enable_if)

#endif

// As above, except with Closure instead of AnyInvocable.
//
// REQUIRES: !cb->IsRepeatable() (i.e. cb is not a permanent callback)
// REQUIRES: executor != nullptr && closure != nullptr && handle != nullptr
ABSL_DEPRECATED("Use the AnyInvocable-accepting overload instead.")
void AddCancellable(Executor* executor, absl::Duration delay, Closure* closure,
                    ExecutorHandle* handle);

#ifndef SWIG

// Arrange to run the specified callback on executor at time "when". (Actual
// time may be later if the executor is busy.)  A "when" <= absl::Now() means
// arrange to run the callback as soon as possible. Stores a handle to this
// scheduled activity in *handle. The caller can cancel this scheduled activity
// using thread::Cancel().
//
// NOTE: when this method is used to schedule a callback, the Closure overload
// of thread::Cancel will return a non-repeatable Closure that wraps that
// callback.
//
// REQUIRES: executor != nullptr && closure != nullptr && handle != nullptr
void AddCancellableAt(Executor* executor, absl::Time when,
                      absl::AnyInvocable<void() &&> callback,
                      ExecutorHandle* handle);

#endif

// As above, except with Closure instead of AnyInvocable.
//
// REQUIRES: executor != nullptr && closure != nullptr && handle != nullptr
// REQUIRES: !cb->IsRepeatable() (i.e. cb is not a permanent callback)
ABSL_DEPRECATED("Use the AnyInvocable-accepting overload instead.")
void AddCancellableAt(Executor* executor, absl::Time when, Closure* closure,
                      ExecutorHandle* handle);

namespace executor_internal {

// The result of a cancellation attempt. Convertible to bool where 0/false
// implies "the closure is still running when Cancel() returned" as it always
// has.
enum CancelResultImpl {
  // Cancellation failed because the closure was already running and did not
  // finish within the specified timeout.
  kRunning = 0,
  // Cancellation succeeded, and the closure never ran.
  kCancelled = 1,
  // Cancellation failed because the handle did not refer to a scheduled
  // closure. This can mean the handle was invalid, or that the closure had
  // already run or had been cancelled.
  kNotScheduled = 2,
};

static_assert(CancelResultImpl::kRunning == false);

}  // namespace executor_internal

using CancelResult = executor_internal::CancelResultImpl;

// Attempt to cancel the closure associated with the handle.  If the
// closure is currently running, waits up to timeout for it to finish.  You
// can pass timeout==absl::InfiniteDuration() if you want to wait without a
// timeout.  Never blocks if timeout <= absl::ZeroDuration().
//
// Returns false if the timeout expires while the closure is still running.
// Returns true otherwise.
//
// When the closure is successfully cancelled without being run, a
// pointer to the closure is stored in *cb_ptr.  Else *cb_ptr is set
// to nullptr.
//
// Stores nullptr in *cb_ptr and returns true without any delay if no
// closure associated with the specified handle is found (the closure
// may have already finished running, may have been cancelled by a
// previous Cancel(), or the handle may be invalid).
//
// REQUIRES: cb_ptr != nullptr
CancelResult Cancel(ExecutorHandle handle, absl::Duration timeout,
                    Closure** cb_ptr);

// As above, except deletes the closure instead of returning it when
// successfully cancelled.
//
// NOTE: this function doesn't distinguish between repeatable (permanent) and
// non-permanent callbacks. It deletes whatever the other overload returns.
CancelResult Cancel(ExecutorHandle handle,
                    absl::Duration timeout = absl::InfiniteDuration());

// An alias for the common case of cancelling without potentially blocking for
// the closure to finish.
inline CancelResult TryCancel(ExecutorHandle handle) {
  // Break into a variable to prevent inlining with the below overload.
  absl::Duration timeout = absl::ZeroDuration();
  return Cancel(handle, timeout);
}

#if ABSL_HAVE_ATTRIBUTE(enable_if)
ABSL_DEPRECATE_AND_INLINE()
inline CancelResult Cancel(ExecutorHandle handle, absl::Duration timeout)
    __attribute__((enable_if(timeout <= absl::ZeroDuration(),
                             "Use TryCancel instead."))) {
  return TryCancel(handle);
}

ABSL_DEPRECATE_AND_INLINE()
inline CancelResult Cancel(ExecutorHandle handle, absl::Duration timeout)
    __attribute__((enable_if(timeout == absl::InfiniteDuration(),
                             "Use the one argument overload instead."))) {
  return Cancel(handle);
}
#endif  // ABSL_HAVE_ATTRIBUTE(enable_if)

// --------------------------------------------------------
// Implementation/internal details.  Clients should ignore.
class InlineExecutorInternal {
 private:
  friend class util::SleepUntilImpl;
  static bool IsInlineExecutor(const Executor*);
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_EXECUTOR_H_
