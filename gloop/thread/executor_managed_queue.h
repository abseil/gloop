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

// This is an alternative implementation of ThreadManager that leverages another
// executor as a source for underlying parallelism. That executor could be a
// global executor or a bespoke one.
//
// This implementation is intended to roll out to replace the current
// implementation of ThreadManager.

#ifndef THIRD_PARTY_GLOOP_THREAD_EXECUTOR_MANAGED_QUEUE_H_
#define THIRD_PARTY_GLOOP_THREAD_EXECUTOR_MANAGED_QUEUE_H_

#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/chunked_queue.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/context.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread_manager.h"
#include "gloop/util/refcount/blocking_refcount.h"

namespace thread::internal {

struct ThreadManagerExecutorRep final : public ThreadManager::RepBase {
  explicit ThreadManagerExecutorRep(
      absl_nonnull std::shared_ptr<Executor> executor)
      : executor(executor) {}
  explicit ThreadManagerExecutorRep(
      ABSL_ATTRIBUTE_LIFETIME_BOUND Executor& executor)
      : executor({&executor, [](void*) {}}) {}

  // ThreadManager's destructor needs to wait for all queues to be destroyed and
  // work to complete. Because ExecutorManagedQueue itself cannot be destroyed
  // until all work is complete, this refcount wait ensures both.
  ~ThreadManagerExecutorRep() override { refcount.WaitForZero(); }

  ManagedQueue* NewQueue(absl::string_view name,
                         const ManagedQueueOptions& queue_options) override;

