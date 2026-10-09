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

// A ThreadLocalFreeList supplies a FreeList of N elements for every thread.
// This reduces lock contention, but may also significantly increase memory
// usage since the number of free elements may be N*T where T is the number of
// threads.
//
// ThreadLocalFreeLists should only be used if the same thread calls New and
// Delete on an object.  If objects can be passed between threads or if there
// are many more threads than CPUs, a CPULocalFreeList may be more appropriate.

#ifndef THIRD_PARTY_GLOOP_UTIL_FREELIST_THREADLOCAL_FREELIST_H_
#define THIRD_PARTY_GLOOP_UTIL_FREELIST_THREADLOCAL_FREELIST_H_

#include <string>

#include "gloop/thread/threadlocal.h"
#include "gloop/util/freelist/freelist.h"

template <class T>
class ThreadLocalFreeList final : public AbstractFreeList<T> {
 public:
  explicit ThreadLocalFreeList(int max_length, ObjectFactory<T>* factory);
  explicit ThreadLocalFreeList(int max_length);

  // This type is neither copyable nor movable.
  ThreadLocalFreeList(const ThreadLocalFreeList&) = delete;
  ThreadLocalFreeList& operator=(const ThreadLocalFreeList&) = delete;

  ~ThreadLocalFreeList() override;

  // See freelist.h for the specification of the following methods
  T* New() override;
  void NewMany(T** ptr, int N) override;
  void Delete(T* x) override;
  void DeleteMany(T** ptr, int N) override;
  int size() const override;
  bool CheckRep() const override;
  bool CanDelete(T* x) const override;
  void Clear() override;

  // Returns a string with some stats on the freelist behavior.
  std::string DebugString() const override;

  // Implementation follows
 private:
  class FactoryFreeList : public FreeList<T> {
   public:
    FactoryFreeList(int max_length, ObjectFactory<T>* factory)
        : FreeList<T>(max_length, factory), factory_(factory) {}
    FactoryFreeList(const FactoryFreeList& copy)
        : FreeList<T>(copy.max_length(), copy.factory_),
          factory_(copy.factory_) {}
    ObjectFactory<T>* factory_;
  };
  ObjectFactory<T>* factory_;

  mutable ThreadLocal<FactoryFreeList> freelist_;

  // Return the free list for this thread.
  // REQUIRES: The returned object should only be accessed from this thread
  FactoryFreeList* GetFreeList() const { return freelist_.pointer(); }
};

template <class T>
ThreadLocalFreeList<T>::ThreadLocalFreeList(int max_length)
    : factory_(&freelist_internal::GetDefaultTrivialFactoryInstance<T>()),
      freelist_(FactoryFreeList(max_length, factory_)) {}

template <class T>
ThreadLocalFreeList<T>::ThreadLocalFreeList(int max_length,
                                            ObjectFactory<T>* factory)
    : factory_(factory), freelist_(FactoryFreeList(max_length, factory_)) {}

template <class T>
ThreadLocalFreeList<T>::~ThreadLocalFreeList() {}

template <class T>
void ThreadLocalFreeList<T>::Clear() {
  GetFreeList()->Clear();
}

template <class T>
T* ThreadLocalFreeList<T>::New() {
  return GetFreeList()->New();
}

template <class T>
void ThreadLocalFreeList<T>::NewMany(T** ptr, int N) {
  FactoryFreeList* fl = GetFreeList();
  for (int i = 0; i < N; i++) {
    *ptr++ = fl->New();
  }
}

template <class T>
void ThreadLocalFreeList<T>::Delete(T* x) {
  if (x == nullptr) return;
  GetFreeList()->Delete(x);
}

template <class T>
void ThreadLocalFreeList<T>::DeleteMany(T** ptr, int N) {
  FactoryFreeList* fl = GetFreeList();
  for (int i = 0; i < N; i++) {
    fl->Delete(*ptr);
    ptr++;
  }
}

// Returns the number of elements currently in the freelist
template <class T>
int ThreadLocalFreeList<T>::size() const {
  return GetFreeList()->size();
}

template <class T>
bool ThreadLocalFreeList<T>::CheckRep() const {
  return GetFreeList()->CheckRep();
}

template <class T>
bool ThreadLocalFreeList<T>::CanDelete(T* x) const {
  return GetFreeList()->CanDelete(x);
}

template <class T>
std::string ThreadLocalFreeList<T>::DebugString() const {
  return GetFreeList()->DebugString();
}

#endif  // THIRD_PARTY_GLOOP_UTIL_FREELIST_THREADLOCAL_FREELIST_H_
