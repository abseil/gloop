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

// Copyright 2005 Google Inc. All Rights Reserved.

//
// This file defines two ways to get a unique integer from any type.
//
// It's useful when you want to store several values indexed by type,
// but don't want to use static variables in those types. The Type
// does not need to be complete. WARNING: The values returned are only valid
// for a single run of a binary, and should never be saved or sent over the
// wire. For cross-process, cross-machine consistency, see a related mechanism
// implemented in //gloop/util/registration/typename.h.
//
// These IDs serve a similar purpose to the typeid() operator but does
// not require RTTI, can only operate over static types, and doesn't
// provide as much information. You may be tempted to explicitly
// specialize this class to provide other per-type data. Probably
// don't. It's probably cleaner to define another structure to hold
// your data. In any case, this code will need to be refactored
// before it's ready to be explicitly specialized.
//
// Call it as:
//
//   FastTypeId<Type>()  // If you don't need contiguous/small values, or
//   TypeIdInSet<SetKeyType>::get<Type>()  // if you do.
//
// The FastTypeId-variant is fully evaluated at compile/link-time, resulting in
// a pure constant in your final binary, while the TypeIdInSet-variant uses a
// singleton and counter to guarantee small/contiguous values.
//
// Note that there is a third (deprecated) variant:
//   TypeId::get<Type>()
//
// This variant uses TypeIdInSet<void> internally, so gives you the drawback of
// using the TypeIdInSet-variant, while at the same time not giving you the
// guarantee of contiguous values, assuming another library in your binary might
// use that call as well.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_TYPEID_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_TYPEID_H_

#include <atomic>
#include <cstddef>

namespace gtl {

// FastTypeId<Type>() evaluates at compile/link-time to a unique integer for the
// passed in type. Their values are neither contiguous nor small, making them
// unfit for using as an index into a vector, but a good match for keys into
// maps or straight up comparisons.
// Note that on 64-bit (unix) systems size_t is 64-bit while int is 32-bit and
// the compiler will happily and quietly assign such a 64-bit value to a
// 32-bit integer. While a client should never do that it SHOULD still be safe,
// assuming the BSS segment doesn't span more than 4GiB.
template <typename Type>
inline size_t FastTypeId() {
  static_assert(sizeof(char*) <= sizeof(size_t),
                "ptr size too large for size_t");

  // This static variable isn't actually used, only its address, so there are
  // no concurrency issues.
  static char dummy_var;
  return reinterpret_cast<size_t>(&dummy_var);
}

// Use this as TypeIdInSet<some_type_specific_to_your_use>, that way you are
// guaranteed to get contiguous IDs starting at 0 unique to your particular
// use-case, as would be appropriate to use for indexes into a vector.
// 'some_type_specific_to_your_use' could (e.g.) be the class that contains
// that particular vector.
template <typename IdSet>
class TypeIdInSet {
 public:
  template <typename Type>
  static size_t get() {
    static const size_t id = value_.fetch_add(1, std::memory_order_relaxed);
    return id;
  }

  // Returns the number of IDs returned by get() so far. This is appropriate
  // pre-sizing a vector, but should be avoided for fixed sized data structures
  // since the number of IDs can change over time.
  static size_t num_ids() { return value_.load(std::memory_order_relaxed); }

 private:
  static std::atomic<size_t> value_;
};

template <typename IdSet>
std::atomic<size_t> TypeIdInSet<IdSet>::value_;

// Deprecated, see comment at top. Either use FastTypeId or TypeIdInSet.
typedef TypeIdInSet<void> TypeId;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_TYPEID_H_