  absl_nonnull std::shared_ptr<Executor> executor;
  util::BlockingRefcount refcount;
};

// A ManagedQueue that wraps an Executor. This uses shared_from_this to allow
// the "real" queue to live until all closures which potentially are scheduled
// on it using CurrentExecutor() have completed. This matches the historical
// behavior of ManagedQueue, and the documented behavior of the ManagedQueue
// destructor.
class ExecutorManagedQueue final
    : public ManagedQueue,
      public std::enable_shared_from_this<ExecutorManagedQueue> {
 public:
  explicit ExecutorManagedQueue(ThreadManagerExecutorRep& rep,
                                absl::string_view name,
                                ManagedQueueOptions options)
      : rep_(rep),
        tm_ref_(rep.refcount.GetRef()),
        name_(std::move(name)),
        options_(options) {
    RegisterQueueForStats();
  }

  ~ExecutorManagedQueue() override { UnregisterQueueForStats(); }

  std::string name() const override { return name_; }

  ManagedQueueOptions queue_options() const override { return options_; }

  ManagedQueue* current_executor_for_testing() const override {
    return const_cast<ExecutorManagedQueue*>(this);
  }

  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    if (Unbounded()) [[likely]] {
      return rep_.executor->Schedule(Wrap(std::move(callback)));
    }
    absl::MutexLock lock(
        mu_, absl::Condition(this, &ExecutorManagedQueue::HasQueueSpace));
    ForceSchedule(std::move(callback));
  }

  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    if (Unbounded()) [[likely]] {
      return rep_.executor->TrySchedule(Wrap(std::move(callback)));
    }
    absl::MutexLock lock(mu_);
    if (!HasQueueSpace() && active_workers_ == options_.thread_limit) {
      return false;
    }
    ForceSchedule(std::move(callback));
    return true;
  }

  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    if (Unbounded()) [[likely]] {
      return rep_.executor->ScheduleAt(when, Wrap(std::move(callback)));
    }
    rep_.executor->ScheduleAt(
        when, Wrap([this, callback = std::move(callback)]() mutable {
          absl::MutexLock lock(mu_);
          ForceSchedule(std::move(callback));
        }));
  }

  int num_pending_closures() const override {
    absl::MutexLock lock(mu_);
    return pending_work_.size();
  }

  void WaitUntilComplete() override { refcount_.WaitForZero(); }

  ManagedQueueStats Stats() const override {
    return {.queue_name = name_,
            .queue_running = static_cast<int>(refcount_.count()),
            .num_pending_closures = num_pending_closures()};
  }

 private:
  struct DeferredWork {
    DeferredWork() = default;
    DeferredWork(absl::AnyInvocable<void() &&> callback)
        : callback(std::move(callback)), context(base::Context::kThread) {}

    void operator()() && {
      DCHECK(callback);
      base::WithContext wc(std::move(context));
      std::exchange(callback, nullptr)();
    }

    absl::AnyInvocable<void() &&> callback;
    base::Context context;
  };

  bool Unbounded() const { return options_.thread_limit == INT_MAX; }
  bool HasQueueSpace() const ABSL_SHARED_LOCKS_REQUIRED(mu_) {
    return active_workers_ < options_.thread_limit ||
           static_cast<int64_t>(pending_work_.size()) < options_.queue_limit;
  }

  absl::AnyInvocable<void() &&> Wrap(absl::AnyInvocable<void() &&> callback) {
    return [self = shared_from_this(), callback = std::move(callback),
            ref = refcount_.GetRef()]() mutable {
      Executor* old = std::exchange(*Executor::CurrentExecutorPointerInternal(),
                                    self.get());
      std::exchange(callback, nullptr)();
      *Executor::CurrentExecutorPointerInternal() = old;
    };
  }

  // Schedule the callback to run and ensure there is a worker to run it if
  // below the thread limit.
  void ForceSchedule(absl::AnyInvocable<void() &&> callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (active_workers_ < options_.thread_limit) {
      ++active_workers_;
      rep_.executor->Schedule(
          Wrap([this, callback = std::move(callback)]() mutable {
            RunWorker(std::move(callback));
          }));
    } else {
      pending_work_.push_back(DeferredWork{std::move(callback)});
    }
  }

  void RunWorker(absl::AnyInvocable<void() &&> callback) {
    std::exchange(callback, nullptr)();
    absl::MutexLock lock(mu_);
    while (!pending_work_.empty()) {
      DeferredWork work = std::move(pending_work_.front());
      pending_work_.pop_front();
      mu_.unlock();
      std::move(work)();
      mu_.lock();
    }
    --active_workers_;
  }

  ThreadManagerExecutorRep& rep_;
  const util::BlockingRefcountReference tm_ref_;
  const std::string name_;
  const ManagedQueueOptions options_;

  util::BlockingRefcount refcount_;

  // State for managing worker threads when the queue specifies limits. This
  // state is not used when no thread limit is specified to avoid acquiring a
  // mutex.
  mutable absl::Mutex mu_;
  int64_t active_workers_ ABSL_GUARDED_BY(mu_) = 0;
  absl::chunked_queue<DeferredWork> pending_work_ ABSL_GUARDED_BY(mu_);
};

// A ManagedQueue that wraps another ManagedQueue owned by a shared_ptr.
class WrapperManagedQueue final : public ManagedQueue {
 public:
  explicit WrapperManagedQueue(
      absl_nonnull std::shared_ptr<ExecutorManagedQueue> impl)
      : impl_(std::move(impl)) {}

  std::string name() const override { return impl_->name(); }
  ManagedQueueOptions queue_options() const override {
    return impl_->queue_options();
  }

  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    impl_->Schedule(std::move(callback));
  }
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    return impl_->TrySchedule(std::move(callback));
  }
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    impl_->ScheduleAt(when, std::move(callback));
  }
  int num_pending_closures() const override { return 0; }
  void WaitUntilComplete() override { impl_->WaitUntilComplete(); }

  ManagedQueueStats Stats() const override { return impl_->Stats(); }

  // for testing
  ManagedQueue* current_executor_for_testing() const override {
    return impl_->current_executor_for_testing();
  }

 private:
  const absl_nonnull std::shared_ptr<ExecutorManagedQueue> impl_;
};

inline ManagedQueue* ThreadManagerExecutorRep::NewQueue(
    absl::string_view name, const ManagedQueueOptions& queue_options) {
  return new WrapperManagedQueue(
      std::make_shared<ExecutorManagedQueue>(*this, name, queue_options));
}

}  // namespace thread::internal

#endif  // THIRD_PARTY_GLOOP_THREAD_EXECUTOR_MANAGED_QUEUE_H_
