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

// Simple templatized free-list.  This differs from an object pool in
// that all contents of an object-pool are released at once.  Here,
// the app can release one object at a time.

#ifndef THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_H_
#define THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_H_

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

ABSL_DECLARE_FLAG(bool, use_freelists);  // In freelist.cc

// An AbstractFreeList maintains a free list of objects.  Objects allocated
// from the free list are in an unknown state.  They may be freshly
// constructed or in whatever state they were in when returned to the list.

namespace freelist {
template <typename T>
class Deleter;
}  // namespace freelist

template <class T>
class AbstractFreeList {
 public:
  virtual ~AbstractFreeList() = default;
  // Get a new object T from free list
  virtual T* New() = 0;
  // Allocate "N" objects in one call, returning them in "ptr[0] .. ptr[N-1]".
  // "ptr" must point to an array large enough to hold the returned pointers.
  virtual void NewMany(T** ptr, int N);
  // Allocate an object T from the freelist, and return it as a unique_ptr with
  // a custom deleter that will return it to the freelist on deletion.
  std::unique_ptr<T, freelist::Deleter<T>> NewUniquePtr();
  // Return an object T to free list.  Delete(nullptr) is a nop.
  virtual void Delete(T*) = 0;
  // Deallocate "N" objects in "ptr[0] .. ptr[N-1]" in one call.
  virtual void DeleteMany(T** ptr, int n);
  // Current size of free list
  virtual int size() const = 0;
  // Verify all elements of the free list
  virtual bool CheckRep() const = 0;
  // Determine if an element can be safely deleted (mostly for testing)
  virtual bool CanDelete(T*) const = 0;
  // Clear all elements from the list.
  virtual void Clear() = 0;
  // Returns a string with some stats on the freelist behavior.
  virtual std::string DebugString() const = 0;
};

// A FastAllocator is like an AbstractFreeList but allows you to specify an
// initializer method or a finalizer method, or both.  The initializer, if
// specified, is invoked on any object before it is given to the caller via
// New(), whether the object was constructed or reused from the internal
// free list.  The finalizer, if specified, is invoked on any object after
// it is returned to the allocator via Delete(), whether it ends up being
// deleted or put back onto the free list.  A FastAllocator is thread-safe
// if and only if its AbstractFreeList is.
//
// The initializer and finalizer are thus like constructors and destructors
// but are intended to be lightweight (e.g., some partial setup/cleanup
// that sets a few fields).  Otherwise, why bother with a free list at all?
// Much simpler to just use "new" and "delete".  If neither is specified,
// this is the same as an AbstractFreeList.  Use that instead.  We declare
// a separate class because (a) if an application cares so much about New
// and Delete performance that the extra check for initializer or finalizer
// to call is too costly, it is not obligated to carry the extra baggage,
// and (b) we don't need default arguments (frowned upon) for backward
// compatibility.
//
// For example,
//
// struct GFS_RPC {
//   GFS_Request request;      // where GFS_Request is a ProtocolMessage
//   IOBuffer* payload;        // *payload belongs to someone else, not me
//   void Init() { request.Clear(); payload = nullptr; }
// };
//
// FastAllocator<GFS_RPC> alloc(new FreeList<GFS_RPC>(10),
//                              &GFS_RPC::Init, /* no finalizer */ nullptr);
// GFS_RPC* rpc = alloc.New();
// ...
// alloc.Delete(rpc);

template <class T>
class FastAllocator {
 public:
  typedef void (T::*Initializer)();
  typedef void (T::*Finalizer)();

  // Owns free_list afterward
  FastAllocator(AbstractFreeList<T>* const free_list,
                const Initializer initializer, const Finalizer finalizer)
      : free_list_(free_list),
        initializer_(initializer),
        finalizer_(finalizer) {}

  ~FastAllocator() { delete free_list_; }

  T* New() {
    T* const x = free_list_->New();
    if (initializer_) {  // don't use != nullptr, compiler complains
      (x->*initializer_)();
    }
    return x;
  }

