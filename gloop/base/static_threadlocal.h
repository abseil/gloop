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
#include <functional>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"  // IWYU pragma: keep, ifdef'd
#include "absl/base/config.h"      // IWYU pragma: keep
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
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
// - MutateInCriticalSection() allows mutating the thread-local object in a
// non-atomic way without sacrificing signal safety.
//
// - safe_pointer() is similar to pointer(), but it returns nullptr if the
// object is not yet allocated to the thread or a call to
// MutateInCriticalSection is currently in flight on the thread. It is safe to
// call this function inside a signal handler.
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
      // Check that this thread isn't in a MutateInCriticalSection call. In
      // optimized builds this would cause us to leak the object by taking the
      // path below. At destruction we may also wind up in a cycle in the linked
      // list.
      DCHECK(value != this);

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

  void set_pointer(T* absl_nonnull const obj) {
    DCHECK(obj != nullptr);

    // Ensure we're not in the middle of a MutateInCriticalSection critical
    // section.
    DCHECK(Derived::Var().value.load(std::memory_order_relaxed) != this);

    return set_pointer_internal(obj);
  }

  T& get() const { return *pointer(); }

  T* safe_pointer() const {
    // Read the value pointer from the thread-local struct. Unlike the other
    // method, we don't initialize it here if it's still null because it
    // hasn't been initialized before.
    //
    // The acquire here pairs with the release fences in `pointer` and
    // `set_pointer_internal`.
    //
    void* const p = Derived::Var().value.load(std::memory_order_acquire);

    // In debug builds we must filter out the sentinel set up by
    // MutateInCriticalSection.
#ifndef NDEBUG
    if (p == this) {
      return nullptr;
    }
#endif

    return static_cast<T*>(p);
  }

  // Run the supplied function on the current value of the thread-local,
  // allocating one if it's not already present, in such a way that safe_pointer
  // will return null while the function runs. This allows for making non-atomic
  // mutations to it in such a way that a signal handler interrupting this
  // thread won't see an inconsistent object.
  //
  // REQUIRES: the function must not call back into *this, with exception of the
  // safe_pointer method.
  template <typename F>
  std::invoke_result_t<F, T&> MutateInCriticalSection(F&& f) {
    // We must hide the value from signal handlers while mutating it, as
    // arbitrary mutations are not necessarily atomic and a signal handler may
    // otherwise see an inconsistent state.
    //
    // In debug builds we temporarily set the pointer to a sentinel value that
    // will cause `pointer` to crash if it's later loaded, detecting bugs where
    // we might accidentally leak a new T if the function calls back into us. In
    // optimized builds we shoot for code that may be faster.
    T* absl_nonnull const current = pointer();
    const absl::Cleanup c = [&] { set_pointer_internal(current); };

#ifndef NDEBUG
    set_pointer_internal(this);
#else
    set_pointer_internal(nullptr);
#endif

    return std::invoke(std::forward<F>(f), *current);
  }

  bool is_native_tls() const { return true; }
  static void Destructor(void* p) { delete static_cast<T*>(p); }

 private:
  // Like set_pointer, but allows setting any pointer. Careful—if null is
  // observed by `pointer`, it will cause a new T to be allocated.
  void set_pointer_internal(void* absl_nullable const p) {
    // Before we update the pointer, synchronize with signal handlers that call
    // safe_pointer, for the same reason as described in `pointer` above.
    std::atomic_signal_fence(std::memory_order_release);

    // Update the pointer.
    Derived::Var().value.store(p, std::memory_order_relaxed);

    // Ensure that the compiler doesn't reorder modifications to the object we
    // just unpublished that happen after we return to before unpublishing it.
    // Doing so would cause an interrupting signal handler to potentially use a
    // partially modified or destroyed object.
    //
    // It would be great to use std::atomic_signal_fence for this, but the
    // standard offers no way to do so that isn't a tortured and inefficient
    // mess (https://stackoverflow.com/q/79808589/1505451). So instead we match
    // the kernel (http://shortn/_24XvjEMkLd), other code in base/
    // (http://shortn/_HoKRcPm8UA), and elsewhere in google3
    // (http://shortn/_ZBWgpnMIIP) in using an old-fashioned inline assembly
    // barrier to avoid compiler reordering.
    //
    // Clang's documentation about this sort of thing is poor compared to gcc,
    // but gcc's docs at least imply this guarantee
    // (https://stackoverflow.com/q/67943540/1505451), and with respect to this
    // kind of old school C-like thing clang generally tries to be like gcc.
    //
    // See also http://b/458798425.
    //
    // Note that __GNUC__ doesn't mean specifically gcc; it also includes clang
    // (https://stackoverflow.com/a/55926503/1505451).
#if defined(__GNUC__)
    // NOLINTNEXTLINE(google3-runtime-inline-assembly): b/458798425
    __asm__ __volatile__("" : : : "memory");
#else
    // HACK(jacobsa): on other platforms we simply leave this code as incorrect.
    // This is not great, but this function was incorrect for almost two decades
    // before this problem was fixed and nobody noticed, so it must not be a
    // large concern in practice. Fixing http://b/458798425 would make it easy
    // to fix this.
#endif
  }
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
                                                                             \
    template <typename _Func_>                                               \
    std::invoke_result_t<_Func_, _Type_&> MutateInCriticalSection(           \
        _Func_&& f) {                                                        \
      return Base::MutateInCriticalSection(std::forward<_Func_>(f));         \
    }                                                                        \
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

