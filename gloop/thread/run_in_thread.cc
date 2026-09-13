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

#include "gloop/thread/run_in_thread.h"

#include <cerrno>
#include <csignal>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/signal-handler.h"
#include "gloop/base/spinlock.h"
#include "gloop/thread/os_semaphore.h"
#include "gloop/thread/thread-internal.h"

extern const LiveThread* Thread_GetMyLiveThread();

RunInThread::RunInThread()
    // RunInThread's spinlock may be taken inside signal handlers.
    : func_(nullptr),
      spin_(absl::base_internal::SCHEDULE_KERNEL_ONLY),
      installed_signal_handler_(false) {
  CHECK_EQ(0, thread::internal::OsSemaphoreInit(&sem_));
}

void RunInThread::NotifyThread(const LiveThread* target_thread,
                               int timeout_ms) {
  if (pthread_kill(target_thread->tid_, kSignalToUse) != 0) {
    // Couldn't deliver the signal, no point in waiting.
    return;
  }

  // Note that a large real-time step backwards (e.g., due to an NTP
  // step) could make this code wait longer than desired.  That's not
  // considered a real worry.

  while (pthread_kill(target_thread->tid_, 0) == 0 && !done()) {
    // If the request timed out, give up.  Otherwise, we keep trying.
    // (This code is robust against extra posts, e.g., due to late
    // delivery of the signal, and is intended to use sem_ only to get
    // wakeup notifications from the target thread.)

    int result = thread::internal::OsSemaphoreTimedWaitRelative(
        &sem_, absl::Milliseconds(timeout_ms));

    //
    // This code compensates for some buggy sem_timedwait implementations:
    // sem_timedwait returns the error number directly (rather
    // than returning -1 and setting errno, as specified by POSIX).
    // (see http://sources.redhat.com/bugzilla/show_bug.cgi?id=133).
    if (result == ETIMEDOUT || (result == -1 && errno == ETIMEDOUT)) {
      break;
    }
  }
}

void RunInThread::SignalHandler(int sig, siginfo_t* si, void* vuc) {
  int saved_errno = errno;
  const LiveThread* this_thread = Thread_GetMyLiveThread();

  // We can get here without a LiveThread pointer in certain
  // circumstances, in which case we return immediately.
  //
  // There is a window in which this thread is on the live thread list
  // but when Thread_GetMyLiveThread will return nullptr (because it has
  // started to exit, and the my_live_thread_holder TSD destructor has
  // been called but has not yet completed).  If Thread_ForEach is
  // called (or NotifyThread is otherwise used to signal this thread)
  // in that window, then this function will be run when there is no
  // LiveThread pointer for this thread.
  if (this_thread == nullptr) {
    errno = saved_errno;
    return;
  }

  // Note that if a late delivery (i.e. when mu_ is unlocked) can
  // crash while this spin lock to be held, use of TryRun may not be
  // async-signal-safe because it may deadlock on spin_.  Therefore,
  // the code checking for timely delivery with spin_ locked should be
  // careful not to crash.
  {
    SpinLockHolder spin_lock(instance_->spin_);
    if (instance_->func_ != nullptr && instance_->thread_ == this_thread) {
      (*instance_->func_)(instance_->arg_, static_cast<ucontext_t*>(vuc),
                          this_thread);
      instance_->done_ = true;
    }
  }
  thread::internal::OsSemaphorePost(&instance_->sem_);

  errno = saved_errno;
}

bool RunInThread::Run(const LiveThread* target_thread, TargetFunctionType func,
                      void* arg, int timeout_ms) {
  absl::MutexLock mu_lock(mu_);
  return RunLocked(target_thread, func, arg, timeout_ms);
}

bool RunInThread::TryRun(const LiveThread* target_thread,
                         TargetFunctionType func, void* arg, int timeout_ms) {
  bool done = false;
  if (mu_.try_lock()) {
    done = RunLocked(target_thread, func, arg, timeout_ms);
    mu_.unlock();
  }
  return done;
}

bool RunInThread::RunLocked(const LiveThread* target_thread,
                            TargetFunctionType func, void* arg,
                            int timeout_ms) {
  if (!installed_signal_handler()) {
    // Cannot deliver signal if we have no handler, no point in trying.
    return false;
  }

  bool done = false;
  {
    SpinLockHolder spin_lock(spin_);
    func_ = func;
    arg_ = arg;
    thread_ = target_thread;
    done_ = false;
  }

  NotifyThread(target_thread, timeout_ms);

  {
    SpinLockHolder spin_lock(spin_);
    done = done_;
    func_ = nullptr;
  }

  return done;
}