  // Return an object T.  Delete(nullptr) is a nop.
  void Delete(T* const x) {
    if (x == nullptr) return;
    if (finalizer_) {  // don't use != nullptr, compiler complains
      (x->*finalizer_)();
    }
    free_list_->Delete(x);
  }

  int size() const { return free_list_->size(); }

 private:
  AbstractFreeList<T>* const free_list_;
  const Initializer initializer_;
  const Finalizer finalizer_;
};

namespace freelist {

// A deleter for use in smart pointers containing objects from an
// AbstractFreeList.
template <typename T>
class Deleter {
 public:
  explicit Deleter(AbstractFreeList<T>* freelist) : freelist_(freelist) {}
  void operator()(T* element) { freelist_->Delete(element); }

 private:
  AbstractFreeList<T>* freelist_;
};
// Opt into class template argument deduction (CTAD) by adding explicit
// deduction guide for the constructor.
//
// This allows creating a Deleter<T> from an AbstractFreeList<T> pointer without
// needing to specify the type T multiple times. E.g. when creating unique_ptrs:
//
//   FreeList<Foo> fl;
//   std::unique_ptr<Foo, freelist::Deleter<Foo>> ptr(fl.New(),
//     freelist::Deleter(&fl));
//
// See <link>
template <typename T>
explicit Deleter(AbstractFreeList<T>*) -> Deleter<T>;

}  // namespace freelist

// ScopedFreeListPointer is just a std::unique_ptr with custom deleter. Give us
// a freelist, and it allocates a freelist element; when we go out of scope the
// element is returned to the freelist.
template <typename T>
class [[deprecated(
    "Use std::unique_ptr<T, freelist::Deleter<T>> directly "
    "instead.")]] ABSL_NULLABILITY_COMPATIBLE ScopedFreeListPointer
    : public std::unique_ptr<T, freelist::Deleter<T>> {
 public:
  explicit ScopedFreeListPointer(AbstractFreeList<T>* freelist)
      : ScopedFreeListPointer(freelist->New(), freelist) {}
  ScopedFreeListPointer(T* element, AbstractFreeList<T>* freelist)
      : std::unique_ptr<T, freelist::Deleter<T>>(
            element, freelist::Deleter<T>(freelist)) {}
};

// A pure virtual base class for object factories used by the various
// FreeList classes. If you want to construct and destroy list elements
// yourself, you need to specialize this class.
template <class T>
class ObjectFactory {
 public:
  virtual ~ObjectFactory() = default;
  virtual T* New() = 0;
  virtual void Delete(T* p) = 0;
};

// A simple factory class that uses new and delete to create and destroy
// objects with a 0-argument constructor.  This is thread safe.
template <class T>
class TrivialFactory : public ObjectFactory<T> {
 public:
  T* New() override { return new T(); }
  void Delete(T* p) override { delete p; }
};

namespace freelist_internal {

template <typename T>
TrivialFactory<T>& GetDefaultTrivialFactoryInstance() {
  static absl::NoDestructor<TrivialFactory<T>> factory;
  return *factory;
}

}  // namespace freelist_internal

// A free list that can use a factory object to create and destroy new
// objects.
template <class T>
class FreeList : public AbstractFreeList<T> {
 private:
  // List of free objects. Remains null when freelists are disabled
  // (!absl::GetFlag(FLAGS_use_freelists) at construction).
  T** absl_nullable list_;
  int max_length_;  // Maximum number of free objects to keep
  // Number of free objects. Always 0 when `list_` is null.
  int size_;

 protected:
  ObjectFactory<T>* factory_;  // Factory class for new objects

