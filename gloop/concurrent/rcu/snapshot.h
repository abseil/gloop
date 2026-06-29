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

// A Snapshot<T> provides a view of an item from a concurrent data structure.
// That item will be safe to access for the lifetime of all Snapshots pointing
// at it.
//
// Note: It is not thread-safe to use a pointer or view (such as
// std::string_view) referencing internal data after the lifetime of its
// originating Snapshot has ended. When the Snapshot goes out of scope, its RCU
// read lock is released, making subsequent concurrent accesses vulnerable to
// data races or use-after-free errors.
//
// While this is implemented via unique_ptr, this is not a hard guarantee; do
// not look behind the typedef, or use release/reset.  Simply treat this as a
// smart pointer as follows:
//
//   struct Foo {
//     int x;
//     std::string s;
//   };
//   View<Foo> view;
//
//   // Safe: keep Snapshot alive while accessing derived views or pointers.
//   Snapshot<const Foo> snapshot = view.Get();
//   if (snapshot == nullptr) return;
//   Foo& reference = *snapshot;
//   Foo* pointer = snapshot.get();
//   int x = snapshot->x;
//   std::string_view sv = snapshot->s;  // Safe while `snapshot` lives.
//
//   // Not thread-safe: temporary Snapshot drops RCU lock at statement end.
//   std::string_view bad_sv = view.Get()->s;  // Not thread-safe!
//   Foo* bad_ptr = view.Get().get();          // Not thread-safe!
//
// Any other expressions are outside the contract of Snapshot and are subject to
// change or removal in the future.

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_SNAPSHOT_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_SNAPSHOT_H_

#include <memory>

#include "gloop/concurrent/rcu/snapshot_rcu_deleter.h"

namespace rcu {

template <typename T>
using Snapshot = std::unique_ptr<T, ::rcu::internal::RcuDeleter>;

}  // namespace rcu

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_SNAPSHOT_H_
