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

#ifndef THIRD_PARTY_GLOOP_UTIL_REFCOUNT_COMPACT_REFERENCE_COUNTED_H_
#define THIRD_PARTY_GLOOP_UTIL_REFCOUNT_COMPACT_REFERENCE_COUNTED_H_

#include <atomic>
#include <cstdint>

#include "gloop/util/refcount/reffed_ptr.h"

namespace refcount {

// A base class for intrusively reference counted objects.
//
// Publicly inherit from CompactReferenceCounted to obtain a type with Ref and
// Unref methods:
//
//     class MyRefCounted final : public CompactReferenceCounted<MyRefCounted> {
//       // ...
//     };
//
// The best way to manage the lifetime of such an object is to create it with
// refcount::MakeReffed and pass around references using refcount::reffed_ptr.
// These will automatically do the right thing.
//
// The destructor of the type in the first template parameter will be called
// when the reference count hits zero. This means that type must either not have
// subclasses or must have a virtual destructor, just as with std::unique_ptr.
// Marking the class final ensures the former.
//
// ---
//
// Note that modern types should avoid intrusive reference counting unless both
// of the following are true:
//
// *   A reference to the type always needs to be shared by users, without some
//     natural lifetime associated with some other object.
//
//     If you don't need to reference semantics, use value semantics. If the
//     type has some natural owner, own it there instead. If it doesn't always
//     need to be shared, leave it up to the user whether to associate a
//     reference count or not.
//
// *   References will be acquired and released in a very hot path.
//
//     If you need shared ownership but won't manipulate the reference count on
//     a hot path, use std::shared_ptr instead.
//
template <typename Derived, typename RefcountT = int32_t>
class CompactReferenceCounted {
 public:
  CompactReferenceCounted() : ref_count_(1) {}

  CompactReferenceCounted(const CompactReferenceCounted&) = delete;
  CompactReferenceCounted(CompactReferenceCounted&&) = delete;
  CompactReferenceCounted& operator=(const CompactReferenceCounted&) = delete;
  CompactReferenceCounted& operator=(CompactReferenceCounted&&) = delete;

  // Increments the reference count by one.
  void Ref() const { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  // Decrements the reference count by one. Invokes `OnRefCountIsZero` and
  // returns true if the reference count dropped to zero.
  bool Unref() const {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
      AsDerived()->OnRefCountIsZero();
      return true;
    }
    return false;
  }

  bool RefCountIsOne() const {
    return ref_count_.load(std::memory_order_acquire) == 1;
  }

  // Creates a `reffed_ptr` with a new reference to `this`.
  reffed_ptr<Derived> TakeRef() {
    return WrapReffed(AsDerived(), RefTakingMode::kCreate);
  }
  // As above, but const.
  reffed_ptr<const Derived> TakeRef() const {
    return WrapReffed(AsDerived(), RefTakingMode::kCreate);
  }

 protected:
  ~CompactReferenceCounted() = default;

  // `Derived` can provide its own `OnRefCountIsZero` method to customize the
  // behaviour when the refcount reaches zero. Otherwise the default behaviour
  // is to delete `this`.
  void OnRefCountIsZero() const { delete AsDerived(); }

  const RefcountT ref_count() const {
    return ref_count_.load(std::memory_order_acquire);
  }

 private:
  Derived* AsDerived() { return static_cast<Derived*>(this); }
  const Derived* AsDerived() const { return static_cast<const Derived*>(this); }

  mutable std::atomic<RefcountT> ref_count_;
};

}  // namespace refcount

#endif  // THIRD_PARTY_GLOOP_UTIL_REFCOUNT_COMPACT_REFERENCE_COUNTED_H_
