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

// NOTE: Before building APIs designed to be used with reffed_ptr, consider
// using `std::shared_ptr` instead.  Proceed only if:
//   - Your class has Ref()/Unref() member functions that are already in use, or
//   - `std::shared_ptr` is not suitable for your class and you really need
//     to add an intrusive reference counting API.
//
// A reffed_ptr is similar to a std::shared_ptr, but it is for reference-counted
// objects.  It requires that the object have Ref() and Unref() methods.  When
// created with a raw pointer to an object, the reffed_ptr can either adopt
// responsibility for a reference from the caller, which is the default
// behavior, or take a new reference for itself.
//
// SOME COMMON PATTERNS:
//
// reffed_ptrs are essentially useful in two situations:
// 1) As a local variable to hold a reference until exit from a scope
// 2) As a member variable to hold a reference until destruction of the parent
//
// Construct and initialize a reffed_ptr from a new pointer instance.  A
// properly implemented T constructor should grant the caller a reference, which
// is adopted by the reffed_ptr.  This is the most common pattern when a
// reffed_ptr is used as a local variable:
//   reffed_ptr<T> r_ptr(new T());
// or, equivalently:
//   reffed_ptr<T> r_ptr(new T(), RefTakingMode::kAdopt);
//
// If a reffed_ptr is used as a member variable and the constructor of its
// parent object takes a raw pointer to initialize it with, the reffed_ptr
// generally needs to create a new ref for itself:
//   MyClass::MyClass(T* ptr) : reffed_t_(ptr, RefTakingMode::kCreate) {}
//
// Given an existing reffed_ptr, create a duplicate that has its own ref on
// the object:
//   reffed_ptr<T> r_ptr_b(r_ptr_a.get(), RefTakingMode::kCreate);
// Equivalently:
//   reffed_ptr<T> r_ptr_b(r_ptr_a);
// Or through assignment:
//   reffed_ptr<T> r_ptr_b = r_ptr_a;
//
// Use get() to access the temporarily usable raw pointer from a reffed_ptr.
// The raw pointer should only be used while the reffed_ptr remains in scope,
// or an additional reference should be taken while the reffed_ptr remains in
// scope:
//   T* t = r_ptr.get();
//
// Given two reffed_ptrs, have r_ptr_b drop its reference on whatever it's
// holding and take another reference on the object owned by r_ptr_a:
//  r_ptr_b = r_ptr_a;
//
// Make the reffed_ptr drop its reference and set its pointer to nullptr:
//   r_ptr.reset();
//
// reset() can also be used to give the reffed pointer possession of a new
// pointer.  Whether it adopts or creates a reference can be specified, but
// RefTakingMode::kAdopt is the default:
//   r_ptr.reset(new T());
// or, equivalently:
//   r_ptr.reset(new T(), RefTakingMode::kAdopt);
//
// Transferring ownership from one reffed_ptr to another, clearing the first:
//   r_ptr_b = std::move(r_ptr_a);
// or, equivalently:
//   r_ptr_b.reset(r_ptr_a.release());
// or, equivalently:
//   r_ptr_b.reset(r_ptr_a.release(), RefTakingMode::kAdopt);
//
// Swapping the contents of two r_ptrs (and also swapping their reference
// responsibilities, of course):
//   r_ptr_a.swap(r_ptr_b);
// or, equivalently:
//   swap(r_ptr_a, r_ptr_b);
//
// These patterns are errors, because b believes it is adopting a ref, but no
// ref is conferred:
//   BAD:  r_ptr_b.reset(r_ptr_a.get());
//   BAD:  reffed_ptr<T> r_ptr_b(r_ptr_a.get());
//
// The right ways for b to take its own ref of a's ptr:
//   BEST: r_ptr_b = r_ptr_a;
//   BEST: reffed_ptr<T> r_ptr_b(r_ptr_a);
//   OK:   r_ptr_b.reset(r_ptr_a.get(), RefTakingMode::kCreate);
//
// The right ways to transfer a ref from a to b:
//   BEST: r_ptr_b = std::move(r_ptr_a);
//   GOOD: r_ptr_b.reset(r_ptr_a.release());  // kAdopt is implicit
//   OK:   r_ptr_b.reset(r_ptr_a.release(), RefTakingMode::kAdopt);
//   GOOD: reffed_ptr<T> r_ptr_b(r_ptr_a.release());
//   GOOD: reffed_ptr<T> r_ptr_b;
//         swap(r_ptr_a, r_ptr_b);  // sometimes a useful trick
//
// There are also WrapReffed and MakeReffed functions to better mimic
// unique_ptr/shared_ptr. Look below for their docs.

