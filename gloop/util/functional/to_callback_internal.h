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
// this header file directly.  Instead, use "to_callback.h"

// IWYU pragma: private, include "util/functional/to_callback.h"
#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_INTERNAL_H_

#include <type_traits>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "gloop/base/callback-types.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace util {
namespace functional {
namespace internal {

// For non-permanent callbacks, the bound functor is destroyed once it has been
// called; we cast to allow potential move optimizations in this case.
template <bool Permanent, typename Functor>
using FunctorRef =
    typename std::conditional<Permanent, Functor&, Functor&&>::type;

// IsCallback<T>() is true iff T is a callback type.
template <typename T>
constexpr bool IsCallback() {
  return std::is_base_of_v<base::internal::CallbackBase, T>;
}

// Extracts the non-const method Run function signature
// (as CallbackToSig<C>::type) for any type that has a non-const Run method.
template <typename C>
class CallbackToSig {
  template <typename F>
  struct MFSig;
  template <typename R, typename U, typename... A>
  struct MFSig<R (U::*)(A...)> {
    using type = R(A...);
  };

 public:
  // Extract the call signature directly from the type of &C::Run.
  using type = typename MFSig<decltype(&C::Run)>::type;
};

// We immediately specialize below to extract Sig.
template <typename CallbackType, bool Permanent, typename Functor,
          typename Sig = typename CallbackToSig<CallbackType>::type>
class FunctorCallback;

// This implementation provides all callback specializations.
template <typename CallbackType, bool Permanent, typename Functor, typename R,
          typename... Args>
class FunctorCallback<CallbackType, Permanent, Functor, R(Args...)> final
    : public CallbackType {
 public:
  // Always move-constructed by FunctorCallbackBinder.
  explicit FunctorCallback(
      Functor&& functor, perftools::tracing::StringLabel label =
                             perftools::tracing::TraceSourceLocation::current())
      : CallbackType(
            std::conditional_t<Permanent, ::base::Context::DefaultInitType,
                               ::base::Context::ThreadInitType>(),
            label),
        functor_(std::move(functor)),
        label_(std::move(label)) {}

  // This type is neither copyable nor movable.
  FunctorCallback(const FunctorCallback&) = delete;
  FunctorCallback& operator=(const FunctorCallback&) = delete;

  R Run(Args... args) override {
    // Some notes on the invocation below:
    // - For non-void types, the conversion above has already been validated by
    //   IsCallable.
    // - std::forward allows the use of move-only arguments.
    if constexpr (Permanent) {
      if constexpr (std::is_void_v<R>) {
        functor()(std::forward<Args>(args)...);
      } else {
        return static_cast<R>(functor()(std::forward<Args>(args)...));
      }
    } else {
      base::WithContext with(std::move(this->context_), label_);
      auto delete_self = absl::Cleanup([this] { delete this; });
      if constexpr (std::is_void_v<R>) {
        functor()(std::forward<Args>(args)...);
      } else {
        return static_cast<R>(functor()(std::forward<Args>(args)...));
      }
    }
  }

  bool IsRepeatable() const override { return Permanent; }

 private:
  // Returns `functor_` cast to the proper lvalue or rvalue function reference.
  inline FunctorRef<Permanent, Functor> functor() {
    return static_cast<FunctorRef<Permanent, Functor>>(functor_);
  }

  Functor functor_;
  const perftools::tracing::StringLabel label_;
};

template <typename F, typename Sig, typename E = void>
struct IsCallableImpl : std::false_type {};

template <typename F, typename R, typename... Args>
struct IsCallableImpl<
    F, R(Args...),
    typename std::enable_if<
        std::is_convertible<
            decltype(std::declval<F>()(std::declval<Args>()...)), R>::value ||
        std::is_void<R>::value>::type> : std::true_type {};

// Evaluates whether functor "F" is callable with signature "Sig".
template <typename F, typename Sig>
constexpr bool IsCallable() {
  return IsCallableImpl<F, Sig>::value;
}

// FunctorCallbackBinder allows desired Callback-type to be deduced via
// conversion operators as we cannot otherwise overload ToCallback*'s result
// type.
template <typename Functor, bool Permanent>
class FunctorCallbackBinder {
 public:
  explicit FunctorCallbackBinder(
      const Functor& functor,
      perftools::tracing::StringLabel label =
          perftools::tracing::TraceSourceLocation::current())
      : functor_(functor), label_(std::move(label)) {}

  explicit FunctorCallbackBinder(
      Functor&& functor, perftools::tracing::StringLabel label =
                             perftools::tracing::TraceSourceLocation::current())
      : functor_(std::move(functor)), label_(std::move(label)) {}

  // FunctorCallbackBinder is neither copyable nor movable. It must be
  // immediately converted to a callback or back to the original functor.
  FunctorCallbackBinder(const FunctorCallbackBinder&) = delete;
  FunctorCallbackBinder& operator=(const FunctorCallbackBinder&) = delete;
  FunctorCallbackBinder(FunctorCallbackBinder&& other) = delete;
  FunctorCallbackBinder& operator=(FunctorCallbackBinder&& other) = delete;

  // Requested conversions are valid only if:
  // a) We are converting to a known Callback type.
  // b) The signature of that type is compatible with the functor we're binding.
  // c) *this is a temporary (e.g. prevents auto f = ..., then converting f)
  template <
      typename CallbackType,
      typename = typename std::enable_if<IsCallback<CallbackType>()>::type,
      typename = typename std::enable_if<
          IsCallable<FunctorRef<Permanent, Functor>,
                     typename CallbackToSig<CallbackType>::type>()>::type>
  operator CallbackType*() && {
    CHECK(!bound_) << "Returned ToCallback object has already been converted";
    bound_ = true;
    if (IsEmpty<Functor>()) {
      return nullptr;
    } else {
      return new FunctorCallback<CallbackType, Permanent, Functor>(
          std::move(functor_), std::move(label_));
    }
  }

  // Unwrap the functor, returning ownership back.
  //
  // For use in ResultCallbackFunctorImpl.
  explicit operator Functor&&() && { return std::move(functor_); }

 private:
  template <typename F>
  typename std::enable_if<std::is_constructible<bool, F>::value, bool>::type
  IsEmpty() const {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpointer-bool-conversion"
#endif
    // functor_ may be a lambda, which is convertible to bool, leading to a
    // -Wpointer-bool-conversion warning because the value is always true.
    return functor_ == nullptr;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  }

  template <typename F>
  typename std::enable_if<!std::is_constructible<bool, F>::value,
                          bool>::type constexpr IsEmpty() const {
    return false;
  }

  Functor functor_;
  perftools::tracing::StringLabel label_;
  bool bound_ = false;
};

// Template aliases to hide the details of the computed return type of
// ToCallback and ToPermanentCallback. These keep the declarations
// readable enough to be in the public to_callback.h header.
template <typename Functor>
using ToCallbackResult =
    FunctorCallbackBinder<typename std::decay<Functor>::type, false>;
template <typename Functor>
using ToPermanentCallbackResult =
    FunctorCallbackBinder<typename std::decay<Functor>::type, true>;

}  // namespace internal
}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_INTERNAL_H_
