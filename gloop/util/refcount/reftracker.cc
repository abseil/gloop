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

#include "gloop/util/refcount/reftracker.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/debugging/stacktrace.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/examine_stack.h"

namespace util {

RefTracker::RefTracker() = default;

RefTracker::~RefTracker() = default;

void RefTracker::Add(const void* owner) {
  StackTrace trace;
  trace.count = 1;
  trace.ambiguous = false;
  trace.depth = absl::GetStackTrace(trace.stack, kMaxDepth,
                                    1);  // Skip this routine
  absl::MutexLock l(mu_);
  std::pair<OwnerMap::iterator, bool> p =
      owners_.insert(std::make_pair(owner, trace));
  if (!p.second) {
    // Owner already registered
    StackTrace* existing = &p.first->second;
    DCHECK_GE(existing->count, 1);
    existing->ambiguous = true;
    existing->count++;
  }
}

void RefTracker::Remove(const void* owner) {
  absl::MutexLock l(mu_);
  OwnerMap::iterator it = owners_.find(owner);
  DCHECK(it != owners_.end()) << " owner not found in tracked refs";
  if (it != owners_.end()) {
    StackTrace* t = &it->second;
    DCHECK_GE(t->count, 1);
    t->count--;
    if (t->count < 1) {
      owners_.erase(it);
    }
  }
}

std::string RefTracker::ListOwners(int* count) const {
  std::string result;
  *count = 0;
  typedef std::vector<std::pair<const void*, StackTrace> > OwnerVec;
  OwnerVec owners;
  {
    absl::ReaderMutexLock l(mu_);
    owners.resize(owners_.size());
    std::copy(owners_.begin(), owners_.end(), owners.begin());
  }

  // Symbolization is slow.  Do it without holding tracked_->mu_.
  for (int i = 0; i < owners.size(); i++) {
    const OwnerVec::value_type& owner = owners[i];
    const StackTrace& t = owner.second;
    absl::StrAppendFormat(&result, "===== owner %p =====\n", owner.first);
    if (t.ambiguous) {
      result += "reported stack trace may be incorrect (ambiguous owner)\n";
    }
    if (t.count != 1) {
      absl::StrAppendFormat(&result, "currently held %d times by owner\n",
                            t.count);
    }

    DumpPCAndStackTrace(nullptr, const_cast<void**>(t.stack), t.depth,
                        &DebugWriteToString, &result);
    *count += t.count;
  }
  return result;
}

}  // namespace util
