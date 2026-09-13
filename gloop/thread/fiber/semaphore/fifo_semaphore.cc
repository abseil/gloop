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

// Implementation of FIFO semaphore.

#include "gloop/thread/fiber/semaphore/fifo_semaphore.h"

#include <cstdint>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/semaphore/ordered_semaphore.h"

namespace thread {
namespace {

// Selectable implementation enforcing FIFO ordering.
class FifoSelectable : public internal::Selectable {
 public:
  explicit FifoSelectable(internal::OrderedSemaphore::Resource* resource)
      : resource_(resource) {}

  // This type is neither copyable nor movable.
  FifoSelectable(const FifoSelectable&) = delete;
  FifoSelectable& operator=(const FifoSelectable&) = delete;

  // Implementation of Selectable interface.
  bool Handle(internal::CaseState* c, bool enqueue) override
      ABSL_LOCKS_EXCLUDED(resource_->mu());
  void Unregister(internal::CaseState* c) override
      ABSL_LOCKS_EXCLUDED(resource_->mu());

 private:
  internal::OrderedSemaphore::Resource* const resource_;
};

bool FifoSelectable::Handle(internal::CaseState* const c, const bool enqueue) {
  absl::MutexLock l(*resource_->mu());
  const uintptr_t amount = internal::Amount(c);
  if (*resource_->waiters() == nullptr && resource_->available() >= amount) {
    // No waiters ahead of us, and the operation can be performed immediately.
    absl::MutexLock sel_l(c->sel->mu);
    if (c->Pick()) {
      // This case was chosen: take the resource.
      resource_->Acquire(amount);
      return true;
    }
  }

  // Otherwise, enqueue if necessary.
  if (enqueue) {
    internal::PushBack(resource_->waiters(), c);
  }
  return false;
}

void FifoSelectable::Unregister(internal::CaseState* c) {
  absl::MutexLock l(*resource_->mu());
  resource_->Remove(c);
}

}  // namespace

FifoSemaphore::FifoSemaphore(uintptr_t capacity)
    : sem_(capacity), acquirer_(sem_.MakeSelectable<FifoSelectable>()) {}

void FifoSemaphore::Acquire(const uintptr_t amount) {
  Select({OnAcquire(amount)});
}

Case FifoSemaphore::OnAcquire(const uintptr_t amount) {
  CHECK_GE(sem_.capacity(), amount);
  Case c = {acquirer_.get(), internal::UintptrToIntptr(amount)};
  return c;
}

bool FifoSemaphore::TryAcquire(const uintptr_t amount) {
  return sem_.TryAcquire(amount);
}

void FifoSemaphore::Release(const uintptr_t amount) { sem_.Release(amount); }

void FifoSemaphore::WaitUntilAllResourcesReleased() {
  Acquire(sem_.capacity());
  Release(sem_.capacity());
}

absl::StatusOr<FifoSemaphoreLock>
FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(FifoSemaphore* semaphore,
                                                     uintptr_t amount,
                                                     absl::Time deadline,
                                                     absl::SourceLocation loc) {
  if (semaphore == nullptr) {
    return absl::InvalidArgumentError("semaphore must not be null", loc);
  }
  if (amount == 0) {
    return absl::InvalidArgumentError("amount must not be 0", loc);
  }
  switch (thread::SelectUntil(
      deadline, {thread::OnCancel(), semaphore->OnAcquire(amount)})) {
    case -1: {
      return absl::DeadlineExceededError(
          absl::StrCat("Deadline exceeded while waiting to acquire ", amount),
          loc);
    }
    case 0: {
      return absl::CancelledError("Fiber cancelled.", loc);
    }
    case 1: {
      return FifoSemaphoreLock(semaphore, amount, kPreAcquired);
    }
  }
  LOG(FATAL) << "Should never reach here.";
}

FifoSemaphoreLock::FifoSemaphoreLock(FifoSemaphore& semaphore,
                                     const uintptr_t amount)
    : amount_(amount), semaphore_(&semaphore) {
  LOG_IF_EVERY_N_SEC(WARNING, amount == 0, 600)
      << "FifoSemaphoreLock with amount == 0 is a no-op.";
  semaphore_->Acquire(amount_);
}

FifoSemaphoreLock::FifoSemaphoreLock(FifoSemaphore* semaphore, uintptr_t amount,
                                     PreAcquiredT pre_acquired)
    : amount_(amount), semaphore_(semaphore) {}

FifoSemaphoreLock::~FifoSemaphoreLock() {
  if (semaphore_ != nullptr) {
    semaphore_->Release(amount_);
  }
}

FifoSemaphoreLock::FifoSemaphoreLock(FifoSemaphoreLock&& other)
    : amount_(std::exchange(other.amount_, 0)),
      semaphore_(std::exchange(other.semaphore_, nullptr)) {}

FifoSemaphoreLock& FifoSemaphoreLock::operator=(FifoSemaphoreLock&& other) {
  if (this != &other) {
    if (semaphore_ != nullptr) {
      semaphore_->Release(amount_);
    }
    amount_ = std::exchange(other.amount_, 0);
    semaphore_ = std::exchange(other.semaphore_, nullptr);
  }
  return *this;
}

FifoSemaphoreMutexLock::FifoSemaphoreMutexLock(FifoSemaphore& semaphore)
    : semaphore_(semaphore) {
  semaphore_.Acquire(semaphore_.capacity());
}

FifoSemaphoreMutexLock::~FifoSemaphoreMutexLock() {
  semaphore_.Release(semaphore_.capacity());
}

}  // namespace thread
