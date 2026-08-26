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

#include "gloop/util/callback/blocking_callback.h"

#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/util/functional/from_callback.h"

BlockingClosure::BlockingClosure(::util::functional::CallbackFunctor<> closure)
    : closure_(closure),
      repeatable_(closure ? closure->IsRepeatable() : true) {}

BlockingClosure::BlockingClosure() : closure_(nullptr), repeatable_(true) {}

BlockingClosure::~BlockingClosure() {}

// virtual
void BlockingClosure::Run() {
  if (closure_ != nullptr) {
    closure_->Run();
  }
  absl::MutexLock l(done_lock_);
  done_ = true;
  num_calls_++;
}

void BlockingClosure::Wait() const {
  done_lock_.LockWhen(absl::Condition(&done_));
  done_lock_.unlock();
}

bool BlockingClosure::WaitWithTimeout(absl::Duration deadline) const {
  bool done = done_lock_.LockWhenWithTimeout(absl::Condition(&done_), deadline);
  done_lock_.unlock();
  return done;
}

bool BlockingClosure::WaitWithTimeout(int64_t deadline_ms) const {
  return WaitWithTimeout(absl::Milliseconds(deadline_ms));
}

void BlockingClosure::WaitForNum(int N) const {
  absl::MutexLock l(done_lock_);
  auto cond = [this, N]() ABSL_SHARED_LOCKS_REQUIRED(&done_lock_) {
    return NumCalledCondition(N);
  };
  return done_lock_.Await(absl::Condition(&cond));
}

bool BlockingClosure::WaitForNumCalled(int N, absl::Duration deadline) const {
  absl::MutexLock l(done_lock_);
  auto cond = [this, N]() ABSL_SHARED_LOCKS_REQUIRED(&done_lock_) {
    return NumCalledCondition(N);
  };
  return done_lock_.AwaitWithTimeout(absl::Condition(&cond), deadline);
}

bool BlockingClosure::WaitForNumCalled(int N, int deadline_ms) const {
  return WaitForNumCalled(N, absl::Milliseconds(deadline_ms));
}

void BlockingClosure::Reset() {
  DCHECK(IsRepeatable());
  absl::MutexLock l(done_lock_);
  done_ = false;
  num_calls_ = 0;
}

int BlockingClosure::num_called() const {
  absl::ReaderMutexLock lock(done_lock_);
  return num_calls_;
}

// virtual
bool BlockingClosure::IsRepeatable() const { return repeatable_; }

bool BlockingClosure::NumCalledCondition(int expected_count) const {
  return num_calls_ >= expected_count;
}
