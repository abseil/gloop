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

#include "gloop/util/coding/two-values-varint.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/numeric/int128.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/coding/varint.h"
#include "gloop/util/gtl/stl_util.h"

namespace util {
namespace coding {

namespace {

// Maximum lengths of varint encoding of absl::uint128.
constexpr int kMax128 = 19;

// Interleaves the two uint32 nibble (4 bits) by nibble and generates an uint64.
inline uint64_t InterleaveNibbles(uint32_t a, uint32_t b) {
  uint64_t v = 0;
  int shift = 0;
  while ((a > 0) || (b > 0)) {
    uint8_t one_byte = (a & 0xf) | ((b & 0xf) << 4);
    v |= ((static_cast<uint64_t>(one_byte)) << shift);
    shift += 8;
    a >>= 4;
    b >>= 4;
  }
  return v;
}

inline int Length128(const uint64_t hi, const uint64_t lo) {
  if (hi == 0) {
    return Varint::Length64(lo);
  }
  // This computes floor(log2(v)) / 7 + 1, where v >= 2^64.
  return (73 * Bits::Log2FloorNonZero128(absl::MakeUint128(hi, lo)) + 530) /
         512;
}

// This function assumes hi != 0 for the input.
// TODO: similar optimizations in Encode32/64 can be applied here.
inline void Encode128(std::string* s, uint64_t hi, uint64_t lo) {
  DCHECK_GT(hi, 0UL);
  const size_t start = s->size();
  gtl::STLStringResizeUninitialized(s, start + Length128(hi, lo));
  unsigned char* ptr = reinterpret_cast<unsigned char*>(&((*s)[start]));
  static const uint64_t B = 128;
  *(ptr++) = static_cast<uint8_t>(lo | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 7) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 14) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 21) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 28) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 35) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 42) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 49) | B);
  *(ptr++) = static_cast<uint8_t>((lo >> 56) | B);
  static const int hi_max = 1 << 6;
  if (hi < hi_max) {
    *(ptr++) = static_cast<uint8_t>((lo >> 63) | (hi << 1));
    return;
  }
  *(ptr++) = static_cast<uint8_t>(((lo >> 63) | (hi << 1)) | B);
  Varint::Encode64(reinterpret_cast<char*>(ptr), hi >> 6);
}
}  // namespace

void TwoValuesVarint::Encode32(std::string* s, uint32_t a, uint32_t b) {
  Varint::Append64(s, InterleaveNibbles(a, b));
}

void TwoValuesVarint::Encode64(std::string* s, uint64_t a, uint64_t b) {
  const uint64_t lo =
      InterleaveNibbles(static_cast<uint32_t>(a), static_cast<uint32_t>(b));
  // Fast path.
  if ((a | b) >> 32 == 0) {
    Varint::Append64(s, lo);
    return;
  }
  const uint64_t hi = InterleaveNibbles(static_cast<uint32_t>(a >> 32),
                                        static_cast<uint32_t>(b >> 32));
  Encode128(s, hi, lo);
}

