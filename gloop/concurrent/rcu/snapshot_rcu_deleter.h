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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_SNAPSHOT_RCU_DELETER_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_SNAPSHOT_RCU_DELETER_H_

#include "absl/base/thread_annotations.h"
#include "gloop/concurrent/rcu/global_domain.h"
#include "gloop/concurrent/rcu/rcu.h"

namespace rcu::internal {

// This is a deleter suitable for passing to unique_ptr for rcu-protected
// pointers--which should _not_ be deleted, but for which we have to
// inform RCU that a reader lock is being dropped
class RcuDeleter {
 public:
  // only creatable via blessed data structures
  RcuDeleter() = delete;

  // These blithly copy/move and ignore the Token.  We rely on
  // unique_ptr/shared_ptr to only invoke the operator() exactly once
  // per our creation of a rcudeleter, which maintains lock discipline.
  RcuDeleter(const RcuDeleter& rhs) = default;
  RcuDeleter& operator=(const RcuDeleter& rhs) = default;
  RcuDeleter(RcuDeleter&& rhs) = default;
  RcuDeleter& operator=(RcuDeleter&& rhs) = default;
  ~RcuDeleter() = default;

  // we don't care what the pointer is, we just drop the lock.
  template <typename T>
  void operator()(T* t) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    // unique_ptr skips the Deleter if the pointer is null, so we
    // don't bother holding a real ReaderLock in that case (which is
    // fine; nothing to protect.)  However, shared_ptr does _not_ do
    // this.  So (in case we got turned into a shared_ptr), we have to
    // check if there's a value to protect (and thus a real lock to
    // drop.)
    if (t == nullptr) return;
    GlobalDomain::d.ReaderUnlock(token_);
  }

 private:
  explicit RcuDeleter(::base::rcu::Token token)
      ABSL_UNLOCK_FUNCTION(GlobalDomain::d)
      : token_(token) {}
  ::base::rcu::Token token_;
  friend class RcuDeleterAccess;
};

}  // namespace rcu::internal

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_SNAPSHOT_RCU_DELETER_H_
