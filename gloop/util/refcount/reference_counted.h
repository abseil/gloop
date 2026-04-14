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

// Base class for explicitly reference-counted objects.
//
// A class T that wants reference-counting should inherit from
// ReferenceCounted.  Reference counts can be adjusted by calling
// either Ref/Unref or RefFor/UnrefFor.
//
// The initial reference count for a new object is one, and therefore
// there should always be one more unref calls than ref calls by the
// time an object is no longer needed.
//
// Note: this module provides "EXPLICIT" reference counting where the
// application code has to write down Ref/RefFor and Unref/UnrefFor
// calls.
//
// Various template classes like shared_ptr<> in util/gtl implement
// "IMPLICIT" reference counting by making assignment and copy
// operators do automatic refs/unrefs.
//
// Explicit reference counting, as provided by this module, is more
// error-prone.  But on the positive side, it is less intrusive (types
// do not have to be wrapped in one of many possible smart pointer
// classes), avoids operator overloading, and also exposes lifetime
// issues to somebody reading the code, which seems good, especially
// for objects with complicated behavior.
//
// Tracking support
// ----------------
// When a ReferenceCounted object is created, you can cause it to
// start tracking who is holding references by supplying a special
// constructor argument.  When tracking is enabled, the object
// remembers the current stack trace whenever RefFor() is called and
// forgets this stack trace when a corresponding UnrefFor() call is
// made.  To make this work, the RefFor() and UnrefFor() calls must be
// passed the same "owner" argument so that the ReferenceCounted
// implementation can figure out which RefFor() call should be
// forgotten on a particular UnrefFor() call.
//
// Thread safety
// -------------
// ReferenceCounted is thread-safe.

#ifndef THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFERENCE_COUNTED_H_
#define THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFERENCE_COUNTED_H_

#include <atomic>
#include <cstdint>
#include <string>

#include "absl/base/dynamic_annotations.h"
#include "gloop/util/refcount/reftracker.h"

namespace util {

// ReferenceCounted objects come in various flavors:
enum ReferenceCountedType {
  // No stack traces are tracked on ref/unref.  This is the fastest mode.
  // If you're unconditionally using this mode, consider using
  // `SimpleReferenceCounted`.
  UNTRACKED,

  // References are tracked on RefFor()/UnrefFor() calls.
  // It is ok to call the Ref()/Unref() methods, but
  // make sure that RefFor() is always paired with an UnrefFor()
  // and a Ref() is always paired with an Unref().
  TRACKED,

  // References are tracked on RefFor()/UnrefFor() calls that are
  // passed "owner" arguments.  Calling the argument-free
  // Ref()/Unref() methods is forbidden and causes crashes in debug
  // mode.
  TRACKED_STRICT,
};

// A less-efficient version of CompactReferenceCounted that provides features
// around tracking of owners. Do not use this in new code unless you absolutely
// must have those features.
class ReferenceCounted {
 public:
  // Initial reference count is one.  Mode is UNTRACKED.
  ReferenceCounted();

  // Initial reference count is one.  The reference belongs to "owner".
  // The corresponding unref should be UnrefFor(owner).
  ReferenceCounted(ReferenceCountedType type, const void* owner);

  // This type is neither copyable nor movable.
  ReferenceCounted(const ReferenceCounted&) = delete;
  ReferenceCounted& operator=(const ReferenceCounted&) = delete;

  // Increments reference count by one on behalf of the specified
  // owner.  The caller should call UnrefFor(owner) when this ref is
  // no longer needed.
  //
  // The same owner should not hold multiple concurrent references to
  // the same object.  Otherwise, the tracking code can get confused
  // and can report spurious owners.
  //
  // If you use RefFor(owner) to acquire a reference, the
  // corresponding unref must be done using UnrefFor(owner).
  // Otherwise, this code may use an unbounded amount of memory.
  //
  // The owner should typically be the object that contains
  // a pointer to this, or some other convenient object that is
  // guaranteed to outlive this.
  void RefFor(const void* owner) const;

  // Decrements reference count by one on behalf of the specified
  // owner.  If the count remains positive, returns false.  When the
  // count reaches zero, returns true and deletes this, in which case
  // the caller must not access the object afterward.
  // REQUIRES: RefFor(owner) was used to acquire this reference.
  bool UnrefFor(const void* owner) const;

