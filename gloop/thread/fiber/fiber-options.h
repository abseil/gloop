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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_OPTIONS_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_OPTIONS_H_

#include <cstddef>
#include <optional>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "gloop/base/context.h"

namespace base {
namespace scheduling {
class Scheduler;
}  // namespace scheduling
}  // namespace base

namespace thread {

// Options controlling the behavior of a newly created Fiber.
class FiberOptions {
 public:
  // Always provide a default constructor.  Currently there are no parameters.
  FiberOptions() = default;

  // The name of the fiber that will be / has been created using these
  // FiberOptions. Fiber names may contain only the characters a-z, A-Z, 0-9,
  // -, and _, and may not start with a digit.
  //
  // Note: fiber names are inherited, unless explicitly set/changed during
  // child fiber creation (helps to tag fiber trees with e.g. IDs of requests
  // they are processing).
  //
  // Note: fiber names are exposed via /fiberz, so DO NOT put PII in there.
  absl::string_view name() const;

  // Set fiber name: the name will be interned.
  //
  // Note: interned strings are never freed. If an application creates
  // an unlimited number of unique fiber names, the application will OOM.
  FiberOptions& SetInternedName(absl::string_view name);

  // Not calling this method or setting to zero, will cause Fibers to use the
  // default stack size set by fibers_default_thread_stack_size. The bounds
  // follow (in bytes):
  // Linux: [2^12*, 2^30]
  // Fuchsia: [2^12*, 2^16]
  // Apple: [2^12*, 2^16]
  // Android: [2^12*, 2^16]
  // * in dbg mode, the minimum is set to 2^13. Requests lower than this are
  // silently rounded up.
  FiberOptions& SetStackSize(size_t stack_size);
  size_t GetStackSize() const;

 private:
  const char* name_ = nullptr;  // strings::ArenaString encoded.
  size_t stack_size_ = 0;
};

// Options controlling the behavior of the tree parented by a new root fiber.
// Default values are described in the field descriptions.

class TreeOptions {
 public:
  // Set the FiberOptions which will be used to create the root fiber associated
  // with this tree.
  // DEFAULT: FiberOptions()
  TreeOptions& set_fiber_options(FiberOptions options) & {
    fiber_options_ = options;
    return *this;
  }
  TreeOptions&& set_fiber_options(FiberOptions options) && {
    fiber_options_ = options;
    return std::move(*this);
  }
  const FiberOptions& fiber_options() const { return fiber_options_; }

  // Specify the base::Context for this root fiber to execute within; by
  // default, new root fibers run within a new (empty) Context. Copies of this
  // Context are inherited by children unless an alternate Context was active
  // at their point of creation.
  //
  // To create a new root fiber within a copy of the current environmental
  // context use:
  //  set_context(base::CurrentContext())
  //
  // If this function is not called, context() returns an empty Context.
  TreeOptions& set_context(base::Context context) & {
    context_ = std::move(context);
    return *this;
  }
  TreeOptions&& set_context(base::Context context) && {
    context_ = std::move(context);
    return std::move(*this);
  }

  // Returns a reference to the optional context for this instance.
  // Tree options have no default context, i.e.; it defaults to std::nullopt.
  const base::Context* absl_nullable context() const {
    return context_.has_value() ? &*context_ : nullptr;
  }

  //----------------------------------------------------------------------------
  // Scheduler-specific options.
  //----------------------------------------------------------------------------
  // All fibers belonging to a fiber-tree (including the root) are coordinated
  // by a single scheduler.  While this scheduler may be explicitly instantiated
  // and passed using "set_scheduler"; most callers will prefer the
  // implementation default.  The behavior and location of the default scheduler
  // may also be tuned below.

  // Maximum Number of CPU Slots:
  // Defines the maximum number of CPU slots that will be used for the created
  // scheduler.  This is how many fibers from this tree may be *simultaneously*
  // executing.  Note that this does not limit the number of fibers that are
  // in the *midst* of execution.  When a fiber blocks, control transfers to
  // another fiber.  E.g., if each of your fibers sends a blocking RPC, you
  // will not limit the number of simultaneous RPCs by setting this value.
  //
  // The actual parallelism cannot exceed parent_scheduler parallelism. If set
  // above parent_scheduler()->num_slots() the value will be reduced to match
  // parent.
  //
  // When unspecified, this will be managed by the implementation.  The current
  // default is to schedule requests onto a single cpu (e.g. equivalent to
  // max_cpu_slots == 1).  This provides isolation between requests and
  // increases cache locality. Note: This behavior is subject to change.  Code
  // that depends on a specific value for max_cpu_slots is forbidden.
  //
  // DEFAULT: 0, The implementation will completely manage the number of CPUs.
  int max_cpu_slots() const { return max_cpu_slots_; }

