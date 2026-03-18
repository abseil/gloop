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

// This file provides the StrongFixedArray container that wraps around
// FixedArray.  The wrapper restrict indexing to a pre-specified type-safe
// integer type or IntType (see util/intops/strong_int.h).  It prevents
// accidental indexing by different "logical" integer-like types (e.g.  another
// IntType) or native integer types.  The wrapper is useful as C++ and the
// standard template library allows the user to mix "logical" integral indices
// that might have a different role.
//
// The container can only be indexed by an instance of a StrongInt class, which
// can be declared as:
//
//     DEFINE_STRONG_INT_TYPE(IntTypeName, IntTypeValueType);
//
// where IntTypeName is the desired name for the "logical" integer-like type
// and the ValueType is a supported native integer type such as int or
// uint64 (see util/intops/strong_int.h for details).
//
// The wrapper exposes all public methods of FixedArray and behaves mostly as
// pass-through.  The only method modified to ensure type-safety is the
// operator [].
//
// EXAMPLES --------------------------------------------------------------------
//
//    DEFINE_STRONG_INT_TYPE(PhysicalChildIndex, int32);
//    StrongFixedArray<PhysicalChildIndex, ChildStats*> array;
//
//    PhysicalChildIndex physical_index;
//    array[physical_index] = ...;      <-- index type match: compiles properly.
//
//    int32 physical_index;
//    array[physical_index] = ...;      <-- fails to compile.
//
//    DEFINE_STRONG_INT_TYPE(LogicalChildIndex, int32);
//    LogicalChildIndex logical_index;
//    array[logical_index] = ...;       <-- fails to compile.
//
// NB: Iterator arithmetic is not allowed as the iterators are not wrapped
// themselves.  Therefore, the following caveat is possible:
//    *(array.begin() + 0) = ...;

#ifndef THIRD_PARTY_GLOOP_UTIL_INTOPS_STRONG_FIXEDARRAY_H_
#define THIRD_PARTY_GLOOP_UTIL_INTOPS_STRONG_FIXEDARRAY_H_

#include <stddef.h>
#include <sys/types.h>

#include <cstddef>
#include <initializer_list>
#include <type_traits>

#include "absl/container/fixed_array.h"
#include "gloop/util/intops/strong_int.h"

namespace util_intops {

// FixedArray ------------------------------------------------------------------
template <typename IntType, typename T,
          std::size_t inline_elements = absl::kFixedArrayUseDefault>
class StrongFixedArray : protected absl::FixedArray<T, inline_elements> {
 public:
  typedef absl::FixedArray<T, inline_elements> ParentType;
  typedef typename ParentType::value_type value_type;
  typedef typename ParentType::iterator iterator;
  typedef typename ParentType::const_iterator const_iterator;
  typedef typename ParentType::reference reference;
  typedef typename ParentType::const_reference const_reference;
  typedef typename ParentType::pointer pointer;
  typedef typename ParentType::const_pointer const_pointer;
  typedef typename ParentType::difference_type difference_type;
  typedef typename ParentType::size_type size_type;

 public:
  explicit StrongFixedArray(size_t n)
      : absl::FixedArray<T, inline_elements>(n) {}
  explicit StrongFixedArray(IntType n)
      : StrongFixedArray(static_cast<size_t>(n.value())) {}
  // Implicit initializer_list construction temporarily deleted to flush out
  // possible conflicting usage.
  StrongFixedArray(std::initializer_list<value_type> l) = delete;
  ~StrongFixedArray() {}

  // -- Modified methods -------------------------------------------------------
  T& operator[](IntType i) {
    return ParentType::operator[](static_cast<size_t>(i.value()));
  }
  const T& operator[](IntType i) const {
    return ParentType::operator[](static_cast<size_t>(i.value()));
  }

  // -- Pass-through methods to FixedArray -------------------------------------
  size_t size() const { return ParentType::size(); }
  size_t memsize() const { return ParentType::memsize(); }
  const T* data() const { return ParentType::data(); }
  T* data() { return ParentType::data(); }

  iterator begin() { return ParentType::begin(); }
  iterator end() { return ParentType::end(); }
  const_iterator begin() const { return ParentType::begin(); }
  const_iterator end() const { return ParentType::end(); }

  // -- Iteration related methods ----------------------------------------------
  // Index into the start of the array.
  IntType start_index() const { return IntType(0); }

  // Index following the last valid index into the array.
  IntType end_index() const { return IntType(size()); }

  // Returns an iterator of valid indices into this StrongFixedArray. Goes from
  // start_index() to end_index(). This is useful for cases of
  // parallel iteration over several containers indexed by the same type, e.g.
  //   StrongFixedArray<MyInt, foo> a1;
  //   StrongFixedArray<MyInt, foo> a2;
  //   CHECK_EQ(a1.size(), a2.size());
  //   for (const auto i : a1.index_range()) {
  //     do_stuff(a1[i], a2[i]);
  //   }
  StrongIntRange<IntType> index_range() const {
    return StrongIntRange<IntType>(start_index(), end_index());
  }

  static_assert(std::is_integral<typename IntType::ValueType>::value,
                "int type indexed fixedarray must have integral index");
};

}  // namespace util_intops

#endif  // THIRD_PARTY_GLOOP_UTIL_INTOPS_STRONG_FIXEDARRAY_H_
