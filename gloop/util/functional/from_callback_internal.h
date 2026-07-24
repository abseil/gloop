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

// Implementation details for functor to callback conversion.  Do not include
// this header file directly.  Instead, use "from_callback.h"

// IWYU pragma: private, include "util/functional/from_callback.h"
#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_FROM_CALLBACK_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_FROM_CALLBACK_INTERNAL_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

// For CallbackToSig<>
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/log/die_if_null.h"
#include "gloop/base/callback-types.h"
#include "gloop/util/functional/callable_once.h"
#include "gloop/util/functional/to_callback_internal.h"  // IWYU pragma: export
#include "gloop/util/functional/to_shared_function.h"
#include "gloop/util/refcount/compact_reference_counted.h"
#include "gloop/util/refcount/reffed_ptr.h"

// Forward declaration to set up inlining on conversion.
class BlockingClosure;

namespace testing {

// Forward declaration to set up inlining on conversion.
template <typename F>
class MockCallback;

}  // namespace testing

namespace util {
namespace functional {
namespace internal {

// We use a specialization to allow convenient extraction of the signature.
template <typename CallbackType, typename Sig>
class CallbackFunctorImpl;

template <typename CallbackType, typename Sig>
class OwningCallbackFunctorImpl;

// Underlying functor for FromCallback.
template <typename CallbackType, typename R, typename... Args>
class CallbackFunctorImpl<CallbackType, R(Args...)> {
 public:
  explicit CallbackFunctorImpl(CallbackType* callback) : callback_(callback) {}

  R operator()(Args... args) const {
    return callback_->Run(std::forward<Args>(args)...);  // NOLINT
  }

 private:
  CallbackType* callback_;
};

struct CallbackDeleter : ::refcount::CompactReferenceCounted<CallbackDeleter> {
  explicit CallbackDeleter(
      std::unique_ptr<::base::internal::CallbackBase> callback)
      : callback(std::move(callback)) {}

  std::unique_ptr<::base::internal::CallbackBase> callback;
};

// Underlying functor for FromCallbackTakesOwnership. This is complicated
// because of non-repeatable callbacks: if the callback is non-repeatable,
// then when we call Run() we must ensure that we don't attempt to delete it
// again in the destructor.
template <typename CallbackType, typename R, typename... Args>
class OwningCallbackFunctorImpl<CallbackType, R(Args...)> {
 public:
  explicit OwningCallbackFunctorImpl(CallbackType* callback)
      : callback_(callback),
        deleter_(callback ? refcount::MakeReffed<CallbackDeleter>(
                                absl::WrapUnique(callback))
                          : nullptr) {}

  R operator()(Args... args) const {
    if (deleter_) {
      (void)ABSL_DIE_IF_NULL(deleter_->callback.get());
    }
    CallbackType* to_call = ABSL_DIE_IF_NULL(callback_);
    if (!to_call->IsRepeatable()) {
      // Temporary callbacks promise to be self-deleting.  We clear the deleter
      // so that is not deleted again.
      deleter_->callback.release();
    }
    return to_call->Run(std::forward<Args>(args)...);  // NOLINT
  }

 private:
  CallbackType* callback_;
  refcount::reffed_ptr<CallbackDeleter> deleter_;
};

// Convenience types that specifies the appropriate functors given only
// the callback-type.
template <typename CallbackType>
using CallbackFunctor =
    CallbackFunctorImpl<CallbackType,
                        typename internal::CallbackToSig<CallbackType>::type>;

template <typename CallbackType>
using OwningCallbackFunctor = OwningCallbackFunctorImpl<
    CallbackType, typename internal::CallbackToSig<CallbackType>::type>;

template <class R, class... Args>
class Callback : public ::base::internal::CallbackBase {
 public:
  virtual R Run(Args...) = 0;

 protected:
  using ::base::internal::CallbackBase::CallbackBase;
};

}  // namespace internal

// Forward declaration for inlining below.
template <typename CallbackType>
internal::OwningCallbackFunctor<CallbackType> FromCallbackWithOwnership(
    CallbackType* callback);

namespace internal {

template <typename R, typename... Args>
struct CallbackTypeImpl;

template <>
struct CallbackTypeImpl<void> {
  using type = Closure;
};

template <typename R>
struct CallbackTypeImpl<R> {
  using type = internal::Callback<R>;
};

template <typename A1>
struct CallbackTypeImpl<void, A1> {
  using type = internal::Callback<void, A1>;
};

template <typename R, typename A1>
struct CallbackTypeImpl<R, A1> {
  using type = internal::Callback<R, A1>;
};

template <typename A1, typename A2>
struct CallbackTypeImpl<void, A1, A2> {
  using type = internal::Callback<void, A1, A2>;
};

template <typename R, typename A1, typename A2>
struct CallbackTypeImpl<R, A1, A2> {
  using type = internal::Callback<R, A1, A2>;
};

// A base class holding the impl_, call operator and Run function for each
// arity.
//
// <link> cannot handle pack expansion, so this ensures the Run()
// function to be inlined is not a pack expansion.
//
// TODO: Reenable inlining once a solution to temporary
// introduction is identified.
template <typename R, typename... Args>
class ResultCallbackFunctorImplBase;

template <typename R>
class ResultCallbackFunctorImplBase<R> {
 public:
  R operator()() const { return impl_(); }

