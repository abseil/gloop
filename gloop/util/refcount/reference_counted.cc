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

#include "gloop/util/refcount/reference_counted.h"

#include <atomic>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"

namespace util {

ReferenceCounted::ReferenceCounted() : tracked_(nullptr), ref_(1) {}

ReferenceCounted::ReferenceCounted(ReferenceCountedType type, const void* owner)
    : tracked_((type != UNTRACKED) ? new TrackedState(type) : nullptr),
      ref_(1) {
  if (type != UNTRACKED) {
    tracked_->owners.Add(owner);
  }
}

ReferenceCounted::~ReferenceCounted() {
  DCHECK_EQ(ref_.load(std::memory_order_relaxed), 0);
  delete tracked_;
}

void ReferenceCounted::Ref() const {
  DCHECK_GE(ref_.load(std::memory_order_relaxed), 1);
  ref_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(type() != TRACKED_STRICT) << "owner required for strict tracking";
}

bool ReferenceCounted::Unref() const {
  DCHECK_GT(ref_.load(std::memory_order_relaxed), 0);
  DCHECK(type() != TRACKED_STRICT) << "owner required for strict tracking";
  // If ref_==1, this object is owned only by the caller. Bypass a locked op
  // in that case.
  if (ref_.load(std::memory_order_acquire) == 1 ||
      ref_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
    // Make DCHECK in ~ReferenceCounted happy
    DCHECK((ref_.store(0, std::memory_order_relaxed), true));
    delete this;
    return true;
  } else {
    return false;
  }
}

void ReferenceCounted::RefFor(const void* owner) const {
  DCHECK_GE(ref_.load(std::memory_order_relaxed), 1);
  ref_.fetch_add(1, std::memory_order_relaxed);
  if (type() != UNTRACKED) {
    tracked_->owners.Add(owner);
  }
}

bool ReferenceCounted::UnrefFor(const void* owner) const {
  DCHECK_GT(ref_.load(std::memory_order_relaxed), 0);
  if (type() != UNTRACKED) {
    tracked_->owners.Remove(owner);
  }
  // If ref_==1, this object is owned only by the caller. Bypass a locked op
  // in that case.
  if (ref_.load(std::memory_order_acquire) == 1 ||
      ref_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
    // Make DCHECK in ~ReferenceCounted happy
    DCHECK((ref_.store(0, std::memory_order_relaxed), true));
    delete this;
    return true;
  } else {
    return false;
  }
}

std::string ReferenceCounted::ListOwners() const {
  std::string result;
  int reported = 0;
  int total = ref_.load(std::memory_order_relaxed);
  if (type() != UNTRACKED) {
    result = tracked_->owners.ListOwners(&reported);
  }
  if (total != reported) {
    absl::StrAppendFormat(&result, "held %d times by untracked owners",
                          total - reported);
  }
  return result;
}

bool ReferenceCounted::RefIsUnique() const {
  return ref_.load(std::memory_order_acquire) == 1;
}

}  // namespace util
