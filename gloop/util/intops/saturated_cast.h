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

// Provides a saturated_cast function template to facilitate conversion between
// numeric types, with clipping at range boundaries in case the conversion would
// otherwise result in overflow or underflow.
//
// When possible, it's preferable to avoiding narrowing in the first place, or
// at least not do it silently. Consider the following alternatives:
// - util_intops::LosslessConvert, which ensures narrowing does not occur.
// - util_intops::Saturated, which indicates when narrowing has occurred.
//
// Types supported by saturated_cast include:
// - int8/uint8
// - int16/uint16
// - int32/uint32
// - int64/uint64
// - uint128
//
// Note the support for conversions between types and for the uint128 type
// distinguishes saturated_cast from related functionality provided by utilities
// like the util_intops::Saturated class.
//
// Note the implementation currently supports only integral types; calling with
// a floating-point type will fail to compile. Support for floating-point could
// conceivably be added if that were considered useful.
#ifndef THIRD_PARTY_GLOOP_UTIL_INTOPS_SATURATED_CAST_H_
#define THIRD_PARTY_GLOOP_UTIL_INTOPS_SATURATED_CAST_H_

#include <cstdint>
#include <limits>
#include <type_traits>

#include "absl/numeric/int128.h"

namespace util_intops {

// Returns 'from' if it can be represented without loss as a value of type To,
// or else saturates:
// - If 'from' < the minimum value representable as To, returns the minimum.
// - If 'from' > the maximum value representable as To, returns the maximum.
//
// Some toy examples:
//   saturated_cast<int32>(kuint16max) returns kuint16max (i.e., unchanged)
//   saturated_cast<int64>(kuint128max) returns kint64max (i.e., saturated max)
//   saturated_cast<int16>(kint64min) returns kint16min (i.e., saturated min)
template <typename To, typename From>
To saturated_cast(From from);

////////////////////////////////////////////////////////////////////////////////

// Implementation details of saturated_cast follow below. These must be in the
// header since everything is templated.

// Specializations of this class must provide a method like the following:
//   static To cast(From from);
// Note the 'Enabled' type parameter is to support use of std::enable_if.
template <typename To, typename From, typename Enabled = void>
struct SaturatedCastImpl;

// Implementation of saturated_cast; see comments on declaration above.
template <typename To, typename From>
To saturated_cast(From from) {
  return SaturatedCastImpl<To, From>::cast(from);
}

// Implementation of saturated_cast when From and To are both integral types.
template <typename To, typename From>
struct SaturatedCastImpl<
    To, From,
    typename std::enable_if<std::is_integral<From>::value &&
                            std::is_integral<To>::value>::type> {
  static To cast(From from) {
    if (std::is_signed<From>::value &&
        // We are careful to not compare between signed and unsigned if 'from'
        // is negative, to avoid confusion around type promotion conversions.
        ((from < 0 && std::is_unsigned<To>::value) ||
         (from < std::numeric_limits<To>::min()))) {
      return std::numeric_limits<To>::min();
    } else if (from > std::numeric_limits<To>::max()) {
      return std::numeric_limits<To>::max();
    } else {
      return static_cast<To>(from);
    }
  }
};

// Implementation of saturated_cast when From=uint128 and To is integral.
template <typename To>
struct SaturatedCastImpl<
    To, absl::uint128,
    typename std::enable_if<std::is_integral<To>::value &&
                            std::numeric_limits<To>::max() <=
                                std::numeric_limits<uint64_t>::max()>::type> {
  static To cast(absl::uint128 from) {
    if (absl::Uint128High64(from) != 0) {
      return std::numeric_limits<To>::max();
    } else {
      return saturated_cast<To>(absl::Uint128Low64(from));
    }
  }
};

// Implementation of saturated_cast when To=uint128 and From is integral.
template <typename From>
struct SaturatedCastImpl<
    absl::uint128, From,
    typename std::enable_if<std::is_integral<From>::value &&
                            std::numeric_limits<From>::max() <=
                                std::numeric_limits<uint64_t>::max()>::type> {
  static absl::uint128 cast(From from) {
    if (from < 0) {
      return absl::uint128(0);
    } else {
      return absl::uint128(static_cast<uint64_t>(from));
    }
  }
};

// Implementation of saturated_cast when From and To both are uint128. Unlikely
// to be called typically but might in generic contexts.
template <>
struct SaturatedCastImpl<absl::uint128, absl::uint128> {
  static absl::uint128 cast(absl::uint128 from) { return from; }
};

}  // namespace util_intops

#endif  // THIRD_PARTY_GLOOP_UTIL_INTOPS_SATURATED_CAST_H_
