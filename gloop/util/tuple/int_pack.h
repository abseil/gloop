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

//
// Low-level facility for expanding tuples.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_INT_PACK_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_INT_PACK_H_

#include <stddef.h>

namespace util {
namespace tuple {

// Denotes a sequence of integers.
template <::size_t... Is>
struct int_pack {
  using type = int_pack;
};

namespace internal_int_pack {

// All symbols defined within namespace internal_int_pack are internal
// to int_pack.h. Do not reference them from outside or your code can break
// without notice.

template <class Pack, ::size_t Offset>
struct offset;

template <::size_t... Is, ::size_t Offset>
struct offset<int_pack<Is...>, Offset> : int_pack<(Is + Offset)...> {};

template <class A, ::size_t N, ::size_t M>
struct extend;

// Note that N == sizeof...(As). It's passed explicitly for efficiency.
template <::size_t... As, ::size_t N>
struct extend<int_pack<As...>, N, 0> : int_pack<As..., (As + N)...> {};

template <::size_t... As, ::size_t N>
struct extend<int_pack<As...>, N, 1> : int_pack<As..., (As + N)..., N + N> {};

template <::size_t N>
struct iota;

template <::size_t N>
using iota_t = typename iota<N>::type;

template <::size_t N>
struct iota : extend<iota_t<N / 2>, N / 2, N % 2> {};

template <>
struct iota<0> : int_pack<> {};

}  // namespace internal_int_pack

// Metafunction that given an two integers M and N produces
// int_pack<M, M+1, ..., N-1>.
//
// Requires: N >= M.
//
//   make_int_pack<1, 4>::type is int_pack<1, 2, 3>.
//   make_int_pack<5, 5> is int_pack<>.
//
// For convenience, make_int_pack<M, N> inherits from make_int_pack<M, N>::type.
//
//   template <size_t... Is>
//   void DoSomething(int_pack<Is...>) {
//     // Use "Is...".
//   }
//
//   DoSomething(make_int_pack<0, 5>());
template <::size_t M, ::size_t N>
struct make_int_pack
    : internal_int_pack::offset<internal_int_pack::iota_t<(N >= M ? N - M : 0)>,
                                M> {
  static_assert(N >= M, "Invalid range");
};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_INT_PACK_H_
