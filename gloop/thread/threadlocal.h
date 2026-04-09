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

// A ThreadLocal<Type> object provides a per-thread instance of Type.
// The Type instance is created for a thread on first access in that thread,
// like, when get() or pointer() is called.
//
// For example, you can use the following to keep track of per-thread stats.
//
//      static ThreadLocal<int64> counter;
//
//      void SomeFunction() {
//        counter.set(counter.get() + 1);
//        ...;
//      }
//
// Please note that since C++11, the recommended way to implement a thread-local
// variable is the thread_local keyword (see <link> and
// <link>).
//
// Caveats:
// (a) It is often very convenient to declare a global "ThreadLocal<T>"
//     object.  Be warned that C++ initialization order for global
//     objects is non-deterministic, and therefore you should not access
//     such a global object until after main() has started.
//     Note: ThreadLocal has an exception to the style guide rules for global
//     variables with regards to destruction. The rules for initialization
//     must still be followed for the type contained within the ThreadLocal.
//
// (b) get/set/pointer operations typically take 50 cycles
//     (18ns on 2.8 GHz machine)
//
// (c) Each per-thread instance value of a ThreadLocal<T> is always
//     initialized via default construction or copy construction.
//     When T is a primitive type default construction means 0-initialization.
//
// (d) As long as ThreadLocal<T> objects are default-constructed and
//     changed only via pointer() method,
//     T does not have to have a copy constructor
//     or assignment operator publicly defined.
//     Thus, one can e.g. create ThreadLocal<std::unique_ptr<T>>
//     to have thread-local pointers that own and deallocate
//     the objects they refer to.
//
// (e) ThreadLocal<T> objects do proper cleanup for the thread-local instances
//     of objects of type T:
//     - T's destructor for the thread-local instances in all threads
//       is called when a ThreadLocal<T> object is destroyed
//     - Ti's destructors for all thread-local instances of all
//       ThreadLocal<Ti> for a given thread are called when the thread exits
//       or soon after. (If an iteration via ForEachUnlocked is in progress, the
//       destructor will not run until after the loop has completed)
//     Thus if the used T owns (is responsible for) some data on the heap
//     or other resources it's important to define T's destructor
//     to do appropriate cleanup.
//     I.e. replacing ThreadLocal<Foo*> with ThreadLocal<std::unique_ptr<Foo>>
//     will in many cases eliminate memory leaks without any adverse effects.
//     Exception: if ThreadLocal<T> is a global object, no destructors will be
//     run on process exit.
//
// (f) It is not safe to access ThreadLocal objects from within a signal
//     handler.  If you need to access a thread-local static from within a
//     signal handler, consider the safe_pointer() method on the
//     STATIC_THREAD_LOCAL family of constructs from
//     https://github.com/abseil/gloop/tree/main/gloop/base/static_threadlocal.h.
//
// (g) Interfaces such as Executors and Fibers do not guarantee that the
//     underlying threads are replaced or reinitialized between operations.
//     Callers may not depend on ThreadLocal objects being in a newly
//     constructed state. For example, after a fiber completes, a subsequently
//     executed fiber might reuse the thread it had been allocated and observe
//     'left-over' thread-local state. This caveat applies to all thread-local
//     storage implementations.

#ifndef THIRD_PARTY_GLOOP_THREAD_THREADLOCAL_H_
#define THIRD_PARTY_GLOOP_THREAD_THREADLOCAL_H_

#include <functional>

#include "gloop/thread/config.h"  // IWYU pragma: keep

// When building some platforms, this file is effectively replaced by its analog
// in port/.
#if THREAD_HAVE_ALTERNATE_THREAD_LOCAL

#include "gloop/thread/port/threadlocal.h"

#else

#include <pthread.h>

#include <functional>

#include "gloop/thread/threadlocal-internal.h"

template <typename T>
class Callback1;

