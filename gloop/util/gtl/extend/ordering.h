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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_ORDERING_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_ORDERING_H_

#include "gloop/util/gtl/extend/equality.h"
#include "gloop/util/gtl/extend/extend.h"

namespace gtl {

// OrderingExtension
//
// A gtl::Extend extension that enables all six comparison operators (<, <=, ==,
// !=, >, >=) for the struct by comparing them lexicographically (in the order
// that they appear in the struct). Each field must itself be comparable with
// operator< and operator==.
//
// Example:
//
// struct Thing
//     : gtl::Extend<Thing>::With<gtl::OrderingExtension> {
//   std::string name;
//   std::vector<int> values;
// };
//
// Thing t1{.name = "abc", .values = {1, 2, 3}}
// Thing t2{.name = "def", .values = {0, 2, 4}}
// Thing t3{.name = "def", .values = {1, 2, 4}}
// assert(t1 == t1);
// assert(t1 < t2);
// assert(t2 < t3);
// assert(t3 > t1);
//
// Note that objects do not need to be totally ordered for this extension to
// behave properly. The objects must be partially ordered. The lexicographic
// extension will extend this partial ordering to a partial ordering on the
// Tuple.
//
// Example:
//
// struct Point {
//   int x;
//   int y;
// };
// constexpr bool operator==(Point lhs, Point rhs) {
//   return lhs.x == rhs.x && lhs.y == rhs.y;
// }
// constexpr bool operator<(Point lhs, Point rhs) {
//   return !(lhs == rhs) && lhs.x <= rhs.x && lhs.y <= rhs.y;
// }
//
// struct WeightedPoint : gtl::Extend<WeightedPoint>::With<
//                            gtl::OrderingExtension> {
//   Point p;
//   double weight;
// };
//
// WeightedPoint w1{.p = {.x = 0, .y = 1}, .weight = 1.0};
// WeightedPoint w2{.p = {.x = 1, .y = 0}, .weight = 1.0};
// WeightedPoint w3{.p = {.x = 1, .y = 1}, .weight = 1.0};
//
// assert(!(w1 < w2))
// assert(!(w2 < w1))
// assert(w1 < w3)
// assert(w2 < w3)
//
template <typename T>
struct OrderingExtension : Extension<OrderingExtension, T> {
  using deps = void(EqualityExtension<T>);

  friend constexpr bool operator<(const T& lhs, const T& rhs) {
    return OrderingExtension::Unpack(lhs) < OrderingExtension::Unpack(rhs);
  }

  friend constexpr bool operator>(const T& lhs, const T& rhs) {
    return rhs < lhs;
  }
  friend constexpr bool operator<=(const T& lhs, const T& rhs) {
    return !(rhs < lhs);
  }
  friend constexpr bool operator>=(const T& lhs, const T& rhs) {
    return !(lhs < rhs);
  }

 private:
  using Extension<OrderingExtension, T>::Unpack;
};
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_ORDERING_H_
