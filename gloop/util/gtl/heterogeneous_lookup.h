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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_HETEROGENEOUS_LOOKUP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_HETEROGENEOUS_LOOKUP_H_

#include "absl/container/internal/common.h"

namespace gtl {

// Utilities to implement heterogeneous lookup (https://abseil.io/tips/144).

// `gtl::IsTransparent<Functor>::value` is true if `Functor` is transparent.
using absl::container_internal::IsTransparent;

// Alias used for heterogeneous lookup functions.
//
// Usage:
//
//   template <typename Key, typename KeyFunctor>
//   class MyContainer {
//      template <class K>
//      using key_arg = HeterogeneousLookupKeyArg<K, Key, KeyFunctor>;
//
//     public:
//      template <typename K = Key>
//      auto Find(const key_arg<K>& arg);
//   };
//
// `key_arg<K>` evaluates to `K` when `KeyFunctor` is transparent, and to
// `Key` otherwise. It permits template argument deduction on `K` for the
// transparent case.
//
// `MyContainer<Key, Fn>::Find<K>()` can be used for any type supported by `Fn`.
template <typename K, typename KeyType, typename... Functors>
using HeterogeneousLookupKeyArg = typename absl::container_internal::KeyArg<(
    ... && gtl::IsTransparent<Functors>::value)>::template type<K, KeyType>;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_HETEROGENEOUS_LOOKUP_H_
