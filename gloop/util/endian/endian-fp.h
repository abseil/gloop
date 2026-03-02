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

// Utility class for converting host-byte-order float/double into
// wire-byte-order.
//
// The main templated class WireFormatConverter is specialized into versions
// for both little-endian and big-endian wire formats.
//

#ifndef THIRD_PARTY_GLOOP_UTIL_ENDIAN_ENDIAN_FP_H_
#define THIRD_PARTY_GLOOP_UTIL_ENDIAN_ENDIAN_FP_H_

#include <cstdint>

#include "absl/base/casts.h"
#include "absl/log/log.h"
#include "gloop/base/port.h"
#include "gloop/util/endian/endian.h"

// This wraps the idea of a (possibly) byte-converted floating point number.
// Simply byte-swapping doesn't make much sense since the result would not
// necessarily be a valid floating point number anymore, so this class
// will return a uint32/64 after conversion.
//
// WARNING: there's a big assumption that we are talking about IEEE-754
// floating point values here. For example long doubles don't have a single
// cross-platform format, so don't add a class for them.
template <typename FPType, typename IntType, IntType (*ToHost)(IntType),
          IntType (*FromHost)(IntType)>
class FPFormatConverter {
 public:
  static IntType FromHostFP(FPType v) {
    return FromHost(absl::bit_cast<IntType>(v));
  }

  static FPType ToHostFP(IntType v) {
    return absl::bit_cast<FPType>(ToHost(v));
  }

  // Loads a network-byte-order value from the supplied buffer.
  //
  // A little hacky but should optimize nicely.
  static FPType LoadFromBuf(const char* buf) {
    IntType value;
    if (sizeof(value) == 4) {
      value = UNALIGNED_LOAD32(buf);
    } else if (sizeof(value) == 8) {
      value = UNALIGNED_LOAD64(buf);
    } else {
      LOG(FATAL) << "Unknown floating point size";
    }
    return ToHostFP(value);
  }

  // Stores a network-byte-order value into the supplied buffer.
  //
  // A little hacky but should optimize nicely.
  static void StoreToBuf(char* buf, FPType v) {
    IntType value = FromHostFP(v);
    if (sizeof(value) == 4) {
      UNALIGNED_STORE32(buf, value);
    } else if (sizeof(value) == 8) {
      UNALIGNED_STORE64(buf, value);
    } else {
      LOG(FATAL) << "Unknown floating point size";
    }
  }

 private:
  static_assert(sizeof(FPType) == sizeof(IntType), "fp_int_sizes_differ");
};

typedef FPFormatConverter<float, uint32_t, LittleEndian::ToHost32,
                          LittleEndian::FromHost32>
    LittleEndianFloat;

typedef FPFormatConverter<double, uint64_t, LittleEndian::ToHost64,
                          LittleEndian::FromHost64>
    LittleEndianDouble;

typedef FPFormatConverter<float, uint32_t, BigEndian::ToHost32,
                          BigEndian::FromHost32>
    BigEndianFloat;

typedef FPFormatConverter<double, uint64_t, BigEndian::ToHost64,
                          BigEndian::FromHost64>
    BigEndianDouble;

#endif  // THIRD_PARTY_GLOOP_UTIL_ENDIAN_ENDIAN_FP_H_