  // REQUIRES: max_cpu_slots >= 0
  // REQUIRES: scheduler() == nullptr
  TreeOptions& set_max_cpu_slots(int max_cpu_slots) & {
    max_cpu_slots_ = max_cpu_slots;
    return *this;
  }
  TreeOptions&& set_max_cpu_slots(int max_cpu_slots) && {
    max_cpu_slots_ = max_cpu_slots;
    return std::move(*this);
  }

  // Specifies the *parent* scheduler that the scheduler created for this tree
  // will be attached to.
  //
  // This is most commonly useful when combined with a parent scheduler that
  // performs some sort of admission control.  E.g. ArrivalOrderScheduler.
  // DEFAULT: nullptr [thread::DefaultDomain()->root_scheduler() will be used.]
  base::scheduling::Scheduler* parent_scheduler() const {
    return parent_scheduler_;
  }
  // REQUIRES: scheduler() == nullptr
  TreeOptions& set_parent_scheduler(
      base::scheduling::Scheduler* parent_scheduler) {
    parent_scheduler_ = parent_scheduler;
    return *this;
  }

  // Explicitly specify a Scheduler which will coordinate all fibers in this
  // tree.  This supersedes all above scheduler options: the default scheduler
  // will not be used.
  //
  // Lifetime: Callers are responsible for ensuring Orphan() is
  // called on the passed scheduler.  The root fiber associated with this tree
  // will hold a Ref() for the duration of its life, taken during
  // Detach()/NewTree().  Best practice is to call Orphan() immediately after
  // the associated fiber-tree has been created.
  //
  // CAUTION:  Fiber trees and Schedulers should exist in a 1-to-1 relationship.
  // It is almost *always* a misconfiguration to pass the same Scheduler to more
  // than one Fiber-tree as it limits the ability to co-schedule related work.
  // Be careful to re-initialize scheduler() when re-using TreeOptions.
  //
  // DEFAULT: nullptr [an implementation maintained scheduler will be created]
  base::scheduling::Scheduler* scheduler() const { return scheduler_; }
  // REQUIRES: No other scheduler-specific options may be set.
  // REQUIRES: Should be re-initialized if *this (TreeOptions) is reused.
  TreeOptions& set_scheduler(base::scheduling::Scheduler* scheduler) & {
    scheduler_ = scheduler;
    return *this;
  }
  TreeOptions&& set_scheduler(base::scheduling::Scheduler* scheduler) && {
    scheduler_ = scheduler;
    return std::move(*this);
  }

  // Parallelism:
  // Defines the maximum parallelism that will be used for the created
  // scheduler.
  ABSL_DEPRECATE_AND_INLINE()
  int parallelism() const { return max_cpu_slots(); }
  ABSL_DEPRECATE_AND_INLINE()
  TreeOptions& set_parallelism(int parallelism) & {
    return set_max_cpu_slots(parallelism);
  }
  ABSL_DEPRECATE_AND_INLINE()
  TreeOptions&& set_parallelism(int parallelism) && {
    return std::move(set_max_cpu_slots(parallelism));
  }

 private:
  friend class Fiber;

  // Consumes (extracts) or copies the context from this TreeOptions instance.
  // This function has special logic to propagate privacy context if no explicit
  // context was set using `set_context`.
  base::Context copy_context(const void* fiber) const;
  base::Context consume_context(const void* fiber);

  FiberOptions fiber_options_;
  int max_cpu_slots_ = 0;
  base::scheduling::Scheduler* parent_scheduler_ = nullptr;
  base::scheduling::Scheduler* scheduler_ = nullptr;
  std::optional<base::Context> context_;
};

}  // namespace thread
#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_FIBER_OPTIONS_H_