  ABSL_DEPRECATE_AND_INLINE() R Run() const { return (*this)(); }

 protected:
  using CallbackType = typename CallbackTypeImpl<R>::type;
  using Impl = std::function<R()>;

  ResultCallbackFunctorImplBase() = default;
  explicit ResultCallbackFunctorImplBase(Impl impl) : impl_(std::move(impl)) {}

  Impl impl_;
};

template <typename R, typename A0>
class ResultCallbackFunctorImplBase<R, A0> {
 public:
  R operator()(A0 a0) const { return impl_(std::forward<A0>(a0)); }

  template <typename X0>
  ABSL_DEPRECATE_AND_INLINE()
  R Run(X0&& a0) const {
    return (*this)(std::forward<X0>(a0));
  }

 protected:
  using CallbackType = typename CallbackTypeImpl<R, A0>::type;
  using Impl = std::function<R(A0)>;

  ResultCallbackFunctorImplBase() = default;
  explicit ResultCallbackFunctorImplBase(Impl impl) : impl_(std::move(impl)) {}

  Impl impl_;
};

template <typename R, typename A0, typename A1>
class ResultCallbackFunctorImplBase<R, A0, A1> {
 public:
  R operator()(A0 a0, A1 a1) const {
    return impl_(std::forward<A0>(a0), std::forward<A1>(a1));
  }

  template <typename X0, typename X1>
  ABSL_DEPRECATE_AND_INLINE()
  R Run(X0&& a0, X1&& a1) const {
    return (*this)(std::forward<X0>(a0), std::forward<X1>(a1));
  }

 protected:
  using CallbackType = typename CallbackTypeImpl<R, A0, A1>::type;
  using Impl = std::function<R(A0, A1)>;

  ResultCallbackFunctorImplBase() = default;
  explicit ResultCallbackFunctorImplBase(Impl impl) : impl_(std::move(impl)) {}

