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

// Fiber implementation internals -- for public interfaces see fiber.h

// IWYU pragma: private, include "gloop/thread/fiber/fiber.h"

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_INTERNAL_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_INTERNAL_H_

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/flags/declare.h"
#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/thread-identity.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.pb.h"
#include "gloop/thread/timedcall.h"

ABSL_DECLARE_FLAG(int32_t, fibers_default_thread_stack_size);

namespace thread {

class Fiber;
class FiberOptions;

namespace internal {

using InvocableImpl = absl::AnyInvocable<void() &&>;

class FiberHelpers {
 public:
  // Returns the fiber attached to the schedulable, or nullptr if none.
  //
  // REQUIRES: The caller must hold scheduler_state.association_lock
  // of the thread associated with this Schedulable, like in the example below.
  // Returned fiber may only be dereferenced for as long as this lock is held.
  //
  //  auto* identity = const_cast<absl::base_internal::ThreadIdentity*>(
  //      LiveThread_Identity(thread));
  //
  //  {
  //    SpinLockHolder l(identity->scheduler_state.association_lock());
  //    const auto* schedulable =
  //        base::scheduling::Schedulable::GetBoundSchedulable(identity);
  //    if (schedulable == nullptr) return false;  // Not a fiber.
  //
  //    DCHECK(schedulable->type == base::scheduling::Schedulable::kWorkItem);
  //
  //    const Fiber* fiber =
  //        internal::FiberUtil::FiberFromSchedulable(schedulable);
  //    if (fiber == nullptr) return;
  //
  //    // use fiber
  //  }
  static const Fiber* FiberFromSchedulable(
      const base::scheduling::Schedulable* sched);

  static bool IsFiberDetached(const Fiber* fiber);

  // Returns fiber->type_.
  static fiber::FiberType GetFiberType(const Fiber* fiber);

  static base::scheduling::Domain* GetFiberDomain(const Fiber* fiber);

  // Returns the current scheduler for a given fiber.
  static base::scheduling::Scheduler* GetScheduler(const Fiber* fiber);

  // Create a new child fiber in the tree of `parent`.
  static std::unique_ptr<Fiber> CreateChildFiber(
      Fiber* parent, absl::AnyInvocable<void() &&> invocable);
};

// An alarm which, once created, will fire at a specified time. When destroyed,
// will ensure that the alarm is either done running or will never run.
//
// Creating an alarm with an InfiniteFuture time is a no-op.
class OneShotAlarm {
 public:
  OneShotAlarm(absl::Time when, InvocableImpl invocable) {
    Create(buffer_.data(), when, std::move(invocable));
  }
  ~OneShotAlarm() { Destroy(buffer_.data()); }
  // OneShotAlarm is not copyable or movable.
  OneShotAlarm(const OneShotAlarm&) = delete;
  OneShotAlarm& operator=(const OneShotAlarm&) = delete;
  OneShotAlarm(OneShotAlarm&&) = delete;
  OneShotAlarm& operator=(OneShotAlarm&&) = delete;

 private:
  // Weak functions overridden in eventmanager_default.cc to prevent depending
  // on eventmanager where it isn't available.
  static void Create(void* buffer, absl::Time when, InvocableImpl invocable);
  static void Destroy(void* alarm);

  std::array<char, sizeof(TimedCall)> buffer_ alignas(TimedCall);
};

// Forward declaration of the type- implementation is only accessible in fiber.h
// due to dependence on constructing an instance.
class DynamicFiber;

// Allows the implementation to recognize when a non-default stack size is used.
// Clients may not query or depend on this.
extern const int32_t kDefaultFiberStackSize;

// Implementations for ChildFiberScoped.
absl::AnyInvocable<void() &&> InternalChildFiberScoped(
    const FiberOptions& options, absl::AnyInvocable<void() &&> invocable);
absl::AnyInvocable<void() &&> InternalChildFiberScoped(
    absl::AnyInvocable<void() &&> invocable);

}  // namespace internal

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_INTERNAL_H_
