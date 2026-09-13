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

// Implementation details for functor to callback conversion.  Do not include
// this header file directly.  Instead, use "to_callback.h"

// IWYU pragma: private, include "util/functional/to_callback.h"
#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_INTERNAL_H_

#include <type_traits>
#include <utility>

#include "absl/cleanup/cleanup.h"
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

template <bool Permanent, typename Functor>
class FunctorClosure final : public Closure {
 public:
  // Always move-constructed.
  explicit FunctorClosure(
      Functor functor, perftools::tracing::StringLabel label =
                           perftools::tracing::TraceSourceLocation::current())
      : Closure(std::conditional_t<Permanent, ::base::Context::DefaultInitType,
                                   ::base::Context::ThreadInitType>(),
                label),
        functor_(std::move(functor)),
        label_(std::move(label)) {}

  // This type is neither copyable nor movable.
  FunctorClosure(const FunctorClosure&) = delete;
  FunctorClosure& operator=(const FunctorClosure&) = delete;

  void Run() override {
    // Some notes on the invocation below:
    // - For non-void types, the conversion above has already been validated by
    //   IsCallable.
    // - std::forward allows the use of move-only arguments.
    if constexpr (Permanent) {
      functor()();
    } else {
      base::WithContext with(std::move(this->context_), label_);
      auto delete_self = absl::Cleanup([this] { delete this; });
      functor()();
    }
  }

  bool IsRepeatable() const override { return Permanent; }

 private:
  // Returns `functor_` cast to the proper lvalue or rvalue function reference.
  FunctorRef<Permanent, Functor> functor() {
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

template <typename F>
constexpr bool IsEmpty(const F& f) {
  if constexpr (std::is_constructible_v<bool, F>) {
    return !static_cast<bool>(f);
  } else {
    return false;
  }
}

}  // namespace internal
}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_INTERNAL_H_
