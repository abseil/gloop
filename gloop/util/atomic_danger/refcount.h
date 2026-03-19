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

#ifndef THIRD_PARTY_GLOOP_UTIL_ATOMIC_DANGER_REFCOUNT_H_
#define THIRD_PARTY_GLOOP_UTIL_ATOMIC_DANGER_REFCOUNT_H_

#include <atomic>
#include <cassert>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"

namespace atomic_danger {

// RefCount is a low-level primitive that can be used to implement a scheme
// similar to std::shared_ptr<T>. It should be very rare that you want this
// class instead of std::shared_ptr<T>, or (less commonly) one of the tools in
// https://github.com/abseil/gloop/tree/main/gloop/util/refcount.
//
// This is not a general purpose atomic integer class!  The memory orderings
// chosen are as relaxed as possible for the semantics described above.  Using
// this class to perform any other kind of atomic counting is a mistake.
//
// This class is a thread safe reference count, typically a count of references
// to a separate object or resource (from here on, the "referenced object").
// Most typically, a reference is a pointer, but it could be a reference (&) or
// something else with similar properties. Except for the limited situations
// described below, this class does not provide thread safety or synchronization
// guarantees for any other object, including the reference itself (e.g. the
// pointer variable), and the referenced object.
//
// Formal Guarantees
// =================
// * The client must ensure that there's some one-to-one mapping of Dec() calls
//   to Inc() calls, such that the Dec() happens-after the corresponding Inc().
//   For these purposes, the constructor counts as an Inc() call.
// * RefCount guarantees that the Dec() call that returns true happens-after all
//   other Dec() calls on the RefCount. It is an error to call Inc() once Dec()
//   has returned true.
//
// Usage Instructions
// ==================
// * To create a new reference, the program should first call Inc(), and then
//   store a copy of reference it currently holds in a new place (e.g. assigning
//   a pointer to a new variable). This should be done before making the new
//   copy visible to another thread, or allowing another thread to overwrite the
//   old copy. This can be ensured by whatever synchronization mechanism
//   (usually an absl::Mutex) is preventing data races on the associated
//   pointers.
// * To release an existing reference, the caller should first ensure that it
//   will no longer use the reference (e.g. by storing null in its pointer
//   variable), then call Dec().
// * The return value of Dec() indicates whether the caller has just released
//   the last reference.  The caller may then delete the referenced object.
// * The return value of IsUnique() indicates whether the caller holds the
//   only reference.  If true, and if no other thread has access to the caller's
//   reference, the caller may assume no other thread has access to the object.
//   This can be used to avoid an expensive copy (see IsUnique() for more
//   information).
// * A common non-concurrency bug in roll-your-own reference count
//   implementations is to implement reference-counted assignment as:
//       1) Decrement reference count for the old value of the target, since we
//          are about to overwrite it.
//       2) Increment the reference count for the new value.
//       3) Perform the assignment.
//   This usually works, but fails badly if the user assigns a reference-counted
//   pointer to itself, as in `x = x;`. See b/28742000 for other common
//   mistakes.
//
// Because RefCount does not provide it, you must use other means to prevent
// races on the referenced object (and the references themselves, if necessary).
// The means are not important; all the usual approaches suffice. E.g. with a
// Mutex guarding the referenced object, by referring to immutable data, etc.
// This is no different than the thread safety requirements of a
// std::shared_ptr<T> and the T within. While simultaneous concurrent
// assignments of the same value to the same raw pointer are always illegal,
// simultaneous concurrent assignments of the same value to the same reference-
// counted pointers will definitely allow your program to corrupt the heap.
template <class IntType>
class RefCount final {
  static_assert(std::is_integral<IntType>::value,
                "RefCount must be instantiated with an integral type.");

 public:
  constexpr RefCount() : ref_count_(1) {}
  ~RefCount() = default;

  // RefCount is not copyable or moveable.
  RefCount(const RefCount&) = delete;
  RefCount(RefCount&&) = delete;
  RefCount& operator=(const RefCount&) = delete;
  RefCount& operator=(RefCount&&) = delete;

  // Obtains additional references, which belong to the caller.  The caller of
  // this method must logically hold one of the existing references. For
  // example, it is an error to call this after all existing references have
  // been released. `increment` must be positive.
  void Inc(IntType increment = 1);

  // Drops one or more references to this object.  Returns true if the final
  // reference has been released. This class can no longer be modified once this
  // returns true. `decrement` must be positive.
  ABSL_MUST_USE_RESULT bool Dec(IntType decrement = 1);

  // Returns true if and only if, for every Inc() call that happens-before C,
  // there is a Dec() call that happens-before C. The constructor does *not*
  // count as an Inc() call here; IsUnique() is a synchronization operation, so
  // if it returns true, some of those happens-before relationships might exist
  // only because it created them.
  //
  // This can be used to implement copy-on-write logic with an optimization that
  // avoids a copy if the caller is the sole owner of the instance.
  ABSL_MUST_USE_RESULT bool IsUnique() const;

  // Returns true if and only if all references have been released. This class
  // can no longer be modified if this returns true.
  ABSL_MUST_USE_RESULT bool IsZero() const;

  // Returns a string that can be used to log the current reference count. This
  // is intended for debug use only; writing program logic against the returned
  // value is an error.
  ABSL_MUST_USE_RESULT std::string DebugString() const;

 private:
  std::atomic<IntType> ref_count_;
};

//
// Implementation details follow.
//

template <class IntType>
void RefCount<IntType>::Inc(IntType increment) {
  assert(increment > 0);
#ifndef NDEBUG
  // In debug builds, use an acq_rel increment and check the return value of
  // fetch_add to detect calling Inc() after the refcount has become zero.
  // fetch_add returns the "old" value of ref_count_.
  assert(ref_count_.fetch_add(increment, std::memory_order_acq_rel) != 0);
#else
  ref_count_.fetch_add(increment, std::memory_order_relaxed);
#endif
}

template <class IntType>
bool RefCount<IntType>::Dec(IntType decrement) {
  assert(decrement > 0);
  // fetch_sub returns the "old" value of ref_count_.
  return ref_count_.fetch_sub(decrement, std::memory_order_acq_rel) ==
         decrement;
}

template <class IntType>
bool RefCount<IntType>::IsUnique() const {
  return ref_count_.load(std::memory_order_acquire) == 1;
}

template <class IntType>
bool RefCount<IntType>::IsZero() const {
  return ref_count_.load(std::memory_order_acquire) == 0;
}

template <class IntType>
std::string RefCount<IntType>::DebugString() const {
  return absl::StrCat(ref_count_.load(std::memory_order_relaxed));
}

}  // namespace atomic_danger

#endif  // THIRD_PARTY_GLOOP_UTIL_ATOMIC_DANGER_REFCOUNT_H_
