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

// Implements an object pool with fixed maximum size.  See the class comment
// for FixedSizeObjectPool.

#ifndef THIRD_PARTY_GLOOP_UTIL_FREELIST_FIXED_SIZE_OBJECT_POOL_H_
#define THIRD_PARTY_GLOOP_UTIL_FREELIST_FIXED_SIZE_OBJECT_POOL_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/util/gtl/stl_util.h"

// Implements an object pool with fixed maximum size.
//
// This class pools up to N objects, letting them be grabbed and released.
// The pool will contain some number of initially free objects, and more will
// be created as needed until the pool's maximum size is reached.  Once
// created, objects are not destroyed until the object pool is destroyed.
//
// The "number of available objects" in the free pool is the number of times
// that Get() can be called without blocking (regardless of whether some of
// these objects would have to be instantiated).  It is *not* the number of
// objects currently created that are not grabbed.
//
// This class is thread-safe.
template <class T>
class FixedSizeObjectPool {
  struct Deleter;

 public:
  using UniquePtr = std::unique_ptr<T, Deleter>;

  // Constructs an object pool using the default constructor.
  FixedSizeObjectPool(int maximum_size, int initial_num_created)
      : FixedSizeObjectPool(maximum_size, initial_num_created, [this] {
          return absl::WrapUnique(DefaultFactory());
        }) {}

  // Constructs an object pool, creating the objects using the supplied
  // callable.
  FixedSizeObjectPool(int maximum_size, int initial_num_created,
                      absl::AnyInvocable<std::unique_ptr<T>()> factory)
      : at_least_one_available_cond_(
            this, &FixedSizeObjectPool::AtLeastOneAvailableCb) {
    Init(maximum_size, initial_num_created, std::move(factory));
  }

  // This type is neither copyable nor movable.
  FixedSizeObjectPool(const FixedSizeObjectPool&) = delete;
  FixedSizeObjectPool& operator=(const FixedSizeObjectPool&) = delete;

