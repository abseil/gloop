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

#include "gloop/thread/threadpool.h"

#if !THREAD_HAVE_ALTERNATE_THREAD_POOL
#error Feature macros and BUILD file are out of sync.
#endif

#ifdef _MSC_VER
#include <process.h>
#include <windows.h>
#else  // _MSC_VER
#include <pthread.h>
#endif  // _MSC_VER
#include <stddef.h>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/callback.h"

ThreadPool::~ThreadPool() { Shutdown(); }

void ThreadPool::Schedule(std::function<void()> closure) {
  queue_mutex_.Lock();
  queue_.push_back(std::move(closure));
  condition_.Signal();
  queue_mutex_.Unlock();
}

bool ThreadPool::ScheduleIfReadyToRun(std::function<void()> closure) {
  absl::MutexLock lock(&queue_mutex_);

  bool scheduled = false;
  if (num_executing_threads_ + queue_.size() < num_threads_) {
    // Executing by any unassigned worker is guaranteed.
    queue_.push_back(std::move(closure));
    condition_.Signal();
    scheduled = true;
  }

  return scheduled;
}

void ThreadPool::SetThreadNamePrefix(absl::string_view name_prefix) {
  name_prefix_ = name_prefix;
}

int ThreadPool::queue_count() {
  absl::MutexLock lock(&queue_mutex_);
  return static_cast<int>(queue_.size());
}

int ThreadPool::num_threads() const { return num_threads_; }

// A simple implementation that mirrors the non-portable Thread.  We may
// choose to expand this in the future as a portable implementation of
// Thread, or replace it at such a time as one is implemented.
class ThreadPool::WorkerThread {
 public:
  // Creates and starts a thread that runs pool->WorkerFunction().
  WorkerThread(ThreadPool* pool, const std::string& name_prefix);

  // This type is neither copyable nor movable.
  WorkerThread(const WorkerThread&) = delete;
  WorkerThread& operator=(const WorkerThread&) = delete;

  // REQUIRES: Join() must have been called.
  ~WorkerThread();

  // Joins with the running thread.
  void Join();

 private:
#ifdef _MSC_VER
  static unsigned int __stdcall WinThreadBody(void* arg);
#endif
  static void* ThreadBody(void* arg);
  void SetupName();

  ThreadPool* pool_;
  std::string name_prefix_;
#ifdef _MSC_VER
  HANDLE handle_;
#else
  pthread_t thread_;
#endif
};

#ifdef _MSC_VER

// Note: pool also contains name_prefix, but it is passed separately to be
// closer to the google3 Thread interface.
ThreadPool::WorkerThread::WorkerThread(ThreadPool* pool,
                                       const std::string& name_prefix)
    : pool_(pool), name_prefix_(name_prefix) {
  uintptr_t handle = _beginthreadex(
      /*security=*/nullptr, /*stack_size=*/0, &WinThreadBody, this,
      /*initflag=*/CREATE_SUSPENDED, /*thrdaddr=*/nullptr);
  CHECK_NE(0, handle);
  handle_ = reinterpret_cast<HANDLE>(handle);
  ResumeThread(handle_);
}

ThreadPool::WorkerThread::~WorkerThread() { CloseHandle(handle_); }

void ThreadPool::WorkerThread::Join() {
  WaitForSingleObject(handle_, INFINITE);
}

void ThreadPool::WorkerThread::SetupName() {
  // Not currently supported on Windows.
}

unsigned int __stdcall ThreadPool::WorkerThread::WinThreadBody(void* arg) {
  ThreadBody(arg);
  return 0;
}

#else  // _MSC_VER

ThreadPool::WorkerThread::WorkerThread(ThreadPool* pool,
                                       const std::string& name_prefix)
    : pool_(pool), name_prefix_(name_prefix) {
  int res = pthread_create(&thread_, nullptr, ThreadBody, this);
  CHECK_EQ(res, 0) << "pthread_create failed";
}

ThreadPool::WorkerThread::~WorkerThread() {}

void ThreadPool::WorkerThread::Join() { pthread_join(thread_, nullptr); }

void ThreadPool::WorkerThread::SetupName() {
  if (!name_prefix_.empty()) {
#ifdef __APPLE__
    // Apple's version of pthread_setname_np takes one argument and operates on
    // the current thread only. Also, pthread_mach_thread_np is Apple-specific.
    // The maximum size of the |name| buffer was noted in the Chromium source
    // code and was confirmed by experiments.
    char name[64];
    mach_port_t id = pthread_mach_thread_np(pthread_self());
    int rv = snprintf(name, sizeof(name), "%s/%lld", name_prefix_.c_str(),
                      static_cast<int64>(id));
    CHECK_GE(rv, 0);
    if (rv >= sizeof(name)) {
      name[sizeof(name) - 1] = '\0';
    }
    rv = pthread_setname_np(name);
    CHECK_EQ(rv, 0);
#elif defined(__ANDROID__)
    // If the |name| buffer is longer than 16 bytes, pthread_setname_np fails
    // with error 34 (ERANGE) on Android.
    char name[16];
    pid_t id = gettid();
    int rv = snprintf(name, sizeof(name), "%s/%lld", name_prefix_.c_str(),
                      static_cast<int64>(id));
    CHECK_GE(rv, 0);
    if (rv >= sizeof(name)) {
      name[sizeof(name) - 1] = '\0';
    }
    rv = pthread_setname_np(pthread_self(), name);
    CHECK_EQ(rv, 0);
#endif
  }
}

#endif  // _MSC_VER

void* ThreadPool::WorkerThread::ThreadBody(void* arg) {
  auto thread = reinterpret_cast<WorkerThread*>(arg);
  thread->SetupName();
  thread->pool_->WorkerFunction();
  return nullptr;
}

void ThreadPool::WorkerFunction() {
  queue_mutex_.Lock();
  while (true) {
    if (queue_.empty()) {
      if (exit_threads_) {
        break;  // Queue is empty and exit was requested.
      } else {
        // Queue is empty, wait for signal or broadcast.
        condition_.Wait(&queue_mutex_);
      }
    } else {
      // Take a job from the queue.
      std::function<void()> job = std::move(queue_.front());
      queue_.pop_front();

      // Signal execution.
      ++num_executing_threads_;
      queue_mutex_.Unlock();
      // Note that it is good practice to surround this with a try/catch so
      // the thread pool doesn't go to hell if the job throws an exception.
      // This is omitted here because Google3 doesn't like exceptions.
      std::move(job)();
      job = nullptr;

      // Signal execution completed.
      queue_mutex_.Lock();
      --num_executing_threads_;
    }
  }
  queue_mutex_.Unlock();
}

void ThreadPool::Shutdown() {
  // Tell worker threads how to exit.
  queue_mutex_.Lock();
  exit_threads_ = true;
  condition_.SignalAll();
  queue_mutex_.Unlock();

  // Join all workers. This will block.
  for (int i = 0; i < pool_.size(); ++i) {
    CHECK(pool_[i] != nullptr);
    pool_[i]->Join();
    delete pool_[i];
  }

  pool_.clear();
}

ThreadPool::ThreadPool(int num_threads, Options options)
    : name_prefix_(options.name_prefix) {
  // It is a common error to call ThreadPool(workitems.size()), which
  // crashes when workitems is empty. Prevent those crashes by
  // creating at least one thread.
  if (num_threads == 0) num_threads = 1;
  CHECK_GT(num_threads, 0);
  num_threads_ = num_threads;

  pool_.resize(num_threads_);
  for (int i = 0; i < pool_.size(); ++i) {
    pool_[i] = new WorkerThread(this, name_prefix_);
  }
}
