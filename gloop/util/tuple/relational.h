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

// Copyright 2013 Google Inc. All Rights Reserved.

//
// Free functions for comparing tuples (less(), equal()) and polymorphic
// functors that can be conveniently used with STL containers (less_t, equal_t,
// etc.).
//
//   struct Person {
//     string first_name;
//     string second_name;
//     int age;
//   };
//
//   // Allow tuple-like access to the fields of Person.
//   TUPLE_ADAPT_STRUCT(Person, first_name, second_name, age);
//
//   // Person can be used as a key in map with less_t as comparator.
//   map<Person, string, less_t> person_address;

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_RELATIONAL_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_RELATIONAL_H_

#include <stddef.h>

#include "gloop/util/tuple/accumulate.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/ref.h"
#include "gloop/util/tuple/std_tuple.h"
#include "gloop/util/tuple/zip.h"

namespace util {
namespace tuple {

namespace internal_relational {

// All symbols defined within namespace internal_relational are internal
// to relational.h. Do not reference them from outside or your code can break
// without notice.

struct elem_compare {
  template <class Pair>
  constexpr int operator()(int res, const Pair& p) const {
    // If any previous elements aren't equal, we already know the result.
    if (res) return res;
    // Otherwise compare the current pair of elements.
    const auto& lhs = get<0>(p);
    const auto& rhs = get<1>(p);
    if (lhs < rhs) return -1;
    if (rhs < lhs) return 1;
    return 0;
  }
};

struct elem_equal {
  template <class Pair>
  bool operator()(bool res, const Pair& p) const {
    return res && get<0>(p) == get<1>(p);
  }
};

}  // namespace internal_relational

// This namespace is used to disable argument dependent lookup for functions
// defined in it. Later all symbols from it are brough to namespace util::tuple
// with a using directive.
namespace adl_barrier_relational {

// Lexicographically compares two tuples. Return 0 if the tuples are equal, -1
// if the first is smaller and 1 if the second is smaller.
//
// Requires:
//   1. Both tuples are of the same size.
//   2. get<I>(lhs) < get<I>(rhs) and get<I>(rhs) < get<I>(lhs) are valid
//      expressions for every I with the result explicitly convertible to bool.
template <class T, class U>
constexpr int compare(const T& lhs, const U& rhs) {
  static_assert(size<T>::value == size<U>::value,
                "Can't compare tuples of different size");
  return accumulate(
      internal_relational::elem_compare(),
      zip(tuple::ref<std_tuple_tag>(lhs), tuple::ref<std_tuple_tag>(rhs)), 0);
}

// Equivalent to compare(lhs, rhs) < 0.
template <class T, class U>
constexpr bool less(const T& lhs, const U& rhs) {
  return compare(lhs, rhs) < 0;
}

// Equivalent to compare(lhs, rhs) > 0.
template <class T, class U>
constexpr bool greater(const T& lhs, const U& rhs) {
  return compare(lhs, rhs) > 0;
}

// Equivalent to compare(lhs, rhs) == 0.
// Note that this function uses operator< for comparing the elements, not
// operator==. For the latter see equal() below.
template <class T, class U>
constexpr bool equivalent(const T& lhs, const U& rhs) {
  return compare(lhs, rhs) == 0;
}

// Equivalent to compare(lhs, rhs) != 0.
// Note that this function uses operator< for comparing the elements, not
// operator==. For the latter see not_equal() below.
template <class T, class U>
bool not_equivalent(const T& lhs, const U& rhs) {
  return compare(lhs, rhs) != 0;
}

// Equivalent to compare(lhs, rhs) <= 0.
template <class T, class U>
constexpr bool less_equal(const T& lhs, const U& rhs) {
  return compare(lhs, rhs) <= 0;
}

// Equivalent to compare(lhs, rhs) >= 0.
template <class T, class U>
constexpr bool greater_equal(const T& lhs, const U& rhs) {
  return compare(lhs, rhs) >= 0;
}

// Compares two tuples lexicographically for equality.
//
// Requires:
//   1. Both tuples are of the same size.
//   2. get<I>(lhs) == get<I>(rhs) is a valid expressions for every I with the
//      result explicitly convertible to bool.
template <class T, class U>
constexpr bool equal(const T& lhs, const U& rhs) {
  static_assert(size<T>::value == size<U>::value,
                "Can't compare tuples of different size");
  return accumulate(
      internal_relational::elem_equal(),
      zip(tuple::ref<std_tuple_tag>(lhs), tuple::ref<std_tuple_tag>(rhs)),
      true);
}

template <class T, class U>
constexpr bool not_equal(const T& lhs, const U& rhs) {
  return !equal(lhs, rhs);
}

}  // namespace adl_barrier_relational

// Using directive is used to bring all symbols from namespace
// adl_barrier_relational into util::tuple. adl_barrier_relational is a closed
// namespace that isn't used in any other file. This using directive can't be
// replace with a bunch of using declarations because using declarations
// enable ADL.
using namespace adl_barrier_relational;

// Polymorphic binary functor returning less(lhs, rhs).
struct less_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return less(lhs, rhs);
  }
};

// Polymorphic binary functor returning greater(lhs, rhs).
struct greater_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return greater(lhs, rhs);
  }
};

// Polymorphic binary functor returning equivalent(lhs, rhs).
struct equivalent_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return equivalent(lhs, rhs);
  }
};

// Polymorphic binary functor returning not_equivalent(lhs, rhs).
struct not_equivalent_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return not_equivalent(lhs, rhs);
  }
};

// Polymorphic binary functor returning less_equal(lhs, rhs).
struct less_equal_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return less_equal(lhs, rhs);
  }
};

// Polymorphic binary functor returning greater_equal(lhs, rhs).
struct greater_equal_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return greater_equal(lhs, rhs);
  }
};

// Polymorphic binary functor returning equal(lhs, rhs).
struct equal_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return equal(lhs, rhs);
  }
};

// Polymorphic binary functor returning not_equal(lhs, rhs).
struct not_equal_t {
  template <class T, class U>
  bool operator()(const T& lhs, const U& rhs) const {
    return not_equal(lhs, rhs);
  }
};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_RELATIONAL_H_
