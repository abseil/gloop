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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_COMPRESSED_TUPLE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_COMPRESSED_TUPLE_H_

#include "absl/container/internal/compressed_tuple.h"

namespace gtl {

// Helper class to perform the Empty Base Class Optimization.
// Ts can contain classes and non-classes, empty or not. For the ones that
// are empty classes, we perform the EBCO. If all types in Ts are empty classes,
// then CompressedTuple<Ts...> is itself an empty class.
//
// To access the members, use member .get<N>() function.
//
// Class synopsis:
//
// template <typename... Ts>
// class CompressedTuple {
//  public:
//   CompressedTuple() = default;
//   CompressedTuple(Ts...);
//
//   CompressedTuple(const CompressedTuple&) = default;
//   CompressedTuple(CompressedTuple&&) = default;
//   CompressedTuple& operator=(const CompressedTuple&) = default;
//   CompressedTuple& operator=(CompressedTuple&&) = default;
//   ~CompressedTuple() = default;
//
//   // Where Tn is the n-th element in Ts...
//   template <size_t n>
//   Tn& get();
//   template <size_t n>
//   const Tn& get() const;
// };
//
// Example usage:
//   gtl::CompressedTuple<int, T1, T2, T3> value(7, t1, t2, t3);
//   assert(value.get<0>() == 7);
//   T1& t1 = value.get<1>();
//   const T2& t2 = value.get<2>();
//   ...
using absl::container_internal::CompressedTuple;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_COMPRESSED_TUPLE_H_
