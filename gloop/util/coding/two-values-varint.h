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

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_TWO_VALUES_VARINT_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_TWO_VALUES_VARINT_H_

#include <cstdint>
#include <string>

namespace util {
namespace coding {

class TwoValuesVarint {
 public:
  // EFFECTS    Encodes a pair of values to "*s". Interleaves nibbles (4 bits)
  //            from a and b and varint encodes the result. This is useful when
  //            a and b are small and can both be encoded in a single byte.
  static void Encode32(std::string* s, uint32_t a, uint32_t b);
  static const char* Decode32(const char* ptr, uint32_t* a, uint32_t* b);
  static const char* Decode32WithLimit(const char* ptr, const char* limit,
                                       uint32_t* a, uint32_t* b);

  // Compatibility notes: the encoder will generate the same output as
  // Encode32 if both the input values can be represented by uint32.
  // The decoder is able to decode any output produced by Encode32.
  static void Encode64(std::string* s, uint64_t a, uint64_t b);
  static const char* Decode64(const char* ptr, uint64_t* a, uint64_t* b);
  static const char* Decode64WithLimit(const char* ptr, const char* limit,
                                       uint64_t* a, uint64_t* b);

 private:
  static const char* Decode32Slow(const char* p, uint32_t* a, uint32_t* b);
  static const char* Decode64Slow(const char* p, uint64_t* a, uint64_t* b);
};

inline const char* TwoValuesVarint::Decode32(const char* p, uint32_t* a,
                                             uint32_t* b) {
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(p);
  if (*ptr < 128) {
    // Special case for small values
    *a = (*ptr & 0xf);
    *b = *ptr >> 4;
    return reinterpret_cast<const char*>(ptr) + 1;
  } else {
    return Decode32Slow(p, a, b);
  }
}

inline const char* TwoValuesVarint::Decode64(const char* p, uint64_t* a,
                                             uint64_t* b) {
  auto* ptr = reinterpret_cast<const unsigned char*>(p);
  if (*ptr < 128) {
    // Special case for small values
    *a = (*ptr & 0xf);
    *b = *ptr >> 4;
    return reinterpret_cast<const char*>(ptr) + 1;
  } else {
    return Decode64Slow(p, a, b);
  }
}

}  // namespace coding
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_TWO_VALUES_VARINT_H_
