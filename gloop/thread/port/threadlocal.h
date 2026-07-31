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

#ifndef THIRD_PARTY_GLOOP_THREAD_PORT_THREADLOCAL_H_
#define THIRD_PARTY_GLOOP_THREAD_PORT_THREADLOCAL_H_

#include "absl/synchronization/mutex.h"
#ifdef _MSC_VER
#include <windows.h>
#else  // _MSC_VER
#include <pthread.h>
#endif  // _MSC_VER
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace thread_internal {

// Generic creation interface to either create a copy from instance,
// if a copy constructor is defined for Type.
// Or return a newly constructed instance for types that are not copyable.
// To be called with std::is_copy_constructible<Type> as second argument.
template <typename Type>
Type CreateInstance(const Type& instance, std::true_type) {
  static_assert(std::is_copy_constructible<Type>::value,
                "Type not copy constructable");
  return instance;
}

template <typename Type>
Type CreateInstance(const Type&, std::false_type) {
  static_assert(!std::is_copy_constructible<Type>::value,
                "Type is copy constructable");
  return Type();
}

}  // namespace thread_internal.

// Mobile implementation of ThreadLocal with a lean set of dependencies.
// This implements semantics of ThreadLocal from thread/threadlocal.h
// via a mutex locked hash_map. Therefore ThreadLocal can be
// used as member variable, a feature that C++11 thread_local does not support.
template <typename Type>
class ThreadLocal {
 public:
  explicit ThreadLocal(const Type& value) : instance_(new Type(value)) {}

  // Default construct.
  ThreadLocal() : instance_(new Type()) {}

  // This type is neither copyable nor movable.
  ThreadLocal(const ThreadLocal&) = delete;
  ThreadLocal& operator=(const ThreadLocal&) = delete;

  Type* pointer() { return &GetThreadItem(); }

  const Type* pointer() const { return &GetThreadItem(); }

  const Type& get() const { return GetThreadItem(); }

  void set(const Type& value) {
    absl::MutexLock ml(&mutex_);
    const ThreadId thread_id = CurrentThreadId();
    if (items_.find(thread_id) != items_.end()) {
      items_.erase(thread_id);
    }
    items_.emplace(thread_id, value);
  }

 private:
#ifdef _MSC_VER
  typedef DWORD ThreadId;
#else   // _MSC_VER
  typedef uintptr_t ThreadId;

  // Unfortunately POSIX does not guarantee a specific pthread_t type, it can be
  // either an arithmetic type or a structure type. As long as below holds, we
  // can explicitly cast pthread_t to uintptr_t.
  static_assert(sizeof(pthread_t) <= sizeof(ThreadId),
                "pthread_t type size is larger than the size of ThreadId");
#endif  // _MSC_VER

  static ThreadId CurrentThreadId() {
#ifdef _MSC_VER
    return GetCurrentThreadId();
#else   // _MSC_VER
    // This is bit_cast<uintptr_t>(pthread_self()). See base/casts.h for
    // additional implementation details.
    //
    // Note that we cannot use a true cast operation here (even
    // reinterpret_cast) since POSIX provides no specification as to the type of
    // pthread_t; meaning that we cannot satisfy any preconditions for casting.
    pthread_t current = pthread_self();
    uintptr_t result = 0;
    memcpy(&result, &current, sizeof(current));
    return result;
#endif  // _MSC_VER
  }

  // Returns a unique item for the calling thread. If item does not exist yet
  // in map, it is created on demand.
  Type& GetThreadItem() const {
    absl::MutexLock ml(&mutex_);
    const ThreadId thread_id = CurrentThreadId();
    auto iter = items_.find(thread_id);
    if (iter == items_.end()) {
      // Create item on demand.
      iter = items_
                 .emplace(thread_id,
                          thread_internal::CreateInstance<Type>(
                              *instance_, std::is_copy_constructible<Type>()))
                 .first;
    }
    return iter->second;
  }

  std::unique_ptr<Type> instance_;
  mutable absl::Mutex mutex_;
  mutable std::unordered_map<ThreadId, Type> items_ ABSL_GUARDED_BY(mutex_);
};

#endif  // THIRD_PARTY_GLOOP_THREAD_PORT_THREADLOCAL_H_