#ifndef THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFFED_PTR_H_
#define THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFFED_PTR_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/meta/type_traits.h"

namespace refcount {

// The RefTakingMode tells the reffed_ptr whether to acquire its own ref when
// taking a handle to a raw pointer or to steal the ref owned by the caller.
enum class RefTakingMode {
  kCreate,  // reffed_ptr takes a new ref by calling Ref() on the object
  kAdopt,   // reffed_ptr assumes ownership of a ref from the caller
};

// A reffed_ptr<T> is like a T*, except that the destructor of reffed_ptr<T>
// calls the Unref() method on the object it holds, if not null.
// That is, reffed_ptr<T> owns a reference on the T object that it points to.
// Like a T*, a reffed_ptr<T> may hold either null or a pointer to a T object.
// Also like T*, reffed_ptr<T> is thread-compatible, and once you
// dereference it, you get the thread-safety guarantees of T.
//
// Instantiations of the T class parameter should inherit from
// util::ReferenceCounted (util/refcount/reference_counted.h) or
// SimpleReferenceCounted (util/refcount/simple-reference-counted.h), or
// implement an interface like the following:
//
// class RefCountedInterface {
//  public:
//   // Takes a reference on the object:
//   void Ref() [const];
//   // Drops a reference and must delete the object if the ref count hits 0:
//   void Unref() [const];
// }
//
// In addition, the constructor of a ref-counted class ought to initialize the
// ref count to 1, so the caller owns a reference.  Otherwise, the object
// arrives in an invalid state: alive but with a 0 reference count.  Both
// SimpleReferenceCounted and ReferenceCounted have this behavior.
//
// reffed_ptr has some similarities to the ScopedReference classes defined in
// util/refcount/reference_counted.h and util/freelist/refcount.h, but one
// significant difference is that those classes both acquire a new reference
// when taking possession of a raw pointer, so their usage can be trickier,
// requiring a pattern like the following:
//   ScopedReference<Foo> s_ref(new Foo());
//   s_ref->Unref();  // Drop the extra ref from the Foo constructor
//
// As opposed to:
//   reffed_ptr<Foo> r_ptr(new Foo(), RefTakingMode::kAdopt);
// or, simply:
//   reffed_ptr<Foo> r_ptr(new Foo());
//
// A reffed_ptr is the same size as a T*.
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI ABSL_NULLABILITY_COMPATIBLE reffed_ptr {
 public:
  typedef T element_type;

  // Holds nullptr by default.
  constexpr reffed_ptr() = default;

  // Constructor from a raw pointer.
  //
  // Note that this either creates a new reference for itself or adopts a ref
  // from the caller, depending on the ref_mode.  The default is to use
  // RefTakingMode::kAdopt, which is the much more common case and is
  // appropriate if ptr has been newly constructed.
  // If ptr is null, the ref_mode has no bearing.
  explicit reffed_ptr(T* ptr, RefTakingMode ref_mode = RefTakingMode::kAdopt)
      : ptr_(ptr) {
    if (ptr_ != nullptr && ref_mode == RefTakingMode::kCreate) ptr_->Ref();
  }

  // Allows implicit conversion from nullptr. This allows writing nullptr
  // whenever it would otherwise be needed to write reffed_ptr<Foo>(nullptr).
  constexpr reffed_ptr(std::nullptr_t) {}  // NOLINT

  // The copy constructor implements RefTakingMode::kCreate. It does not take
  // the reference away from 'other' and they each end up with their own ref.
  // Supports copying from compatible reffed_ptr<U> types.
  // Note that the template doesn't suppress the compiler-generated
  // copy constructor when U=T, so we provide an identical
  // non-template.
  template <typename U>
  reffed_ptr(const reffed_ptr<U>& other) : ptr_(other.get()) {  // NOLINT
    if (ptr_ != nullptr) ptr_->Ref();
  }
  reffed_ptr(const reffed_ptr& other) : ptr_(other.get()) {
    if (ptr_ != nullptr) ptr_->Ref();
  }
  // Move constructors corresponding to the copy constructors.
  // Move operations implement RefTakingMode::kAdopt.
  // A moved-from reffed_ptr holds nullptr.
  template <typename U>
  reffed_ptr(reffed_ptr<U>&& other) noexcept  // NOLINT
      : ptr_(other.release()) {}
  reffed_ptr(reffed_ptr&& other) noexcept : ptr_(other.release()) {}

  // If there is a non-null ptr_, the ref on it is dropped.
  ~reffed_ptr() {
    if (ptr_ != nullptr) ptr_->Unref();
  }

  explicit operator bool() const { return ptr_ != nullptr; }

  // The assignment operator, like the copy constructor, implements
  // RefTakingMode::kCreate.  After the call, the left hand side and right
  // hand side of the assignment expression each hold a reference to the
  // pointee.
  // Self-assignment is safe.
  template <typename U>
  reffed_ptr& operator=(const reffed_ptr<U>& other) {
    reset(reffed_ptr<U>(other).release());
    return *this;
  }
  reffed_ptr& operator=(const reffed_ptr& other) {
    reset(reffed_ptr(other).release());
    return *this;
  }
  // Move assignments corresponding to the copy assignments.
  // Move operations implement RefTakingMode::kAdopt.
  // A moved-from reffed_ptr holds nullptr.
  template <typename U>
  reffed_ptr& operator=(reffed_ptr<U>&& other) noexcept {
    reset(other.release());
    return *this;
  }
  reffed_ptr& operator=(reffed_ptr&& other) noexcept {
    reset(other.release());
    return *this;
  }

  // Accessors for the referenced object.
  // operator* and operator-> will assert() if there is no current object.
  T& operator*() const {
    assert(ptr_ != nullptr);
    return *ptr_;
  }
  T* operator->() const {
    assert(ptr_ != nullptr);
    return ptr_;
  }
  // This returns the raw pointer with no change in reference ownership.
  // Be careful not to pass the return value to something that expects to
  // adopt a reference.  See the BAD PATTERNS above.
  T* get() const { return ptr_; }

  // Drops the ref on the current object, if any.
  // Replaces it with the new_ptr and either creates a new ref on it or
  // adopts a ref from the caller, depending on the ref_mode.
  // Resetting to the current pointer value is safe, as the Unref of the old
  // pointer is performed after the Ref (if creating) of the new pointer.
  void reset(T* new_ptr = nullptr,
             RefTakingMode ref_mode = RefTakingMode::kAdopt) {
    reffed_ptr(new_ptr, ref_mode).swap(*this);
  }

  // Release and return the pointer, along with ownership of the reference.
  // The return value is the current pointer held by this object.  After this
  // operation, the reffed_ptr will hold a null pointer, and will not own a
  // ref on the object any more.  If the returned pointer is non-null, the
  // caller assumes ownership of the reference on it.
  //
  // WARNING: The refcount on the object is left unchanged. See example below:
  //   {
  //     // Let's assume refcount is initialized to 1 because T doesn't
  //     // specialize RefCountStartsAtZero
  //     (//gloop/util/refcount/reffed_ptr.h;l=412-427;rcl=792165480)
  //     reffed_ptr<T> p_reffed = MakeReffed<T>(...);
  //     ...
  //     T* p = p_reffed.release();
  //     ...
  //     WrapReffed<T>(p, RefTakingMode::kCreate);
  //     ...
  //   }
  //   // MEMORY LEAK: The refcount is still 1, so the object is never
  //   // destroyed.
  T* release() { return std::exchange(ptr_, nullptr); }

  // Swap two reffed pointers.  Each retains a ref.
  void swap(reffed_ptr& other) noexcept {
    using std::swap;
    swap(ptr_, other.ptr_);
  }

  // Nonmember swap.
  friend void swap(reffed_ptr& a, reffed_ptr& b) {  // NOLINT
    a.swap(b);
  }

  // Comparisons against same type.
  friend bool operator==(const reffed_ptr& a, const reffed_ptr& b) {
    return a.get() == b.get();
  }
  friend bool operator!=(const reffed_ptr& a, const reffed_ptr& b) {
    return a.get() != b.get();
  }
  friend bool operator<(const reffed_ptr& a, const reffed_ptr& b) {
    return a.get() < b.get();
  }
  friend bool operator<=(const reffed_ptr& a, const reffed_ptr& b) {
    return a.get() <= b.get();
  }
  friend bool operator>(const reffed_ptr& a, const reffed_ptr& b) {
    return a.get() > b.get();
  }
  friend bool operator>=(const reffed_ptr& a, const reffed_ptr& b) {
    return a.get() >= b.get();
  }

  // Comparisons against other reffed_ptr types.
  template <typename U>
  friend bool operator==(const reffed_ptr& a, const reffed_ptr<U>& b) {
    return a.get() == b.get();
  }
  template <typename U>
  friend bool operator!=(const reffed_ptr& a, const reffed_ptr<U>& b) {
    return a.get() != b.get();
  }
  template <typename U>
  friend bool operator<(const reffed_ptr& a, const reffed_ptr<U>& b) {
    return a.get() < b.get();
  }
  template <typename U>
  friend bool operator<=(const reffed_ptr& a, const reffed_ptr<U>& b) {
    return a.get() <= b.get();
  }
  template <typename U>
  friend bool operator>(const reffed_ptr& a, const reffed_ptr<U>& b) {
    return a.get() > b.get();
  }
  template <typename U>
  friend bool operator>=(const reffed_ptr& a, const reffed_ptr<U>& b) {
    return a.get() >= b.get();
  }

  // Comparisons against nullptr.
  friend bool operator==(const reffed_ptr& a, std::nullptr_t) {
    return a.get() == nullptr;
  }
  friend bool operator==(std::nullptr_t, const reffed_ptr& b) {
    return nullptr == b.get();
  }
  friend bool operator!=(const reffed_ptr& a, std::nullptr_t) {
    return a.get() != nullptr;
  }
  friend bool operator!=(std::nullptr_t, const reffed_ptr& b) {
    return nullptr != b.get();
  }

  // absl::Hash-able. Defined as being the hash of the contained pointer.
  template <typename H>
  friend H AbslHashValue(H h, const reffed_ptr& p) {
    return H::combine(std::move(h), p.ptr_);
  }

 private:
  // Invariant: this reffed_ptr owns a reference on ptr_ iff ptr_ is non-null.
  T* absl_nullable ptr_ = nullptr;
};

// Tell the compiler that we're okay with class template argument deduction.
template <typename T>
reffed_ptr(reffed_ptr<T>&&) -> reffed_ptr<T>;

// Wraps an existing pointer in a `reffed_ptr`.  The returned value is a
// `reffed_ptr` of deduced type.  The `ref_mode` parameter controls whether
// the `reffed_ptr` creates a new reference by calling `ptr->Ref()` or adopts
// an existing reference.
//
// This is the `reffed_ptr` equivalent of `absl::WrapUnique`.
//
// This function is a convenient way to create a `reffed_ptr` from an existing
// raw pointer (e.g., from a factory function):
//
//   // A factory function declared in a header somewhere:
//   a_long_namespace_name::AReallyLongClassName* create();
//
//   // Using reffed_ptr's constructor:
//   refcount::reffed_ptr<a_long_namespace_name::AReallyLongClassName> foo{
//       create()};
//
//   // Same, but avoids spelling out the object's type:
//   auto foo = refcount::WrapReffed(create());
//
// Here's an example where an existing object is shared via an API that uses
// raw pointers:
//
//   // In a header somewhere.
//   class Foo {
//    public:
//     // Does not release ownership.
//     SomeRefCountedObjectType* bar();
//     //...
//   };
//
//   Foo foo;
//
//   // Using reffed_ptr's constructor:
//   refcount::reffed_ptr<SomeRefCountedObjectType> bar{
//       foo.bar(), refcount::RefTakingMode::kCreate};
//
//   // Same, but avoids spelling out the object's type:
//   auto bar = refcount::WrapReffed(foo.bar(),
//                                   refcount::RefTakingMode::kCreate);
//
// Prefer 'refcount::MakeReffed<T>(args...)' over using this with operator
// new:
//
//   auto p = refcount::WrapReffed(new X(1, 2));  // works, but nonideal.
//   auto p = refcount::MakeReffed<X>(1, 2);      // safer, avoids raw 'new'.
//
// Note that `refcount::WrapReffed(p, mode)` is valid only if `p->Ref()` and
// `p->Unref()` are valid expressions.
template <int&... ExplicitParameterBarrier, typename T>
reffed_ptr<T> WrapReffed(T* ptr,
                         RefTakingMode ref_mode = RefTakingMode::kAdopt) {
  return reffed_ptr<T>(ptr, ref_mode);
}

// Type trait whose member named 'value' is true if the construction of a new
// T does not come with a ref that can be adopted by reffed_ptr, false
// otherwise.
//
// This trait is false by default.  If this trait should be true for your
// class, do not specialize it -- add a nested type named
// ref_count_starts_at_zero instead.  For example:
//
//   class Foo {
//    public:
//     // For refcount::MakeReffed().
//     using ref_count_starts_at_zero = void;
//     //...
//   };
template <typename T, typename = void>
struct RefCountStartsAtZero : public std::false_type {};

template <typename T>
struct RefCountStartsAtZero<T,
                            std::void_t<typename T::ref_count_starts_at_zero>>
    : public std::true_type {};

// Creates a new object wrapped in a `reffed_ptr`.  This avoids issues caused
// by creating temporaries during the construction process, and it avoids
// redundant type declarations by avoiding the need to explicitly use the
// `new` operator.
//
// This is the `reffed_ptr` equivalent of `std::make_unique`.
//
// A new reference is created if RefCountStartsAtZero<T>::value is true,
// otherwise it adopts the new object's existing reference.
//
// Example usage:
//
//   // Runs 'new X(1, 2)' and puts the result in a reffed_ptr<X>.
//   auto p = refcount::MakeReffed<X>(1, 2);
//
// Note that `refcount::MakeReffed<X>(args...)` is valid only if `(new
// X(args...))->Ref()` and `(new X(args...))->Unref()` are valid expressions.
template <typename T, int&... ExplicitParameterBarrier, typename... Args>
reffed_ptr<T> MakeReffed(Args&&... args) {
  return reffed_ptr<T>(new T(std::forward<Args>(args)...),
                       RefCountStartsAtZero<T>::value ? RefTakingMode::kCreate
                                                      : RefTakingMode::kAdopt);
}

}  // namespace refcount

#endif  // THIRD_PARTY_GLOOP_UTIL_REFCOUNT_REFFED_PTR_H_