namespace base {
namespace internal {

template <typename T, typename Derived>
class StaticThreadLocalImpl {
 public:
  StaticThreadLocalImpl() = default;
  StaticThreadLocalImpl(const StaticThreadLocalImpl&) = delete;
  StaticThreadLocalImpl& operator=(const StaticThreadLocalImpl&) = delete;

  T* pointer() const {
    PerThread::Key& key = Derived::Key();
    PerThread::Allocate(&key, Destructor);
    void** loc = PerThread::Data(key);

    void* const value = *loc;
    DCHECK(value != this);

    auto* obj = static_cast<T*>(value);
    if (!obj) {
      *loc = obj = Derived::New();
      ::absl::IgnoreLeak(obj);
    }

    return obj;
  }

  T& get() const { return *pointer(); }

  T* safe_pointer() const {
    const PerThread::Key& key = Derived::Key();
    if (key == PerThread::kInvalid) return nullptr;

    void* const p = PerThread::GetData(key);

#ifndef NDEBUG
    if (p == this) {
      return nullptr;
    }
#endif

    return static_cast<T*>(p);
  }

  void set_pointer(T* absl_nonnull obj) {
    DCHECK(obj != nullptr);
    DCHECK(*PerThread::Data(Derived::Key()) != this);
    set_pointer_internal(obj);
  }

  template <typename F>
  std::invoke_result_t<F, T&> MutateInCriticalSection(F&& f) {
    T* absl_nonnull const current = pointer();
    const absl::Cleanup c = [&] { set_pointer_internal(current); };

#ifndef NDEBUG
    set_pointer_internal(this);
#else
    set_pointer_internal(nullptr);
#endif

    return std::invoke(std::forward<F>(f), *current);
  }

  bool is_native_tls() const { return false; }
  static void Destructor(void* obj) { delete static_cast<T*>(obj); }

 private:
  void set_pointer_internal(void* absl_nullable const p) {
    *PerThread::Data(Derived::Key()) = p;
  }
};

}  // namespace internal
}  // namespace base

#define STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(_Type_, _var_, _args_)       \
  ABSL_CONST_INIT static PerThread::Key s_key_##_var_{PerThread::kInvalid};    \
  namespace {                                                                  \
  class ThreadLocal_##_var_                                                    \
      : private ::base::internal::StaticThreadLocalImpl<_Type_,                \
                                                        ThreadLocal_##_var_> { \
   private:                                                                    \
    using Base = typename ThreadLocal_##_var_::StaticThreadLocalImpl;          \
    friend Base;                                                               \
                                                                               \
    static PerThread::Key& Key() { return s_key_##_var_; }                     \
    static _Type_* New() { return new _Type_ _args_; }                         \
                                                                               \
   public:                                                                     \
    _Type_* pointer() const { return Base::pointer(); }                        \
    _Type_& get() const { return Base::get(); }                                \
    _Type_* safe_pointer() const { return Base::safe_pointer(); }              \
    void set_pointer(_Type_* absl_nonnull obj) { Base::set_pointer(obj); }     \
    bool is_native_tls() const { return Base::is_native_tls(); }               \
    static void Destructor(void* p) { Base::Destructor(p); }                   \
                                                                               \
    template <typename _Func_>                                                 \
    std::invoke_result_t<_Func_, _Type_&> MutateInCriticalSection(             \
        _Func_&& f) {                                                          \
      return Base::MutateInCriticalSection(std::forward<_Func_>(f));           \
    }                                                                          \
  };                                                                           \
  }                                                                            \
  static ThreadLocal_##_var_ _var_;

#define STATIC_THREAD_LOCAL_POD(_Type_, _var_) \
  STATIC_THREAD_LOCAL(_Type_, _var_)

#endif  // no compiler-supported TLS

#define STATIC_THREAD_LOCAL(_Type_, _var_) \
  STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(_Type_, _var_, ())

#endif  // THIRD_PARTY_GLOOP_BASE_STATIC_THREADLOCAL_H_