  // Destroys this object pool.  Any available objects in the pool are deleted.
  // Even though we grab lock pool_mutex_ here, it does not guarantee
  // the dectructor will be executed only after all objects have been returned
  // to pool_. Users of this class should make sure of that; e.g. finish using
  // all objects before destructing the object pool.
  ~FixedSizeObjectPool() ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::MutexLock lock(pool_mutex_);
    gtl::STLDeleteElements(&pool_);
  }

  // Returns the pool's maximum size.
  int MaximumSize() const { return maximum_size_; }

  // Returns the number of objects currently available for grabbing.
  int NumAvailable() const ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::ReaderMutexLock lock(pool_mutex_);
    return (maximum_size_ - num_grabbed_);
  }

  // Returns the number of objects currently grabbed.
  int NumGrabbed() const ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::ReaderMutexLock lock(pool_mutex_);
    return num_grabbed_;
  }

  // Blocks until the pool's number of objects available is equal to the
  // specified number.  Of course, right after this method returns, a
  // different thread may mutate the pool, so this method is most useful for
  // determining things like when all objects have started to be used, or when
  // all objects have been returned to the pool.
  void WaitForNumAvailable(int num_available) const
      ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::MutexLock lock(pool_mutex_);
    CHECK_GE(num_available, 0) << ": must have non-negative num available";
    CHECK_LE(num_available, maximum_size_)
        << ": can't wait for more than maximum size to be available";
    auto avail = [this, num_available] {
      pool_mutex_.AssertReaderHeld();
      return NumAvailableCb(num_available);
    };
    pool_mutex_.Await(absl::Condition(&avail));
  }

  // Returns an object from the pool. If no object is currently available,
  // then nullptr is returned.
  T* TryGet() ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::MutexLock lock(pool_mutex_);
    return GetInternal();
  }

  // Returns smart pointer to an object from the pool, which will be returned
  // back to the pool when the smart pointer is destroyed.
  // If no object is currently available, then nullptr is returned.
  UniquePtr TryGetRAII() ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    return {TryGet(), {this}};
  }

  // Returns an object from the pool. If no object is currently available,
  // then this method blocks until one frees up.
  T* Get() ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::MutexLock lock(pool_mutex_);
    pool_mutex_.Await(at_least_one_available_cond_);
    return GetInternal();
  }

  // Returns a smart pointer to an object from the pool, which will be returned
  // back to the pool when the smart pointer is destroyed.
  UniquePtr GetRAII() ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    return {Get(), {this}};
  }

  // Returns a smart pointer to an object from the pool, which will be returned
  // back to the pool when the smart pointer is destroyed. Will try to wait for
  // an object to become available up to the specified timeout duration.
  // Returns nullptr if timeout occurred before an instance becomes available.
  UniquePtr GetRAIIWithTimeout(absl::Duration timeout)
      ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    return {GetWithTimeout(timeout), {this}};
  }

  // A combination of Get and TryGet where we'll block for up to the specified
  // time if necessary. Returns nullptr if timeout occurred before an instance
  // becomes available.
  T* GetWithTimeout(absl::Duration timeout) ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    absl::MutexLock lock(pool_mutex_);
    pool_mutex_.AwaitWithTimeout(at_least_one_available_cond_, timeout);
    return GetInternal();
  }
  // Overload of GetWithTimeout provided for legacy purposes.
  T* GetWithTimeout(int64_t millis) ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    return GetWithTimeout(absl::Milliseconds(millis));
  }

  // Releases the object back into the pool.
  void Release(T* object) ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    if (object == nullptr) {
      LOG(DFATAL) << "Can't release null object";
      return;
    }

    absl::MutexLock lock(pool_mutex_);

    // Check that there is at least one object grabbed
    if (num_grabbed_ == 0) {
      LOG(DFATAL) << "Can't release more objects than were allocated in the "
                     "object pool";
      delete object;
      return;
    }

    // Add the object to the available list.
    pool_.push_back(object);
    num_grabbed_ -= 1;
  }

  // Similar to Release(), but always deletes the object rather than reusing it
  // in the pool.  This is useful when long-lived objects must be periodically
  // refreshed from scratch. If you need this function, consider using
  // FixedSizeReusableObjectPool instead.
  void Retire(T* object) ABSL_LOCKS_EXCLUDED(pool_mutex_) {
    if (object == nullptr) {
      LOG(DFATAL) << "Can't retire null object";
      return;
    }
    delete object;

    absl::MutexLock lock(pool_mutex_);
    // Check that there is at least one object grabbed
    if (num_grabbed_ == 0) {
      LOG(DFATAL) << "Retiring an object when none were grabbed.";
      return;
    }
    num_grabbed_ -= 1;
  }

 private:
  struct Deleter {
    void operator()(T* v) const {
      if (v != nullptr) {
        pool->Release(v);
      }
    }
    FixedSizeObjectPool* pool;
  };

  // Initializes this FixedSizeObjectPool.
  void Init(int maximum_size, int initial_num_created,
            absl::AnyInvocable<std::unique_ptr<T>()> factory)
      ABSL_NO_THREAD_SAFETY_ANALYSIS {
    // Check parameters.
    CHECK_GT(maximum_size, 0) << ": must have positive maximum size";
    CHECK_GE(initial_num_created, 0)
        << ": must have non-negative initial num created";
    CHECK_LE(initial_num_created, maximum_size)
        << ": can't have initial num created greater than maximum size";

    // Initialize member variables.
    maximum_size_ = maximum_size;
    factory_ = std::move(factory);
    num_grabbed_ = 0;

    // Create initially created objects.
    pool_.reserve(initial_num_created);
    for (int i = 0; i < initial_num_created; ++i) {
      pool_.push_back(factory_().release());
    }
  }

  // Constructs a new object using its default constructor.
  T* DefaultFactory() const { return new T(); }

  // Returns an object from the pool.  If no object is currently available,
  // then NULL is returned.  This method assumes that pool_mutex_ is held.
  T* GetInternal() ABSL_EXCLUSIVE_LOCKS_REQUIRED(pool_mutex_) {
    // pool_mutex_ is already held.
    T* return_object;
    if (!pool_.empty()) {
      return_object = pool_.back();
      pool_.pop_back();
      num_grabbed_ += 1;
    } else if (num_grabbed_ < maximum_size_) {
      return_object = factory_().release();
      num_grabbed_ += 1;
    } else {
      return_object = static_cast<T*>(nullptr);
    }
    return return_object;
  }

  // Returns true iff the number of objects available is equal to the given
  // argument.  This method is used in a Condition by WaitForNumAvailable().
  bool NumAvailableCb(int num_available) const
      ABSL_SHARED_LOCKS_REQUIRED(pool_mutex_) {
    // This method is called as part of Mutex::Await() and therefore does not
    // need to grab pool_mutex_.
    return ((maximum_size_ - num_grabbed_) == num_available);
  }

  // Returns true iff there is at least one object available.  This method is
  // used in a Condition by Get().
  bool AtLeastOneAvailableCb() const ABSL_SHARED_LOCKS_REQUIRED(pool_mutex_) {
    // This method is called as part of Mutex::Await() and therefore does not
    // need to grab pool_mutex_.
    return ((maximum_size_ - num_grabbed_) > 0);
  }

  int maximum_size_;
  absl::AnyInvocable<std::unique_ptr<T>()> factory_;
  const absl::Condition at_least_one_available_cond_;
  mutable absl::Mutex pool_mutex_;
  std::vector<T*> pool_ ABSL_GUARDED_BY(pool_mutex_);
  int num_grabbed_ ABSL_GUARDED_BY(pool_mutex_);
};

#endif  // THIRD_PARTY_GLOOP_UTIL_FREELIST_FIXED_SIZE_OBJECT_POOL_H_