 public:
  // Pass in a factory object. FreeList does *not* own the factory. The
  // caller must make sure that the factory object outlives all elements
  // in the FreeList.
  //
  // Caveat Emptor: When I say "outlive" I mean it. The factory is used
  // in the destruction of FreeList elements, and elements are normally
  // destroyed in the FreeList destructor. The only way around it is to
  // make sure the list is empty when destroyed. This can be achieved by
  // using the Clear() method as the last method invoked on the list
  // before its destruction. This is particularly relevant when inheriting
  // from FreeList. For example:
  //
  // class D : public FreeList<X> {
  //   ...
  //   MyFactory factory_;
  //   D(...) : FreeList<X>(..., &factory_) ...
  //   ~D() { Clear(); }
  //
  // Without the Clear() call, the factory_ object will be destroyed
  // before ~FreeList<X> is invoked, and when that destructor
  // tries to destroy the elements in the list it'll crash and
  // burn. The Clear() call makes sure there are no elements in the
  // list by that time, so the factory_'s Delete() method doesn't have
  // to be called.
  explicit FreeList(int max_length, ObjectFactory<T>* factory);

  // Use a default TrivialFactory, obtained from a singleton TrivialFactory<T>,
  // so there is no need to worry about the factory's lifetime.
  explicit FreeList(int max_length);

  ~FreeList() override;

  void Clear() override;
  T* New() override;

  void Delete(T* x) override;

  // Returns the number of elements currently in the freelist
  int size() const override { return size_; }
  virtual int max_length() const { return max_length_; }
  bool CheckRep() const override;

  // NB this routine is expensive (linear in the FreeList size)
  bool CanDelete(T* x) const override;

  // Returns a string with some stats on the freelist behavior.
  std::string DebugString() const override;

 private:
  FreeList(const FreeList&) = delete;
  FreeList& operator=(const FreeList&) = delete;
};

// Freelist safe for multi-threaded access
template <class T>
class ThreadSafeFreeList : public AbstractFreeList<T> {
 public:
  ThreadSafeFreeList(int max_length, ObjectFactory<T>* factory);

  explicit ThreadSafeFreeList(int max_length);

  ~ThreadSafeFreeList() override;

  T* New() override;

  void NewMany(T** ptr, int N) override;

  void Delete(T* x) override;

  void DeleteMany(T** ptr, int N) override;

  void Clear() override;

  // Returns the number of elements currently in the freelist
  int size() const override {
    absl::ReaderMutexLock l(lock_);
    return size_;
  }

  virtual int max_length() const { return max_length_; }

  bool CheckRep() const override;

  bool CanDelete(T* x) const override;

  // Returns a string with some stats on the freelist behavior.
  std::string DebugString() const override;

 private:
  mutable absl::Mutex lock_;
  // List of free objects. Remains null when freelists are disabled
  // (!absl::GetFlag(FLAGS_use_freelists) at construction).
  T** absl_nullable list_;
  const int max_length_;  // Maximum number of free objects to keep
  // Number of free objects. Always 0 when `list_` is null.
  int size_;

  bool CanDeleteLocked(T* x) const;

 protected:
  ObjectFactory<T>* factory_;  // Factory class for new objects

 private:
  ThreadSafeFreeList(const ThreadSafeFreeList&) = delete;
  ThreadSafeFreeList& operator=(const ThreadSafeFreeList&) = delete;
};

////////////////////////////////////
// Implementation - AbstractFreeList
template <class T>
void AbstractFreeList<T>::NewMany(T** ptr, int N) {
  for (int i = 0; i < N; i++) {
    *ptr++ = New();
  }
}

template <typename T>
std::unique_ptr<T, freelist::Deleter<T>> AbstractFreeList<T>::NewUniquePtr() {
  return std::unique_ptr<T, freelist::Deleter<T>>(New(),
                                                  freelist::Deleter(this));
}

template <class T>
void AbstractFreeList<T>::DeleteMany(T** ptr, int n) {
  DCHECK_GE(n, 0);
  for (int i = 0; i < n; ++i) {
    Delete(ptr[i]);
  }
}

////////////////////////////////////
// Implementation - FreeList
template <class T>
FreeList<T>::FreeList(int max_length, ObjectFactory<T>* factory)
    : list_(absl::GetFlag(FLAGS_use_freelists)
                ? std::allocator<T*>().allocate(max_length)
                : nullptr),
      max_length_(max_length),
      size_(0),
      factory_(factory) {}

