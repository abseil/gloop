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

// DEPRECATED: Prefer the `thread_local` storage specifier when you need static,
// threadlocal storage.  See <link> .

#ifndef THIRD_PARTY_GLOOP_BASE_STATIC_THREADLOCAL_H_
#define THIRD_PARTY_GLOOP_BASE_STATIC_THREADLOCAL_H_

#include <atomic>

#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/debugging/leak_check.h"  // IWYU pragma: keep
#include "absl/log/check.h"
#include "gloop/base/per_thread.h"

//
// This file provides three macros for defining thread-local variables.
//
// STATIC_THREAD_LOCAL(Type,var) is similar to ThreadLocal<Type> var
// (see thread/threadlocal.h), but it always declares a static
// variable. You can use this macro only in a .cc file.
//
// STATIC_THREAD_LOCAL_POD is a variation of STATIC_THREAD_LOCAL that
// restricts the Type to be a Plain Old Data type (bool, int, double,
// pointers, ...).
//
// STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS allows passing arguments
// to the constructor used to create a new thread-local
// object. STATIC_THREAD_LOCAL(Type, var) is equivalent to
// STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(Type, var, ()).
//
// These macros are faster than ThreadLocal especially on a system
// with compiler-supported TLS.
//
// A variable declared by STATIC_THREAD_*(Type, var) exports three key
// methods:
//
// - pointer() returns the pointer to the thread-local object.  It
// allocates a new object when called for the first time by the
// thread.
//
// - safe_pointer() is similar to pointer(), but it returns nullptr if
// the object is not yet allocated to the thread. It is safe to call
// this function inside a signal handler.
//
// - get() just returns *pointer().
//
// The thread-local object is automatically deleted when the thread
// exits.  Note that unlike ThreadLocal<Type>, "var" does not export
// "set" method so that a noncopyable Type can be supported.
//
// CAUTION:
//
// On some platforms (but not Google production), each STATIC_THREAD_LOCAL*
// variable may allocate a PerThread::Key element. There is a limit on the
// number of PerThread::Keys per process, so do not create too many
// STATIC_THREAD_LOCAL* variables. If you want to create many thread local
// variables, use the ThreadLocal class in thread/threadlocal.h instead.
//
// EXAMPLE:
//
// class Foo {
// public:
//   Foo() {...};
//   ...
// };
//
// class Bar {
// public:
//   Bar(int x) {...};
//   ...
// };
//
// STATIC_THREAD_LOCAL_POD(int, var);
// STATIC_THREAD_LOCAL(Foo, foo_var);
// STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(Bar, bar_var, (5));
//
// static ThreadBody() {
//    *var.pointer() = 100;
//
// }
//

#if BASE_INTERNAL_PER_THREAD_TLS

// Compiler-supported TLS.
//
// Variable s_obj_VAR stores a pointer to the object, a pointer to
// Destructor(), which deletes the object, and a "next" pointer for the
// per-thread list with head "g_per_thread_key".  The list's destructor zeroed
// the pointer to each object and calls its destructor.  The zeroing of the
// pointer ensures that if a destructor uses another static_threadlocal
// variable, it will be recreated and then destroyed in another run of the
// destructors.

namespace base {
namespace internal {

extern PerThread::Key g_per_thread_key;  // head of per-thread linked list

// The data we store per thread and per instance.
struct StaticThreadLocalList {
  // Next item in the per-thread linked list.
  StaticThreadLocalList* next = nullptr;

  // Type-specific destructor for the value stored below.
  void (*destructor)(void*) = nullptr;

  // Pointer to the value.
  //
  // This is an atomic because we need to be able to synchronize with signal
  // handlers that call safe_pointer.
  std::atomic<void*> value{nullptr};
};

void StaticThreadLocalRegister(StaticThreadLocalList* variable,
                               void (*destructor)(void*));

void StaticThreadLocalDestructor(void* p);  // destructor for linked list

template <typename T, typename Derived>
class StaticThreadLocalImpl {
 public:
  StaticThreadLocalImpl() = default;
  StaticThreadLocalImpl(const StaticThreadLocalImpl&) = delete;
  StaticThreadLocalImpl& operator=(const StaticThreadLocalImpl&) = delete;

  T* pointer() const {
    // Set up a slightly more readable name for our variable.
    base::internal::StaticThreadLocalList& stll = Derived::Var();

    // Fast path: did we already initialize on this thread?
    //
    // We don't need anything more than a relaxed load here, because this is
    // a thread-local atomic: it's only written by this method, which can
    // run only serially on this thread. It can't be written by a signal
    // handler because a signal handler isn't allowed to call this method
    // (since it allocates).
    if (void* const value = stll.value.load(std::memory_order_relaxed);
        ABSL_PREDICT_TRUE(value != nullptr)) {
      return static_cast<T*>(value);
    }

    // This is the first call, so we must initialize. Note that there is no
    // race in this logic, since this is a thread-local variable and signal
    // handlers are only allowed to use the non-initializing safe_pointer
    // method.

    // Create the object using the user-supplied constructor arguments.
    auto* const obj = Derived::New();

    // Before we update the value pointer, add a release fence to pair with
    // the acquire load of the value in the safe_pointer method used by
    // signal handlers. This means that all of the side effects of the
    // constructor we ran above will be finished before the pointer can be
    // accessed by a signal handler, ensuring a signal handler will never
    // see a partially-initialized object. It will always see either no
    // object or a fully-initialized one.
    //
    // We do this rather than using a release store because we don't need a
    // CPU-level barrier: we're not trying to synchronize with other
    // threads, only with a signal handler that interrupts this thread while
    // we're initializing. The signal fence prevents compiler re-ordering,
    // but has no runtime cost beyond that.
    std::atomic_signal_fence(std::memory_order_release);

    // Now we can publish the newly-initialized object.
    stll.value.store(obj, std::memory_order_relaxed);
    StaticThreadLocalRegister(&stll, &Destructor);

    return obj;
  }

