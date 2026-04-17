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

#ifndef THIRD_PARTY_GLOOP_BASE_REFERENCE_TRACKER_H_
#define THIRD_PARTY_GLOOP_BASE_REFERENCE_TRACKER_H_

#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "gloop/base/spinlock.h"

namespace base {

// A utility class to keep track of which objects hold references to another
// object. We keep track of the stack traces for each reference holder.  This
// makes it easy to track down leaked references.  Each object to be tracked
// should have a ReferenceTracker member.
//
// ReferenceTracker is thread-safe.
class ReferenceTracker {
 public:
  using StackTrace = std::vector<const void*>;

  ReferenceTracker() = default;
  ~ReferenceTracker() = default;

  // Call this when an object 'owner' is about to add a reference to this
  // table. A call to Ref() must be matched with Unref() with the same 'owner'.
  //
  // 'owner' is the owner of this reference.  It must be unique across all
  // outstanding references to this tracker -- you may not call Ref() with the
  // same owner twice (unless you Unref() it first).
  void Ref(const void* owner);

  /// The same value passed to Ref should be passed here.
  void Unref(const void* owner);

  // GetReferenceTraces() returns the stack backtrace at the moment of Ref() for
  // each outstanding reference. This feature is used to diagnose refcnt leaks.
  // The stack traces are pushed onto the back of 'traces'.  Each stack trace is
  // a vector of PC addresses, where the caller of Ref() (the youngest stack
  // frame) is in trace[0].
  void GetReferenceTraces(std::vector<StackTrace>* traces) const;

 private:
  mutable SpinLock mu_;  // protects traces_;

  using OwnerStackTraceMap = absl::flat_hash_map<const void*, StackTrace>;
  // Maps owner -> stack backtrace at the moment of RegisterRef().
  OwnerStackTraceMap traces_ ABSL_GUARDED_BY(mu_);

  ReferenceTracker(const ReferenceTracker&) = delete;
  ReferenceTracker& operator=(const ReferenceTracker&) = delete;
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_REFERENCE_TRACKER_H_