template <class T>
FreeList<T>::FreeList(int max_length)
    : list_(absl::GetFlag(FLAGS_use_freelists)
                ? std::allocator<T*>().allocate(max_length)
                : nullptr),
      max_length_(max_length),
      size_(0),
      factory_(&freelist_internal::GetDefaultTrivialFactoryInstance<T>()) {}

template <class T>
FreeList<T>::~FreeList() {
  Clear();
  if (list_ != nullptr) {
    std::allocator<T*>().deallocate(list_, max_length_);
  }
}

template <class T>
void FreeList<T>::Clear() {
  // When `list_` is null, `size_` is guaranteed to be 0.
  for (int i = 0; i < size_; i++) {
    DCHECK(list_ != nullptr);
    factory_->Delete(list_[i]);
  }
  size_ = 0;
}

template <class T>
inline T* FreeList<T>::New() {
  // When `list_` is null, `size_` is guaranteed to be 0.
  if (size_ > 0) {
    DCHECK(list_ != nullptr);
    T* retval = list_[--size_];
    return retval;
  } else {
    return factory_->New();
  }
}

template <class T>
inline void FreeList<T>::Delete(T* x) {
  if (x == nullptr) return;
  if ((size_ < max_length_) && (list_ != nullptr)) {
    list_[size_++] = x;
  } else {
    factory_->Delete(x);
  }
}

template <class T>
bool FreeList<T>::CheckRep() const {
  return true;  // if we got this far
}

template <class T>
bool FreeList<T>::CanDelete(T* x) const {
  // When `list_` is null, `size_` is guaranteed to be 0.
  for (int i = 0; i < size_; ++i) {
    DCHECK(list_ != nullptr);
    if (list_[i] == x) return false;  // x already deleted
  }
  return true;
}

template <class T>
std::string FreeList<T>::DebugString() const {
  return absl::StrFormat("size: %d, max_length: %d\n", size_, max_length_);
}

////////////////////////////////////
// Implementation - ThreadSafeFreeList
template <class T>
ThreadSafeFreeList<T>::ThreadSafeFreeList(int max_length,
                                          ObjectFactory<T>* factory)
    : list_(absl::GetFlag(FLAGS_use_freelists)
                ? std::allocator<T*>().allocate(max_length)
                : nullptr),
      max_length_(max_length),
      size_(0),
      factory_(factory) {}

template <class T>
ThreadSafeFreeList<T>::ThreadSafeFreeList(int max_length)
    : list_(absl::GetFlag(FLAGS_use_freelists)
                ? std::allocator<T*>().allocate(max_length)
                : nullptr),
      max_length_(max_length),
      size_(0),
      factory_(&freelist_internal::GetDefaultTrivialFactoryInstance<T>()) {}

template <class T>
ThreadSafeFreeList<T>::~ThreadSafeFreeList() {
  // No lock. If you're racing with the destructor, it's your own lookout.
  if (list_ != nullptr) {
    for (int i = 0; i < size_; i++) {
      DCHECK(list_ != nullptr);
      factory_->Delete(list_[i]);
    }
    std::allocator<T*>().deallocate(list_, max_length_);
  }
}

template <class T>
void ThreadSafeFreeList<T>::Clear() {
  absl::MutexLock l(lock_);
  // When `list_` is null, `size_` is guaranteed to be 0.
  for (int i = 0; i < size_; i++) {
    DCHECK(list_ != nullptr);
    factory_->Delete(list_[i]);
  }
  size_ = 0;
}

template <class T>
inline T* ThreadSafeFreeList<T>::New() {
  {
    absl::MutexLock l(lock_);
    // When `list_` is null, `size_` is guaranteed to be 0.
    if (size_ > 0) {
      DCHECK(list_ != nullptr);
      T* retval = list_[--size_];
      return retval;
    }
  }

  return factory_->New();
}