namespace {
// Skip the least significant "offset" bits and extract the next "bits" bits.
// Caller should guarantee the combination of "offset" and "bits" leads to valid
// bit operations.
uint64_t GetBits(uint32_t byte, uint32_t offset, uint32_t bits) {
  DCHECK(offset + bits <= 32 && offset != 32 && bits != 32);
  return (byte >> offset) & ((1u << bits) - 1);
}

// Extract portions of bits for *a and *b from a byte that is formatted
// as follows (least significant bits first):
//  shift bits for b
//  4 bits for a
//  3-shift bits for b
//  varint termination bit
#define PARSE_BITS_DECODE_TWO(shift, a, a_shift, b, b_shift)     \
  if (RespectLimit && uptr >= ulimit) return nullptr;            \
  byte = *(uptr++);                                              \
  b |= GetBits(byte, 0, shift) << b_shift;                       \
  a |= GetBits(byte, shift, 4) << a_shift;                       \
  b |= GetBits(byte, shift + 4, 3 - shift) << (b_shift + shift); \
  if (byte < 128) goto done;                                     \
  a_shift += 4;                                                  \
  b_shift += 3;

template <bool RespectLimit>
const char* Decode32Internal(const char* ptr, const char* limit, uint32_t* a,
                             uint32_t* b) {
  const uint8_t* uptr = reinterpret_cast<const uint8_t*>(ptr);
  const uint8_t* ulimit = reinterpret_cast<const uint8_t*>(limit);

  // Assert that an optimized path is used in DecodeTwo32Values before
  // DecodeTwo32ValuesSlow is invoked. This optimization does not exist for
  // DecodeTwo32ValuesWithLimit (the same way Parse64WithLimit does not have
  // this optimization).
  if (!RespectLimit) {
    DCHECK_GE(*uptr, 128);
  }

  uint32_t res1 = 0, res2 = 0, shift1 = 0, shift2 = 0, byte;

  // First four bytes contain four consecutive bits of res1 each.
  PARSE_BITS_DECODE_TWO(0, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(1, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(2, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(3, res1, shift1, res2, shift2);

  // Next four bytes contain four consecutive bits of res2 each.
  // Note the switched order of variables.
  PARSE_BITS_DECODE_TWO(0, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(1, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(2, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(3, res2, shift2, res1, shift1);

  // Back to the original order.
  PARSE_BITS_DECODE_TWO(0, res1, shift1, res2, shift2);

  if (RespectLimit && uptr >= ulimit) return nullptr;
  byte = *(uptr++);
  // 10th byte has at most one bit set.
  if (byte > 1) {
    return nullptr;  // Value is too long to be two encoded values.
  }
  res2 |= byte << 31;

done:
  *a = res1;
  *b = res2;
  return reinterpret_cast<const char*>(uptr);
}

template <bool RespectLimit>
const char* Decode64Internal(const char* ptr, const char* limit, uint64_t* a,
                             uint64_t* b) {
  const uint8_t* uptr = reinterpret_cast<const uint8_t*>(ptr);
  const uint8_t* ulimit = reinterpret_cast<const uint8_t*>(limit);

  // Assert that an optimized path is used in DecodeTwo64Values before
  // DecodeTwo64ValuesSlow is invoked.
  if (!RespectLimit) {
    DCHECK_GE(*uptr, 128);
  }

  uint64_t res1 = 0, res2 = 0, shift1 = 0, shift2 = 0, byte;

  // First four bytes contain four consecutive bits of res1 each.
  PARSE_BITS_DECODE_TWO(0, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(1, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(2, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(3, res1, shift1, res2, shift2);

  // Note the switched order of variables.
  PARSE_BITS_DECODE_TWO(0, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(1, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(2, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(3, res2, shift2, res1, shift1);

  // Note the switched order of variables.
  PARSE_BITS_DECODE_TWO(0, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(1, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(2, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(3, res1, shift1, res2, shift2);

  // Note the switched order of variables.
  PARSE_BITS_DECODE_TWO(0, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(1, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(2, res2, shift2, res1, shift1);
  PARSE_BITS_DECODE_TWO(3, res2, shift2, res1, shift1);

  // Note the switched order of variables.
  PARSE_BITS_DECODE_TWO(0, res1, shift1, res2, shift2);
  PARSE_BITS_DECODE_TWO(1, res1, shift1, res2, shift2);

  if (RespectLimit && uptr >= ulimit) return nullptr;
  byte = *(uptr++);
  // 19th byte has at most two bits set.
  if (byte > 3) {
    return nullptr;  // Value is too long to be two encoded values.
  }
  res2 |= static_cast<uint64_t>(byte) << 62;

done:
  *a = res1;
  *b = res2;
  return reinterpret_cast<const char*>(uptr);
}

#undef PARSE_BITS_DECODE_TWO
}  // namespace

const char* TwoValuesVarint::Decode32Slow(const char* p, uint32_t* a,
                                          uint32_t* b) {
  return Decode32Internal<false>(p, nullptr, a, b);
}

const char* TwoValuesVarint::Decode32WithLimit(const char* ptr,
                                               const char* limit, uint32_t* a,
                                               uint32_t* b) {
  if (ptr + Varint::kMax64 <= limit) {
    return Decode32(ptr, a, b);
  }
  return Decode32Internal<true>(ptr, limit, a, b);
}

const char* TwoValuesVarint::Decode64Slow(const char* p, uint64_t* a,
                                          uint64_t* b) {
  return Decode64Internal<false>(p, nullptr, a, b);
}

const char* TwoValuesVarint::Decode64WithLimit(const char* ptr,
                                               const char* limit, uint64_t* a,
                                               uint64_t* b) {
  if (ptr + kMax128 <= limit) {
    return Decode64(ptr, a, b);
  }
  return Decode64Internal<true>(ptr, limit, a, b);
}

}  // namespace coding
}  // namespace util
