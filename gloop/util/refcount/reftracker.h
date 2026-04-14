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

// RefTracker keeps track of the set of owners who hold references on
// some reference-counted object.  It is typically used by
// implementers of reference-counting (like util::ReferenceCounted),
// not by normal clients.
//
// RefTracker is thread-safe.

#ifndef THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFTRACKER_H_
#define THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFTRACKER_H_

#include <map>
#include <string>

#include "absl/synchronization/mutex.h"

namespace util {

class RefTracker {
 public:
  RefTracker();

  // This type is neither copyable nor movable.
  RefTracker(const RefTracker&) = delete;
  RefTracker& operator=(const RefTracker&) = delete;

  ~RefTracker();

  // Add owner to the set of tracked references.  The current stack
  // trace is also saved and will be printed by ListOwners.
  void Add(const void* owner);

  // Remove owner from the set of refs.
  // REQUIRES: owner is being tracked.
  void Remove(const void* owner);

  // Return a (possibly multi-line) string that contains the stack
  // traces of all existing reference holders.  Stores in *count
  // the number of owners listed in the report.
  std::string ListOwners(int* count) const;

 private:
  // Stack trace of a tracked reference
  static constexpr int kMaxDepth = 30;
  struct StackTrace {
    int count;       // How many holds does this owner have
    bool ambiguous;  // True if count ever become > 1.  In this case
                     // the stack trace listed here may be misleading.
    int depth;
    void* stack[kMaxDepth];
  };
  typedef std::map<const void*, StackTrace> OwnerMap;

  mutable absl::Mutex mu_;
  OwnerMap owners_;
};

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFTRACKER_H_
