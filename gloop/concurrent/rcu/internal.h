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

// Bits and pieces to support View/Snapshot based interfaces.  Mostly
// uninteresting unless you're writing such a data structure in this
// directory.
//
// Usable things here:
// - MakeSnapshot() (binds a pointer and a rcu lock token protecting it
//                   into a self-protecting RAII type)
// - Dispose() (Get rid of old values protected by snapshots)
#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_INTERNAL_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_INTERNAL_H_

#include <memory>

#include "absl/base/thread_annotations.h"
#include "gloop/concurrent/rcu/global_domain.h"
#include "gloop/concurrent/rcu/rcu.h"
#include "gloop/concurrent/rcu/snapshot.h"
#include "gloop/concurrent/rcu/snapshot_rcu_deleter.h"

namespace rcu::internal {

template <typename T, typename Deleter = std::default_delete<T>>
inline void Dispose(T* t) {
  if (t != nullptr) {
    // Casting to const void* allows for T to be const-qualified.
    void* p = const_cast<void*>(static_cast<const void*>(t));
    internal::GlobalDomain::d.CallRaw(
        +[](void* t) { Deleter()(static_cast<T*>(t)); }, p);
  }
}

// Access controls for RcuDeleter; only functions below are allowed.
class RcuDeleterAccess {
  template <typename T>
  friend Snapshot<T> MakeSnapshot(T* t, ::base::rcu::Token token);
  template <typename T>
  friend Snapshot<T> NullSnapshot();
  static RcuDeleter Create(::base::rcu::Token token)
      ABSL_NO_THREAD_SAFETY_ANALYSIS {
    // Thread safety analysis is disabled here for the same reasons as below in
    // MakeSnapshot.
    return RcuDeleter(token);
  }
};

template <typename T>
inline Snapshot<T> MakeSnapshot(T* t, ::base::rcu::Token token)
    ABSL_UNLOCK_FUNCTION(GlobalDomain::d) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // Okay, this violates nominal rules for RCU (as the need to disable
  // annotalysis should suggest!)  In particular, this reader lock
  // might get dropped by a different thread.  However, that rule
  // isn't actually necessary, but we put it in for conservatism
  // towards future implementations.  I (ahh, author of both)
  // guarantee this is in fact safe, and will fix this before changing
  // RCU in such a way that the reader lock rules might be necesary.
  if (t == nullptr) internal::GlobalDomain::d.ReaderUnlock(token);
  return Snapshot<T>(t, RcuDeleterAccess::Create(token));
}

// Returns a valid snapshot of nullptr, protected by a non-existent token.
// Exists because Snapshots aren't default constructible, but we need to do:
//
// Snapshot s;
// {
//    RAII stuff here;
//    s = MakeSnapshot(...);
// }
// return s;
//
// Should compile to zeroing a few registers (and a branch when we re-assign s.)
template <typename T>
inline Snapshot<T> NullSnapshot() {
  auto token = ::base::rcu::DummyToken();
  return Snapshot<T>(nullptr, RcuDeleterAccess::Create(token));
}

}  // namespace rcu::internal

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_INTERNAL_H_
