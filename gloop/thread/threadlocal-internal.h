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

// Helper classes for threadlocal.h.  Not for use by clients.
//
// Representation:
//
// (A) Every time we allocate an application-specific thread-local T
// object, it is as a part of an Instance object.
//
// (B) All Instance objects for a thread are stored in a linked list
// for that thread.  This list is used when the thread is about
// to go away.
//
// (C) All Instance objects for a particular ThreadLocal<> are
// stored in a linked list for that ThreadLocal<>.  This list is used
// when the ThreadLocal<> is about to go away.
//
// (D) Each ThreadLocal<> is assigned a numeric id drawn from
// a small dense space.
//
// (E) For each thread, we keep a vector indexed by ids.
// If a particular ThreadLocal<> has id X, the pointer to the
// ThreadLocal<> instance for thread T is stored in the Xth slot in
// T's vector.

#ifndef THIRD_PARTY_GLOOP_THREAD_THREADLOCAL_INTERNAL_H_
#define THIRD_PARTY_GLOOP_THREAD_THREADLOCAL_INTERNAL_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/gtl/intrusive_list.h"

namespace thread {
namespace local {
namespace internal {

class Instance;
class ThreadInfo;
class Var;

// Every allocated T (for a particular thread) is wrapped in an Instance. These
// objects are reference-counted.
//
// The Instance class derives from intrusive_link so we can chain together
// Instances that belong to a Var in one list
class Instance : public gtl::intrusive_link<Instance> {
 public:
  Instance() : refcount_(1) {}

  // This type is neither copyable nor movable.
  Instance(const Instance&) = delete;
  Instance& operator=(const Instance&) = delete;

  // Create a copy of this object. (A freshly allocated object different from
  // this one, with one reference owned by the caller)
  virtual Instance* Clone() = 0;

  // Add and remove references.
  void Ref() const {
    DCHECK_GE(refcount_.load(std::memory_order_relaxed), 1);
    refcount_.fetch_add(1, std::memory_order_relaxed);
  }

  void Unref() const {
    DCHECK_GT(refcount_.load(std::memory_order_relaxed), 0);
    if (refcount_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
      delete this;
    }
  }

 protected:
  virtual ~Instance();  // Reference counted!

  // Pointer to the actual T*
  void* ptr_;

  // The thread to which this instance belongs
  ThreadInfo* thread_;

  friend class Var;
  friend class ThreadInfo;

 private:
  mutable std::atomic<int32_t> refcount_;
};

// Var holds the internal representation for a particular ThreadLocal<T>
// as well as holding all per-thread instances created for this
// ThreadLocal<T>
class Var {
 public:
  explicit Var(Instance* prototype);

  // This type is neither copyable nor movable.
  Var(const Var&) = delete;
  Var& operator=(const Var&) = delete;

  ~Var();

  // ThreadLocal::pointer() enforces const-correctness.
  void* Get() const;

  template <class T>
  void ForEachUnlocked(::util::functional::CallbackFunctor<T*> v);
  template <class T>
  void ForEachUnlocked(::util::functional::CallbackFunctor<const T&> v) const;

 private:
  // Index into internal array
  size_t id_;

  // Prototype object used to generate new instances for threads that
  // need them.
  Instance* prototype_;

  // Slow path for Get()
  void* SlowGet(absl::Span<Instance*>* instances);

  // List of per-thread instances for this var
  // Protected by global_lock
  // Heap allocated so that it can outlive this var in certain cases
  typedef gtl::intrusive_list<Instance> List;
  List* instances_;

  friend class ThreadInfo;

  // This function adds a new reference (owned by the caller) to every element
  // of instances_ to the given vector. It acquires global_lock.
  void CopyInstances(std::vector<const Instance*>* copy) const;

#ifdef ABSL_HAVE_THREAD_LOCAL
  // If there is native thread_local support, keep a reference to the instances
  // in a native thread_local variable. This allows Var::Get to be fully
  // inlined.

  static absl::Span<Instance*>* PerThreadInstances() {
    return &per_thread_instances_;
  }
  static thread_local absl::Span<Instance*> per_thread_instances_;
#else
  static absl::Span<Instance*>* PerThreadInstances();
#endif
};

// Information kept per thread
class ThreadInfo {
 public:
  ThreadInfo() = default;

  // This type is neither copyable nor movable.
  ThreadInfo(const ThreadInfo&) = delete;
  ThreadInfo& operator=(const ThreadInfo&) = delete;

  ~ThreadInfo();

 private:
  // Instances indexed by Var::id_.
  //
  // Concurrency model:
  // (1) Thread T may index into its items_ array without grabbing any locks
  // (2) Thread T may grow its items_ array while holding global_lock
  // (3) Another thread may index into items_ while holding global_lock
  std::vector<Instance*> items_;

#ifndef ABSL_HAVE_THREAD_LOCAL
  absl::Span<Instance*> per_thread_instances_;
#endif
  friend class Var;
};

// These don't have to be super-fast, but it's really bad for them to hold the
// lock. So we copy out the list of instances.
template <class T>
void Var::ForEachUnlocked(::util::functional::CallbackFunctor<T*> v) {
  std::vector<const Instance*> copy;
  CopyInstances(&copy);
  for (std::vector<const Instance*>::size_type i = 0; i < copy.size(); ++i) {
    v->Run(static_cast<T*>(copy[i]->ptr_));
    copy[i]->Unref();
  }
}

template <class T>
void Var::ForEachUnlocked(
    ::util::functional::CallbackFunctor<const T&> v) const {
  std::vector<const Instance*> copy;
  CopyInstances(&copy);
  for (std::vector<const Instance*>::size_type i = 0; i < copy.size(); ++i) {
    v->Run(*static_cast<T*>(copy[i]->ptr_));
    copy[i]->Unref();
  }
}

// Fast path access to thread-local variable
inline void* Var::Get() const {
  absl::Span<Instance*>* instances = PerThreadInstances();
  const size_t id = id_;
  if (id < instances->size()) {
    Instance* x = (*instances)[id];
    if (x != nullptr) {
      return x->ptr_;
    }
  }
  return const_cast<Var*>(this)->SlowGet(instances);
}

}  // end namespace internal
}  // end namespace local
}  // end namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_THREADLOCAL_INTERNAL_H_