  // Increments reference count by one.
  // Callers should attempt to use RefFor() + UnrefFor() instead of
  // Ref() + UnRef().
  // If you do not want to track owners, consider using SimpleReferenceCounted.
  // REQUIRES: this was not created in TRACKED_STRICT mode.
  void Ref() const;

  // Decrements reference count by one.  If the count remains positive,
  // returns false.  When the count reaches zero, returns true and deletes
  // this, in which case the caller must not access the object afterward.
  // Callers should attempt to use RefFor() + UnrefFor() instead of
  // Ref() + UnRef().
  // REQUIRES: this was not created in TRACKED_STRICT mode.
  bool Unref() const;

  // Return a (possibly multi-line) string that contains the stack
  // traces of all existing reference holders.  May have incomplete
  // information if references are not being tracked.
  std::string ListOwners() const;

  // Return true if we are the exclusive owner of the object, i.e. if
  // the reference count is 1.  If this returns false, the reference
  // count might still become 1 at any time if other owners unref in
  // the background, but if this returns true, there are no other
  // owners and thus this information stays correct.
  bool RefIsUnique() const;

  // Return the current reference count. Useful only in unittests, where the
  // caller knows that nobody is modifying ref_ in the background.
  int32_t Test_GetReferenceCount() const {
    ABSL_ANNOTATE_IGNORE_READS_BEGIN();
    int32_t value = ref_.load(std::memory_order_relaxed);
    ABSL_ANNOTATE_IGNORE_READS_END();
    return value;
  }

 protected:
  // Make destructor protected so that ReferenceCounted objects cannot
  // be instantiated directly. Only subclasses can be instantiated.
  virtual ~ReferenceCounted();

 private:
  // Out-of-line state stored for modes other than UNTRACKED.
  // We store ReferenceCountedType in here so that we do not need
  // to waste any extra memory for UNTRACKED objects.
  struct TrackedState {
    explicit TrackedState(ReferenceCountedType t) : type(t) {}
    const ReferenceCountedType type;
    RefTracker owners;
  };

  TrackedState* const tracked_;  // NULL implies untracked
  mutable std::atomic<int32_t> ref_;

  ReferenceCountedType type() const {
    return (tracked_ == nullptr) ? UNTRACKED : tracked_->type;
  }
};

// Convenience routine that does "obj->Ref()" iff obj is non-NULL.
// Otherwise it does nothing.
inline void RefIfNonNull(const ReferenceCounted* obj) {
  if (obj != nullptr) obj->Ref();
}

// Convenience routine that returns "obj->Unref()" iff obj is non-NULL.
// Otherwise it does nothing and returns false.
inline bool UnrefIfNonNull(const ReferenceCounted* obj) {
  return (obj != nullptr) ? obj->Unref() : false;
}

// Helper class to use when no convenient owner is available to
// pass to RefFor/UnrefFor.  Example:
//    util::ReferenceOwner owner;
//    ...
//    object->RefFor(&owner);
//    ...
//    object->UnrefFor(&owner);
// You could just as easily use some other type:
//    char owner;
// However using util::ReferenceOwner seems more readable.
class ReferenceOwner {};

// Akin to MutexLock, this increments the reference count on
// `referent' on construction, and decrements it when destroyed; handy
// for code blocks that need to retain a reference for the scope of
// that block.
class ScopedReference {
 public:
  explicit ScopedReference(const ReferenceCounted* referent)
      : referent_(referent) {
    referent_->RefFor(this);
  }

  // This type is neither copyable nor movable.
  ScopedReference(const ScopedReference&) = delete;
  ScopedReference& operator=(const ScopedReference&) = delete;

  ~ScopedReference() { referent_->UnrefFor(this); }

 private:
  const ReferenceCounted* const referent_;
};
// TODO: This check is temporarily omitted because it causes a
// cross-namespace collision with ScopedReference in util/freelist/refcount.h.
// See bug 1590711 for details and the pending real fix.
// Catch bug where variable name is omitted, e.g. ScopedReference (this);
// #define ScopedReference(x)
//   COMPILE_ASSERT(0, scoped_reference_decl_missing_var_name)

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFERENCE_COUNTED_H_
