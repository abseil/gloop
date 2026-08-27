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

// A simple to use class for commonly read, rarely updated values. Owns an
// object of type T, which can be updated with new values (disposing of the old
// safely.)  Any number of parallel readers can get Snapshots, which, for their
// lifetime, provide a view of the stored object at time of creation; no
// synchronization is needed between readers of a Snapshot and updaters of the
// View that created it.
//
// Essentially, if we have:
//
//   View<T> view;
//   Snapshot<const T> s = view.Get();
//
// then s is a smart pointer to a T object owned by the View, and that object
// will not be destroyed so long as s points to it. (Because garbage collection
// happens in the background, the object may outlive all snapshots to it, and
// even the view itself.)
//
// Creating and destroying a Snapshot is faster than incrementing/decrementing a
// reference count, but correspondingly less safe: seemingly-innocuous misuses
// of Snapshots can result in unexpected memory bloat, and even unbounded memory
// leaks, across the whole program; see the comments on Get() for details.

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_VIEW_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_VIEW_H_

#include <cstddef>
#include <memory>
#include <type_traits>

#include "gloop/concurrent/rcu/global_domain.h"
#include "gloop/concurrent/rcu/internal.h"  // IWYU pragma: export
#include "gloop/concurrent/rcu/policy.h"    // IWYU pragma: export
#include "gloop/concurrent/rcu/rcu.h"       // IWYU pragma: export
#include "gloop/concurrent/rcu/snapshot.h"  // IWYU pragma: export

namespace rcu {

template <typename ArgT,
          typename Deleter = std::default_delete<typename raw_type<ArgT>::type>>
class View {
 public:
  typedef typename raw_type<ArgT>::type T;
  typedef typename thread_safe_type<ArgT>::type SafeT;

  static_assert(std::is_empty<Deleter>::value, "Deleter type must be empty.");

 private:
  template <typename... TT>
  using enable_if_constructible_from_t =
      typename std::enable_if<std::is_constructible<T, TT...>::value>::type;

 public:
  View() : View(nullptr) {}
  // It is safe to destroy rcu::View even if there are still live Snapshots
  // constructed from this View. The values pointed to by those Snapshots will
  // still be safe to access as long as the Snapshots are live.
  ~View();
  explicit View(std::unique_ptr<T, Deleter> t) : View(t.release()) {}
  explicit View(std::nullptr_t) : View(static_cast<T*>(nullptr)) {}

  template <typename... TT, typename = enable_if_constructible_from_t<TT...>>
  explicit View(TT&&... t) : View(new T(std::forward<TT>(t)...)) {}

  View(const View& rhs) = delete;
  View(View&& rhs) = delete;
  View& operator=(const View& rhs) = delete;
  View& operator=(View&& rhs) = delete;

  // Replaces the current value of the cell with <t>, letting the background
  // garbage collection thread dispose of the old value once there are no more
  // Snapshots pointing to it.
  void Update(std::unique_ptr<T, Deleter> t) {
    internal::Dispose<T, Deleter>(v_.ReplaceUnsynchronized(t.release()));
  }

  void Update(std::nullptr_t) { Update(std::unique_ptr<T, Deleter>(nullptr)); }

  template <typename... TT, typename = enable_if_constructible_from_t<TT...>>
  void Update(TT&&... t) {
    Update(std::unique_ptr<T, Deleter>(new T(std::forward<TT>(t)...)));
  }

  // Atomically: if the view contains <old>, Update(t) and return true
  // (this moves the contents out of t.)  Otherwise return false,
  // without modifying t.
  // Style waiver for use of rvalue references was granted in cl/165008371
  bool TryUpdate(const Snapshot<SafeT>& old, std::unique_ptr<T, Deleter>&& t);

  // Returns a handle providing access to the latest T; that T is
  // guaranteed to exist as long as any Snapshots point at it. This function
  // will not block.
  //
  // WARNING: a T value stored in a View will often not be reclaimed until after
  // the destruction of every Snapshot for any View *anywhere in the program*
  // that was created before the Update call that replaced that value.
  // Consequently, the overall memory consumption goes up roughly linearly with
  // the age of the oldest living Snapshot globally.
  //
  // This has an even more surprising corollary: if a T object managed by View
  // directly or indirectly owns a Snapshot<U> object (for any type U), that
  // can permanently block memory reclamation for all View objects in the
  // program, resulting in an unbounded memory leak.
  //
  // Consequently, Snapshots should be used only as local variables, not as
  // class members, and the lifetime of those local variables should be kept
  // as short as possible.
  Snapshot<SafeT> Get() const;

  // Returns true if this View contains a nullptr.
  //
  // This is significantly less expensive than calling Get() and checking the
  // result, since no RCU critical section is established. This may be used as a
  // "fast path" optimization in the case where the caller expects a View to
  // contain nullptr in the common case. For example, something like:
  //
  // bool IsAccessAllowed(...) {
  //   if (ABSL_PREDICT_TRUE(my_usually_empty_denylist_.IsNull())) {
  //     return true;
  //   }
  //
  //   auto snapshot = my_usually_empty_denylist_.Get();
  //
  //   // The following check is necessary when the view can be updated
  //   // to a nullptr between the IsNull() check and the Get() call. It's
  //   // not necessary if the view never contains a null value again after
  //   // being made non-null once, i.e. Update is never called with null.
  //   if (ABSL_PREDICT_FALSE(snapshot == nullptr)) {
  //     return true;
  //   }
  //
  //   return !snapshot->Contains(...);
  // }
  //
  // As of Q1 2021, this is ~11x faster than checking the result of Get()..
  bool IsNull() const;

 private:
  ::base::rcu::Value<T> v_;
  explicit View(T* t) : v_(t, &internal::GlobalDomain::d) {
    ::base::rcu::Domain::EnableCleanup();
  }
};

// IMPLEMENTATION DETAILS BELOW

template <typename ArgT, typename Deleter>
inline View<ArgT, Deleter>::~View() {
  internal::Dispose<T, Deleter>(v_.ReplaceUnsynchronized(nullptr));
}

template <typename ArgT, typename Deleter>
inline auto View<ArgT, Deleter>::Get() const -> Snapshot<SafeT> {
  auto token = internal::GlobalDomain::d.ReaderLock();
  T* t = const_cast<T*>(v_.Get(&internal::GlobalDomain::d));
  return internal::MakeSnapshot(t, token);
}

template <typename ArgT, typename Deleter>
inline bool View<ArgT, Deleter>::IsNull() const {
  return v_.IsNull(&internal::GlobalDomain::d);
}

template <typename ArgT, typename Deleter>
inline bool View<ArgT, Deleter>::TryUpdate(const Snapshot<SafeT>& old,
                                           std::unique_ptr<T, Deleter>&& t) {
  T* nv = t.get();
  const T* ov = old.get();
  if (!v_.TryReplaceUnsynchronized(ov, nv)) {
    return false;
  }

  internal::Dispose<T, Deleter>(const_cast<T*>(ov));
  t.release();
  return true;
}

}  // namespace rcu

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_VIEW_H_