template <class Type>
class ThreadLocal {
 public:
  // Create a thread-local variable -- it contains a different "Type"
  // instance for each thread.  Each per-thread instance is
  // initialized with the default value for Type (0 for primitive types).
  ThreadLocal() : rep_(new DefaultObjectCreator) {}

  // Create a thread-local variable -- it contains a different "Type"
  // instance for each thread.  Each per-thread instance is
  // initialized with the supplied value.
  // Type must define a copy constructor for one to use this constructor.
  explicit ThreadLocal(const Type& value) : rep_(new CopyObject(value)) {}

  // Destroys the thread-local variable
  ~ThreadLocal() {}

  // The set/get operations are moderately expensive -- on the order of
  // 50 cycles typically.  For speed, you may want to call the
  // "pointer" method and hold on the result value.  For example, the
  // counter increment described at the top of the file can also
  // be done as follows:
  //    int64* ptr = counter.pointer();
  //    *ptr = *ptr + 1;
  // This method is also essential if Type does not define an assignment
  // operator but is still mutable.
  Type* pointer() { return reinterpret_cast<Type*>(rep_.Get()); }
  const Type* pointer() const { return reinterpret_cast<Type*>(rep_.Get()); }

  // Get/set the value for this thread.
  // Using set requires the assignment operator defined for Type.
  const Type& get() const { return *(pointer()); }
  void set(const Type& v) { *(pointer()) = v; }

  // Execute v->Run(instance) for each live per-thread instance of this
  // ThreadLocal. *v must be repeatable, and ForEachUnlocked() takes ownership
  // of it.
  //
  // ForEachUnlocked() performs no locking on the underlying per-thread
  // objects. The client is responsible for making sure that the accesses are
  // safe. (See // <link>Main/MultiCore101#The_backward_mutex for one
  // possibility; or alternatively, the underlying objects could be
  // thread-safe.)
  //
  // ForEachUnlocked() may visit instances of threads that exited after the
  // iteration began (delaying destruction of those instances--see above), and
  // may fail to visit instances of threads created after the iteration began.
  void ForEachUnlocked(Callback1<Type*>* v) { rep_.ForEachUnlocked<Type>(v); }
  void ForEachUnlocked(Callback1<const Type&>* v) const {
    rep_.ForEachUnlocked<Type>(v);
  }

  // std::function variants of ForEachUnlocked.
  void ForEachUnlocked(const std::function<void(Type*)>& v) {
    rep_.ForEachUnlocked<Type>(v);
  }
  void ForEachUnlocked(const std::function<void(const Type&)>& v) const {
    rep_.ForEachUnlocked<Type>(v);
  }

 private:
  // A wrapper for Type that contains a default constructed object
  class DefaultObject : public thread::local::internal::Instance {
   private:
    Type object_;

   public:
    DefaultObject() : object_() { ptr_ = &object_; }
    virtual Instance* Clone() { return new DefaultObject; }
  };

  // A wrapper for Type when we want to use the copy constructor
  class CopyObject : public thread::local::internal::Instance {
   private:
    Type object_;

   public:
    explicit CopyObject(const Type& src) : object_(src) { ptr_ = &object_; }
    virtual Instance* Clone() { return new CopyObject(object_); }
  };

  // We have a special prototype for zero-arg constructor instead
  // of just using DefaultObject so that we can avoid creating
  // an extra instance of Type() when allocating the prototype.
  class DefaultObjectCreator : public thread::local::internal::Instance {
   public:
    DefaultObjectCreator() { ptr_ = nullptr; }
    virtual Instance* Clone() { return new DefaultObject; }
  };

  thread::local::internal::Var rep_;

  // Templatized variant of DISALLOW_COPY_AND_ASSIGN()
  ThreadLocal(const ThreadLocal<Type>&);
  void operator=(const ThreadLocal<Type>&);
};

#endif  // !THREAD_HAVE_ALTERNATE_THREAD_LOCAL

#endif  // THIRD_PARTY_GLOOP_THREAD_THREADLOCAL_H_
