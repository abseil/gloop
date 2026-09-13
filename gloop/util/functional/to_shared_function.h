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

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_SHARED_FUNCTION_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_SHARED_FUNCTION_H_

// ToSharedFunction is a utility for converting non-mutable lambdas with move
// only objects bound to them to std::function compatible wrappers for use with
// apis that require std::function.
//
// Example usage:
//
// void SomeFunctionUser(std::function<int(std::string)>);
//
// std::unique_ptr<LoggerInterface> logger = ...;
// SomeFunctionUser(ToSharedFunction(std::move(logger)));

#include <memory>
#include <type_traits>

#include "absl/base/nullability.h"
#include "absl/memory/memory.h"

namespace util::functional {

template <class Functor>
auto ToSharedFunction(absl_nonnull std::shared_ptr<Functor> f) {
  return [f = std::move(f)](auto&&... args) -> decltype(auto) {
    return (*f)(std::forward<decltype(args)>(args)...);
  };
}

template <class Functor, class Deleter>
auto ToSharedFunction(absl_nonnull std::unique_ptr<Functor, Deleter> f) {
  return ToSharedFunction(absl::ShareUniquePtr(std::move(f)));
}

template <class Functor>
auto ToSharedFunction(Functor&& f) {
  return ToSharedFunction(
      std::make_shared<std::decay_t<Functor>>(std::forward<Functor>(f)));
}

}  // namespace util::functional

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_SHARED_FUNCTION_H_
