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

// Implementation details for functor context wrapping.  Do not include
// this header file directly.  Instead, use "with_context.h"

// IWYU pragma: private, include "util/functional/with_context.h"
#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_WITH_CONTEXT_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_WITH_CONTEXT_INTERNAL_H_

#include <type_traits>
#include <utility>

#include "gloop/base/context.h"

namespace util {
namespace functional {
namespace internal {

template <typename Functor>
class WithContextFunctorImpl {
 public:
  template <typename Context>
  explicit WithContextFunctorImpl(const Functor& functor, Context&& context)
      : functor_(functor), context_(std::forward<Context>(context)) {}

  template <typename Context>
  explicit WithContextFunctorImpl(Functor&& functor, Context&& context)
      : functor_(std::forward<Functor>(functor)),
        context_(std::forward<Context>(context)) {}

  template <typename... Args>
  std::invoke_result_t<Functor&, Args&&...> operator()(Args&&... args) & {
    ::base::WithContext wc(this->context_);
    return std::invoke(functor_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  std::invoke_result_t<Functor&&, Args&&...> operator()(Args&&... args) && {
    ::base::WithContext wc(std::move(this->context_));
    return std::invoke(std::move(functor_), std::forward<Args>(args)...);
  }

  template <typename... Args>
  std::invoke_result_t<const Functor&, Args&&...> operator()(
      Args&&... args) const& {
    ::base::WithContext wc(this->context_);
    return std::invoke(functor_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  std::invoke_result_t<const Functor&&, Args&&...> operator()(
      Args&&... args) const&& {
    ::base::WithContext wc(std::move(this->context_));
    return std::invoke(std::move(functor_), std::forward<Args>(args)...);
  }

  explicit operator bool() const {
    if constexpr (std::is_constructible_v<bool, Functor>) {
      return static_cast<bool>(functor_);
    } else {
      return true;
    }
  }

 private:
  Functor functor_;
  base::Context context_;
};

// Template alias to hide the details of the computed return type of
// WithContext. These keep the declarations readable enough to be in the public
// with_context.h header.
template <typename Functor>
using WithContextFunctor = WithContextFunctorImpl<std::decay_t<Functor>>;

}  // namespace internal
}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_WITH_CONTEXT_INTERNAL_H_
