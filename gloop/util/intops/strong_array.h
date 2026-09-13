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

// This file provides the StrongArray, which wraps a C native array of
// values, indexed by an StrongInt (see util/intops/string_int.h), as
// well as the ConstStrongArray for wrapping const arrays. Both prevents
// accidental indexing by different "logical" integer-like types (e.g.
// another StrongInt) or native integer types. The wrappers are useful
// because C++ allows the user to mix "logical" integral indices that
// might have different roles.
//
// The container can only be indexed by an instance of a StrongInt
// class, which can be declared as:
//
//     DEFINE_STRONG_INT_TYPE(IntTypeName, IntTypeValueType);
//
// where IntTypeName is the desired name for the "logical" integer-like
// type and the ValueType is a supported native integer type such as
// int or uint64 (see util/intops/strong_int.h for details). No bounds
// checking is done.
//
// The StrongArray doesn't own the underlying array that it is given.
// It is intended for scenarios in which the array is already owned by
// something else (such as being part of a byte stream).  If you want
// owned arrays, see StrongFixedArray.
//
// Objects of this type have value semantics. They can be freely
// assigned and copy-constructed. The object produced by the default
// constructor contains a NULL pointer.
//
// One example use case is when you have serialized data that is known
// to contain an array of values. One can reinterpret a pointer to that
// array as an StrongArray, to provide safer and more convenient access
// when using indices that are already IntTypes.
//
// EXAMPLES --------------------------------------------------------------------
//
//    DEFINE_STRONG_INT_TYPE(PhysicalChildIndex, int32);
//    ChildStats* data = reinterpret_cast<ChildStats*>(ptr_to_block_of_mem);
//    StrongArray<PhysicalChildIndex, ChildStats*> array(data);
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
//    ----------------
//
//    const ChildStats* data = ...;
//    ConstStrongArray<PhysicalChildIndex, ChildStats*> array(data);
//

#ifndef THIRD_PARTY_GLOOP_UTIL_INTOPS_STRONG_ARRAY_H_
#define THIRD_PARTY_GLOOP_UTIL_INTOPS_STRONG_ARRAY_H_

#include <type_traits>

namespace util_intops {

template <typename IntType, typename ValueType>
class StrongArray {
 public:
  typedef StrongArray<IntType, ValueType> ThisType;
  typedef StrongArray<IntType, const ValueType> ConstThisType;
  typedef IntType index_type;
  typedef ValueType value_type;

 public:
  // Default constructor sets the wrapped pointer to NULL.
  StrongArray() : ptr_(nullptr) {}

  explicit StrongArray(ValueType* ptr) : ptr_(ptr) {}
  ~StrongArray() {}

  ValueType& operator[](IntType index) { return ptr_[index.value()]; }
  const ValueType& operator[](IntType index) const {
    return ptr_[index.value()];
  }

  ThisType operator+(IntType delta) { return ThisType(ptr_ + delta.value()); }

  ThisType operator-(IntType delta) { return ThisType(ptr_ - delta.value()); }
  const ThisType operator-(IntType delta) const {
    return ThisType(ptr_ - delta.value());
  }

  // TODO: += and -=?

  // This is for special-case access to the underlying array.  The following
  // is strongly discouraged:  my_strong_array.data()[non_inttype_idx]
  ValueType* data() { return ptr_; }
  const ValueType* data() const { return ptr_; }

 private:
  static_assert(std::is_integral<typename IntType::ValueType>::value,
                "int type indexed fixedarray must have integral index");

  ValueType* const ptr_;
  // Default copy and assign are ok.
};

}  // namespace util_intops

#endif  // THIRD_PARTY_GLOOP_UTIL_INTOPS_STRONG_ARRAY_H_