pthread_once_t RunInThread::instance_init_once_ = PTHREAD_ONCE_INIT;
RunInThread* RunInThread::instance_ = nullptr;

void RunInThread::InitInstance() {
  instance_ = new RunInThread();

  if (ShouldInstallDefaultSignalHandler("stackdump", kSignalToUse)) {
    struct sigaction sa;
    // Set SA_RESTART: collecting a stack dump should not cause pending system
    // calls to fail.
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    if (UseAlternateStackForSignal(kSignalToUse)) {
      sa.sa_flags |= SA_ONSTACK;
    }
    // Block all the signals other than fatal signals and SIGPROF.
    // This is to avoid lock inversion between lock used in libunwind
    // and the spinlock used in RunInThread::SignalHandler.
    // For fatal signals and SIGPROF, GOOGLE_OBSCURE_SIGNAL will be
    // blocked in their signal handlers to avoid the lock inversion.
    // (http://b/19830650).
    //
    // There are two possible deadlock scenarios:
    // The first senario only requires recursive signal handling in one thread:
    // Thread 1:
    //   Wait for spinlock held by Thread 2
    //   RunInThread::SignalHandler
    //   <signal GOOGLE_OBSCURE_SIGNAL>
    //   Hold the lock in libunwind
    //   ProfileHandler::SignalHandler
    //   <signal SIGPROF>
    //   Application code.
    //
    // Thread 2:
    //   Wait for the lock in libunwind in target func GatherStackTrace.
    //   Hold spinlock
    //   RunInThread::SignalHandler
    //   <signal GOOGLE_OBSCURE_SIGNAL>
    //   Application code.
    //
    // The second senario requires recursive signal handling in at least
    // two threads:
    // Thread 1:
    //   The same as Thread 1 above.
    //
    // Thread 2:
    //   Wait for the lock in libunwind
    //   FailureSignalHandler
    //   <signal SIGSEGV>
    //   Hold spinlock
    //   RunInThread::SignalHandler
    //   <signal GOOGLE_OBSCURE_SIGNAL>
    //   Application code.
    //
    // If we can block GOOGLE_OBSCURE_SIGNAL for any signal handler which
    // calls libunwind inside, we can avoid either senario above -- by
    // preventing the case in Thread 1. We already do that for
    // FailureSignalHandler and ProfileHandler::SignalHandler. However,
    // there may be signal handler using libunwind which we havn't noticed
    // yet.
    //
    // So we block all the signals other than fatal signals and SIGPROF
    // here for RunInThread::SignalHandler. It is to reduce the chance of
    // second case above (But cannot guarantee to prevent it). Fatal signals
    // are not blocked in RunInThread::SignalHandler because if the target
    // func of RunInThread does anything bad, we can still get a precise
    // failure report. SIGPROF is not blocked for a better profiling result.
    sigfillset(&sa.sa_mask);
    sigdelset(&sa.sa_mask, SIGSEGV);
    sigdelset(&sa.sa_mask, SIGILL);
    sigdelset(&sa.sa_mask, SIGFPE);
    sigdelset(&sa.sa_mask, SIGABRT);
    sigdelset(&sa.sa_mask, SIGBUS);
    sigdelset(&sa.sa_mask, SIGTERM);
    sigdelset(&sa.sa_mask, SIGPROF);
    sigdelset(&sa.sa_mask, SIGTRAP);
    sa.sa_sigaction = &RunInThread::SignalHandler;
    sigaction(kSignalToUse, &sa, nullptr);
    instance_->installed_signal_handler_ = true;
  }
}

RunInThread* RunInThread::Instance() {
  // This assumes that pthread_once is async-signal-safe after the
  // code it protects has been run once, even though it's not
  // specified to be.  Assuming that, this function is
  // async-signal-safe after the first call.
  pthread_once(&instance_init_once_, &InitInstance);
  return instance_;
}

bool RunInThread::IsRegistered() {
  struct sigaction sa = {};
  // Assume we're not registered if SignalToUse is invalid.
  if (sigaction(kSignalToUse, nullptr, &sa) != 0) {
    return false;
  }
  return sa.sa_sigaction == &SignalHandler;
}
