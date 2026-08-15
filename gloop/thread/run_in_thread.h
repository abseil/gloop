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

#ifndef THIRD_PARTY_GLOOP_THREAD_RUN_IN_THREAD_H_
#define THIRD_PARTY_GLOOP_THREAD_RUN_IN_THREAD_H_

#include <signal.h>

#include "absl/synchronization/mutex.h"
#include "gloop/base/spinlock.h"

#if !defined(__APPLE__)
#include <ucontext.h>  // for ucontext_t
#endif

#include "gloop/base/port.h"
#include "gloop/thread/os_semaphore.h"
#include "gloop/thread/thread-internal.h"

// -------------------------------------------------------------------------
// Support for running a function (via signal handler) in each thread
// -------------------------------------------------------------------------

// Class which allows an arbitrary "target function" to be run from a
// signal handler in an arbitrary thread.  This class is a singleton;
// it is accessed using the Instance() method.
//
// Example code to invoke a function in a particular thread, waiting
// 10ms for the trampoline to start, might look like:
//
//    void target_func(void* arg, ucontext_t* uc) {
//      ... do something (must be async-signal-safe) ...
//    }
//
//    const LiveThread* target_thread = <select target thread>;
//    bool ran = RunInThread::Instance()->Run(
//        target_thread, target_function, arg, 10 /* ms */);
//
// Obviously, the target function must be async-signal-safe, since it
// will be invoked from an asynchronous signal.  To make this a little
// bit easier, RunInThread saves and restores errno around the call to
// the target function.
class RunInThread {
 public:
  // Type of the function to be invoked in the target thread.
  //
  // When the function is run, "arg" is the "arg" value given to Run.
  // "uc" points to the context of the target thread when the signal
  // was delivered.  "thread" is the LiveThread in which the function
  // is being run.
  typedef void (*TargetFunctionType)(void* arg, ucontext_t* uc,
                                     const LiveThread* thread);

  static constexpr int kSignalToUse = GOOGLE_OBSCURE_SIGNAL;  // see port.h

  // Not copyable, assignable, destructable.
  RunInThread(const RunInThread&) = delete;
  RunInThread& operator=(const RunInThread&) = delete;
  ~RunInThread() = delete;

  // Cause the target function "func" to be run (with "arg") from a
  // signal handler in target_thread's context.  This uses a signal
  // handler even if target_thread is the current thread.
  //
  // This function will return true after the target function has run
  // and returned, or will return false if at least "timeout_ms"
  // milliseconds have elapsed without target_thread having started to
  // run the function.  (Once the function has started to run, this
  // function will wait for it to complete.) If this function returns
  // false, the target function will not be run at all.
  //
  // This function cannot be called from signal handlers.
  //
  // L < mu_
  bool Run(const LiveThread* target_thread, TargetFunctionType func, void* arg,
           int timeout_ms);

  // Like Run, but will return false immediately if the RunInThread
  // instance is already in use.  This can be used from code which
  // must be async-signal-safe.
  //
  // L = *
  bool TryRun(const LiveThread* target_thread, TargetFunctionType func,
              void* arg, int timeout_ms);

  // Return whether the instance succeeded in installing the signal handler.
  bool installed_signal_handler() const { return installed_signal_handler_; }

  // Get the instance of the RunInThread singleton.  First invocation
  // is not async-signal-safe, but subsequent invocations are.
  //
  // L = *
  static RunInThread* Instance();

  // Checks whether the RunInThread signal handler is registered.
  static bool IsRegistered();

 private:
  // Semaphore used by target thread to signal completion of the
  // target function.  A semaphore (sem_t) was chosen because sem_post
  // is async-signal-safe.
  //
  // The semaphore will typically have the value 0.  (It will be
  // decremented to 0 by the sending thread, which will then wait for
  // the semaphore to be be incremented or for the request to time
  // out.  The target thread's sem_post will increment the semaphore's
  // value, waking the sending thread.)
  thread::internal::OsSemaphore sem_;

  absl::Mutex mu_;            // < spin_; protects RunInThread::Instance()
                              // state from concurrent access while delivering
                              // the signal to the target thread.
  TargetFunctionType func_;   // function for thread to run; under spin_.
                              // nullptr means "nothing to do."
  void* arg_;                 // argument to func_; under spin_
  const LiveThread* thread_;  // target thread; under spin_
  SpinLock spin_;             // > mu_; protects func_, arg_, thread_, done_.
                              // (Held to keep invocations of Run() and the
                              // signal handler on the target thread from
                              // interfering with each other.  Held while func_
                              // is running, and to check doneness while a
                              // request is outstanding.)

  bool done_;  // thread_ has finished running (*func_)(arg_,uc); under spin_

  static pthread_once_t instance_init_once_;
  static RunInThread* instance_;   // Read-only after init.  Will be
                                   // initialized before
                                   // SignalHandler is called.
  bool installed_signal_handler_;  // whether the signal handler could
                                   // be installed; read-only after init.

  // Implement Run and TryRun.
  //
  // L >= mu_
  // L < spin_
  bool RunLocked(const LiveThread* target_thread, TargetFunctionType func,
                 void* arg, int timeout_ms);

  // Check to see if the target function has finished.
  bool done() {
    SpinLockHolder l(spin_);
    return done_;
  }

  RunInThread();

  // Signal the target thread that a function is ready to run.  Will
  // wait for up to 'timeout_ms' milliseconds for that function to
  // start running.
  void NotifyThread(const LiveThread* target_thread, int timeout_ms);

  static void SignalHandler(int sig, siginfo_t* si, void* vuc);

  static void InitInstance();
};

#endif  // THIRD_PARTY_GLOOP_THREAD_RUN_IN_THREAD_H_