template <class T>
void ThreadSafeFreeList<T>::NewMany(T** ptr, int N) {
  if (list_ != nullptr) {
    absl::MutexLock l(lock_);
    const int num_to_grab = std::min(N, size_);
    memcpy(ptr, list_ + (size_ - num_to_grab), sizeof(T*) * num_to_grab);
    N -= num_to_grab;
    size_ -= num_to_grab;
    ptr += num_to_grab;
  }

  // Anything we couldn't grab from the freelist, we new.
  for (; N > 0; --N) {
    *ptr++ = factory_->New();
  }
}

template <class T>
inline void ThreadSafeFreeList<T>::Delete(T* x) {
  if (x == nullptr) return;
  if (list_ != nullptr) {
    absl::MutexLock l(lock_);
    if (size_ < max_length_) {
      list_[size_++] = x;
      return;
    }
  }

  factory_->Delete(x);
}

template <class T>
void ThreadSafeFreeList<T>::DeleteMany(T** ptr, int N) {
  if (list_ != nullptr) {
    absl::MutexLock l(lock_);
    for (; N > 0 && size_ < max_length_; --N) {
      T* d = *ptr++;
      if (d != nullptr) list_[size_++] = d;
    }
  }

  for (; N > 0; --N) {
    factory_->Delete(*ptr++);
  }
}

template <class T>
bool ThreadSafeFreeList<T>::CheckRep() const {
  return true;  // if we got this far
}

template <class T>
bool ThreadSafeFreeList<T>::CanDelete(T* x) const {
  absl::ReaderMutexLock l(lock_);
  return CanDeleteLocked(x);
}

template <class T>
bool ThreadSafeFreeList<T>::CanDeleteLocked(T* x) const {
  // When `list_` is null, `size_` is guaranteed to be 0.
  for (int i = 0; i < size_; ++i) {
    DCHECK(list_ != nullptr);
    if (list_[i] == x) return false;  // x already deleted
  }
  return true;
}

template <class T>
std::string ThreadSafeFreeList<T>::DebugString() const {
  absl::ReaderMutexLock l(lock_);
  return absl::StrFormat("size: %d, max_length: %d\n", size_, max_length_);
}

// Freelist with no maximum length.  All deleted objects are kept for
// possible reuse until the freelist itself is deleted.
template <class T>
class UnboundedFreeList final : public AbstractFreeList<T> {
 public:
  explicit UnboundedFreeList(ObjectFactory<T>* factory) : factory_(factory) {}

  UnboundedFreeList()
      : factory_(&freelist_internal::GetDefaultTrivialFactoryInstance<T>()) {}

  ~UnboundedFreeList() override { Clear(); }

  void Clear() override {
    for (int i = 0; i < size(); i++) {
      factory_->Delete(list_[i]);
    }
    list_.clear();
  }

  T* New() override {
    if (list_.empty()) return factory_->New();
    T* retval = list_.back();
    list_.pop_back();
    return retval;
  }

  void NewMany(T** ptr, int N) override {
    for (int i = 0; i < N; i++) {
      *ptr++ = New();
    }
  }

  // Return x to free list.  Delete(nullptr) is a nop.
  void Delete(T* x) override {
    if (x == nullptr) return;
    if (absl::GetFlag(FLAGS_use_freelists)) {
      list_.push_back(x);
    } else {
      factory_->Delete(x);
    }
  }

  int size() const override { return list_.size(); }

  bool CheckRep() const override {
    return true;  // if we got this far
  }

  // NB this routine is expensive. (Linear in the (unbounded!) freelist
  // size)
  bool CanDelete(T* x) const override {
    for (int i = 0; i < size(); ++i) {
      if (list_[i] == x) return false;  // x already deleted
    }
    return true;
  }

  // Returns a string with some stats on the freelist behavior.
  std::string DebugString() const override {
    return absl::StrFormat("size: %d\n", size());
  }

 private:
  std::vector<T*> list_;
  ObjectFactory<T>* factory_;

  UnboundedFreeList(const UnboundedFreeList&) = delete;
  UnboundedFreeList& operator=(const UnboundedFreeList&) = delete;
};

#endif  // THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_H_
