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
// This library provides utilities for manually ordering function overloads.
//
// rank<N> can be converted to rank<M> for any M >= N. The smaller the value of
// M, the better is the conversion in terms of overload resolution. This
// property is handy for resolving ambiguity among overloads.
//
// Example: A generic function for clearing objects.
//
// Let's see how we can implement function GenericClear(p) that works as
// follows:
//
//   1. If p->Clear() is a valid expression, GenericClear(p) calls that.
//   2. Else if p->clear() is a valid expression, calls that.
//   3. Else calls *p = T();
//
// This algorithm is a bit silly but it'll do as an example. To check whether
// p->Clear() is a valid expression we can add an extra template parameter with
// a special default value.
//
//   template <class T, class E = decltype(declval<T*>()->Clear())>
//   void GenericClear(T* p) {
//     p->Clear();
//   }
//
// We can do the same for lower case clear().
//
//   template <class T, class E = decltype(declval<T*>()->clear())>
//   void GenericClear(T* p) {
//     p->clear();
//   }
//
// These two overloads work fine as long as the template argument has exactly
// one of Clear() or clear(). We get a compile error if both methods are found,
// though. The solution is to employ rank<> to explicitly specify the ordering
// of these overloads.
//
// Here's the full implementation:
//
//   template <class T, class E = decltype(declval<T*>()->Clear())>
//   void GenericClear(T* p, rank<0> rank) {
//     p->Clear();
//   }
//
//   template <class T, class E = decltype(declval<T*>()->clear())>
//   void GenericClear(T* p, rank<1> rank) {
//     p->clear();
//   }
//
//   template <class T>
//   void GenericClear(T* p, rank<2> rank) {
//     *p = T();
//   }
//
// When calling GenericClear(), we need to pass the special rank_selector object
// as the second argument.
//
//   MyType obj;
//   GenericClear(&obj, rank_selector);
//
// Voilà! Totally useless but pretty cool generic function.
//
// When using rank<>, make sure you aren't inadvertently creating a conflicting
// ordering via arguments other than rank<>. For example, if one function
// overload accepts const T& while another accepts const vector<T>&, there is an
// implicit ordering between the two. It's best to stick to arguments that are
// literally the same in all overloads and rely on enablers to express type
// constraints. We did this in the example above: all overloads of
// GenericClear() work on T*.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_RANK_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_RANK_H_

#include <stddef.h>

namespace util {
namespace tuple {

inline constexpr size_t kMaxRank = 40;

namespace internal_rank {

// rank<N> derives from rank<N + 1>, so rank<N> converts to rank<M> for any
// M >= N, and conversions to closer bases are better in overload resolution.
//
// The chain is flat (as opposed to nesting templates), so the mangled name of
// rank<N> has a small constant size regardless of N. This keeps the symbol
// names of functions taking rank<N> short.
//
// This lives in internal_rank so that rank<N> arguments don't bring
// util::tuple into argument-dependent lookup.
template <size_t N>
struct rank : rank<(N < kMaxRank ? N + 1 : kMaxRank)> {
  // If this assertion triggers because you legitimately need to instantiate
  // rank<N> with N > kMaxRank, talk to the owners of this library or ask them
  // to review a change that increases kMaxRank.
  static_assert(N <= kMaxRank, "N in rank<N> must not exceed kMaxRank");
};

template <>
struct rank<kMaxRank> {};

}  // namespace internal_rank

// N must be <= kMaxRank.
template <size_t N>
using rank = internal_rank::rank<N>;

using rank_selector_t = rank<0>;

extern const rank_selector_t rank_selector;

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_RANK_H_