  void set_pointer(T* absl_nonnull obj) {
    DCHECK(obj != nullptr);
    // Release fence to synchronize with signal handlers that call
    // safe_pointer, for the same reason as in the method above.
    std::atomic_signal_fence(std::memory_order_release);
    Derived::Var().value.store(obj, std::memory_order_relaxed);
  }

  T& get() const { return *pointer(); }

  T* safe_pointer() const {
    // Read the value pointer from the thread-local struct. Unlike the other
    // method, we don't initialize it here if it's still null because it
    // hasn't been initialized before.
    //
    // The acquire here pairs with the release fence in the initialization
    // logic in the pointer method.
    //
    return static_cast<T*>(
        Derived::Var().value.load(std::memory_order_acquire));
  }

  bool is_native_tls() const { return true; }
  static void Destructor(void* p) { delete static_cast<T*>(p); }
};

}  // namespace internal
}  // namespace base

#define STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(_Type_, _var_, _args_)     \
  static thread_local base::internal::StaticThreadLocalList s_obj_##_var_;   \
  namespace {                                                                \
  class ThreadLocal_##_var_                                                  \
      : private base::internal::StaticThreadLocalImpl<_Type_,                \
                                                      ThreadLocal_##_var_> { \
   private:                                                                  \
    using Base = typename ThreadLocal_##_var_::StaticThreadLocalImpl;        \
    friend Base;                                                             \
                                                                             \
    static base::internal::StaticThreadLocalList& Var() {                    \
      return s_obj_##_var_;                                                  \
    }                                                                        \
    static _Type_* New() { return new _Type_ _args_; }                       \
                                                                             \
    /* Use forwarding functions to make it easier to query in GWP. */        \
   public:                                                                   \
    _Type_* pointer() const { return Base::pointer(); }                      \
    _Type_& get() const { return Base::get(); }                              \
    _Type_* safe_pointer() const { return Base::safe_pointer(); }            \
    void set_pointer(_Type_* absl_nonnull obj) { Base::set_pointer(obj); }   \
    bool is_native_tls() const { return Base::is_native_tls(); }             \
    static void Destructor(void* p) { Base::Destructor(p); }                 \
  };                                                                         \
  }                                                                          \
  static ThreadLocal_##_var_ _var_;

// For POD types in TLS mode, s_obj_VAR is the thread-local variable.
#define STATIC_THREAD_LOCAL_POD(_Type_, _var_)                           \
  static thread_local _Type_ s_obj_##_var_;                              \
  namespace {                                                            \
  class ThreadLocal_##_var_ {                                            \
   public:                                                               \
    ThreadLocal_##_var_() {}                                             \
    inline _Type_* pointer() const { return &s_obj_##_var_; }            \
    inline _Type_* safe_pointer() const { return &s_obj_##_var_; }       \
    _Type_& get() const { return s_obj_##_var_; }                        \
    bool is_native_tls() const { return true; }                          \
                                                                         \
   private:                                                              \
    ThreadLocal_##_var_(const ThreadLocal_##_var_&) = delete;            \
    ThreadLocal_##_var_& operator=(const ThreadLocal_##_var_&) = delete; \
  } _var_;                                                               \
  }

#else

//
// No compiler-supported TLS. Use PerThread
//

#define STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(_Type_, _var_, _args_)    \
  ABSL_CONST_INIT static PerThread::Key s_key_##_var_{PerThread::kInvalid}; \
  namespace {                                                               \
  class ThreadLocal_##_var_ {                                               \
   public:                                                                  \
    ThreadLocal_##_var_() {}                                                \
    _Type_* pointer() const {                                               \
      PerThread::Allocate(&s_key_##_var_, Destructor);                      \
      void** loc = PerThread::Data(s_key_##_var_);                          \
      auto* obj = static_cast<_Type_*>(*loc);                               \
      if (!obj) {                                                           \
        *loc = obj = new _Type_ _args_;                                     \
        ::absl::IgnoreLeak(obj);                                            \
      }                                                                     \
      return obj;                                                           \
    }                                                                       \
    _Type_& get() const { return *pointer(); }                              \
    _Type_* safe_pointer() const {                                          \
      if (s_key_##_var_ == PerThread::kInvalid) return nullptr;             \
      return static_cast<_Type_*>(PerThread::GetData(s_key_##_var_));       \
    }                                                                       \
    void set_pointer(_Type_* absl_nonnull obj) {                            \
      *PerThread::Data(s_key_##_var_) = obj;                                \
    }                                                                       \
    bool is_native_tls() const { return false; }                            \
                                                                            \
   private:                                                                 \
    static void Destructor(void* obj) { delete static_cast<_Type_*>(obj); } \
    ThreadLocal_##_var_(const ThreadLocal_##_var_&) = delete;               \
    ThreadLocal_##_var_& operator=(const ThreadLocal_##_var_&) = delete;    \
  };                                                                        \
  }                                                                         \
  static ThreadLocal_##_var_ _var_;

#define STATIC_THREAD_LOCAL_POD(_Type_, _var_) \
  STATIC_THREAD_LOCAL(_Type_, _var_)

#endif  // no compiler-supported TLS

#define STATIC_THREAD_LOCAL(_Type_, _var_) \
  STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(_Type_, _var_, ())

#endif  // THIRD_PARTY_GLOOP_BASE_STATIC_THREADLOCAL_H_