  Impl impl_;
};

template <typename F>
decltype(auto) ToCopyableFunction(F&& f) {
  if constexpr (std::is_copy_constructible_v<F>) {
    return std::forward<F>(f);
  } else {
    return ToSharedFunction(std::forward<F>(f));
  }
}

// A helper type for migration purposes to split caller and callee migration of
// current users of callback types. This type is implicitly convertible from the
// return type of util::functional::ToCallback as well as raw callback pointers,
// meaning that changing a callee from ResultCallbackN<R, A>* to
// ResultCallbackFunctor<R, A> should be a no-op for many callers.
//
// ResultCallbackFunctorImpl is copyable and movable.
template <typename R, typename... Args>
class ABSL_NULLABILITY_COMPATIBLE ResultCallbackFunctorImpl
    : public ResultCallbackFunctorImplBase<R, Args...> {
  using Base = ResultCallbackFunctorImplBase<R, Args...>;
  using Impl = typename Base::Impl;

 public:
  using CallbackType = typename Base::CallbackType;

  // Converting constructor from a functor.
  template <typename F>
    requires(!std::is_same_v<std::decay_t<F>, ResultCallbackFunctorImpl> &&
             IsCallable<FunctorRef</*Permanent=*/true, std::decay_t<F>>,
                        R(Args...)>())
  ResultCallbackFunctorImpl(F&& functor)  // NOLINT(google-explicit-constructor)
      : Base(::util::functional::internal::IsEmpty(functor)
                 ? Impl()
                 : Impl(ToCopyableFunction(std::forward<F>(functor)))) {}

  // Converting constructor from a rvalue-callable-only functor. Historically,
  // calling this twice would be a use-after-free of the callback type
  // constructed with ToCallback(), but now will CHECK-fail.
  template <typename F>
    requires(!std::is_same_v<std::decay_t<F>, ResultCallbackFunctorImpl> &&
             !IsCallable<FunctorRef</*Permanent=*/true, std::decay_t<F>>,
                         R(Args...)>() &&
             IsCallable<FunctorRef</*Permanent=*/false, std::decay_t<F>>,
                        R(Args...)>())
  ABSL_DEPRECATE_AND_INLINE()
  ResultCallbackFunctorImpl(F&& functor)  // NOLINT(google-explicit-constructor)
      : Base(::util::functional::internal::IsEmpty(functor)
                 ? Impl()
                 : Impl(ToCopyableFunction(::util::functional::CallAtMostOnce(
                       std::forward<F>(functor))))) {}

  // Converting constructor from a raw callback pointer (likely a derived
  // callback implementation)
  ABSL_DEPRECATE_AND_INLINE()
  ResultCallbackFunctorImpl(  // NOLINT(google-explicit-constructor,
                              // google3-runtime-inliner-validation)
                              // b/411142993
      CallbackType* cb)
      : Base(cb ? Impl(::util::functional::FromCallbackWithOwnership(cb))
                : Impl()) {}

  // Converting constructor to change MockCallback usage to not rely on callback
  // inheritance.
  template <typename F>
  ABSL_DEPRECATE_AND_INLINE()
  ResultCallbackFunctorImpl(  // NOLINT(google-explicit-constructor)
      ::testing::MockCallback<F>* cb)
      : ResultCallbackFunctorImpl(std::ref(*cb)) {}

  // Converting constructor to change BlockingClosure usage to not rely on
  // callback inheritance.
  template <typename C, typename = std::enable_if_t<std::conjunction_v<
                            std::is_same<CallbackType, Closure>,
                            std::is_same<C, BlockingClosure>>>>
  ABSL_DEPRECATE_AND_INLINE()
  ResultCallbackFunctorImpl(C* c)  // NOLINT(google-explicit-constructor)
      : ResultCallbackFunctorImpl(std::ref(*c)) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  ResultCallbackFunctorImpl(std::nullptr_t) : Base() {}
  ResultCallbackFunctorImpl() : Base() {}

  // ResultCallbackFunctorImpl is copyable and movable.
  ResultCallbackFunctorImpl(const ResultCallbackFunctorImpl& other) = default;
  ResultCallbackFunctorImpl(ResultCallbackFunctorImpl&& other) = default;
  ResultCallbackFunctorImpl& operator=(const ResultCallbackFunctorImpl& other) =
      default;
  ResultCallbackFunctorImpl& operator=(ResultCallbackFunctorImpl&& other) =
      default;

  ABSL_DEPRECATE_AND_INLINE()
  const ResultCallbackFunctorImpl& operator*() const { return *this; }
  ABSL_DEPRECATE_AND_INLINE()
  // NOLINTNEXTLINE(google3-runtime-inliner-validation)
  const ResultCallbackFunctorImpl* operator->() const { return this; }
  // Note that get() returns a reference to this not a pointer: this is
  // intentional. ResultCallbackFunctorImpl is pretending to be a pointer type
  // itself so that the inliner will clean up the pointer-like usages, so this
  // ensures that both the call to get() and the dereference are inlined away.
  ABSL_DEPRECATE_AND_INLINE() const ResultCallbackFunctorImpl& get() const {
    return *this;
  }
  ABSL_DEPRECATE_AND_INLINE()
  void reset(const ResultCallbackFunctorImpl& value) { *this = value; }
  ABSL_DEPRECATE_AND_INLINE()
  void reset(ResultCallbackFunctorImpl&& value) { *this = std::move(value); }
  ABSL_DEPRECATE_AND_INLINE() void reset() { *this = nullptr; }
  ABSL_DEPRECATE_AND_INLINE() ResultCallbackFunctorImpl release() {
    return std::exchange(*this, nullptr);
  }

  ABSL_DEPRECATE_AND_INLINE() bool IsRepeatable() const { return true; }

  bool operator==(std::nullptr_t) const { return this->impl_ == nullptr; }
  friend bool operator==(std::nullptr_t, const ResultCallbackFunctorImpl& f) {
    return f == nullptr;
  }
  bool operator!=(std::nullptr_t) const { return this->impl_ != nullptr; }
  friend bool operator!=(std::nullptr_t, const ResultCallbackFunctorImpl& f) {
    return f != nullptr;
  }
  explicit operator bool() const { return *this != nullptr; }
};

template <typename F>
struct IsResultCallbackFunctorImpl : std::false_type {};

template <typename R, typename... Args>
struct IsResultCallbackFunctorImpl<ResultCallbackFunctorImpl<R, Args...>>
    : std::true_type {};

template <typename F>
constexpr bool IsResultCallbackFunctor = IsResultCallbackFunctorImpl<F>::value;

}  // namespace internal
}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_FROM_CALLBACK_INTERNAL_H_
