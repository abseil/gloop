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

//
// Legacy implementation of the core Jenkins lookup2 algorithm. This is used in
// many older hash functions which we are unable to remove or change due to the
// values being recorded. New code should not use any of these routines and
// should not include this header file. It pollutes the global namespace with
// the 'mix' function.
//
// This file contains the basic hash "mix" code which is widely referenced.
//
// This file also contains routines used to load an unaligned little-endian
// word from memory.  This relatively generic functionality probably
// shouldn't live in this file.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_JENKINS_LOOKUP2_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_JENKINS_LOOKUP2_H_

#include <cstdint>

#include "gloop/base/port.h"

// ----------------------------------------------------------------------
// mix()
//    The hash function I use is due to Bob Jenkins (see
//    http://burtleburtle.net/bob/hash/index.html).
//    Each mix takes 36 instructions, in 18 cycles if you're lucky.
//
//    On x86 architectures, this requires 45 instructions in 18~20 cycles.
// ----------------------------------------------------------------------

inline void mix_c(uint32_t& a, uint32_t& b,
                  uint32_t& c) {  // NOLINT 32bit version
  a -= b;
  a -= c;
  a ^= (c >> 13);
  b -= c;
  b -= a;
  b ^= (a << 8);
  c -= a;
  c -= b;
  c ^= (b >> 13);
  a -= b;
  a -= c;
  a ^= (c >> 12);
  b -= c;
  b -= a;
  b ^= (a << 16);
  c -= a;
  c -= b;
  c ^= (b >> 5);
  a -= b;
  a -= c;
  a ^= (c >> 3);
  b -= c;
  b -= a;
  b ^= (a << 10);
  c -= a;
  c -= b;
  c ^= (b >> 15);
}

inline void mix_c(uint64_t& a, uint64_t& b,
                  uint64_t& c) {  // NOLINT 64bit version
  a -= b;
  a -= c;
  a ^= (c >> 43);
  b -= c;
  b -= a;
  b ^= (a << 9);
  c -= a;
  c -= b;
  c ^= (b >> 8);
  a -= b;
  a -= c;
  a ^= (c >> 38);
  b -= c;
  b -= a;
  b ^= (a << 23);
  c -= a;
  c -= b;
  c ^= (b >> 5);
  a -= b;
  a -= c;
  a ^= (c >> 35);
  b -= c;
  b -= a;
  b ^= (a << 49);
  c -= a;
  c -= b;
  c ^= (b >> 11);
  a -= b;
  a -= c;
  a ^= (c >> 12);
  b -= c;
  b -= a;
  b ^= (a << 18);
  c -= a;
  c -= b;
  c ^= (b >> 22);
}

inline void mix(uint32_t& a, uint32_t& b,
                uint32_t& c) {  // NOLINT 32bit version
  return mix_c(a, b, c);
}

inline void mix(uint64_t& a, uint64_t& b,
                uint64_t& c) {  // NOLINT 64bit version
  return mix_c(a, b, c);
}

// Load an unaligned little endian word from memory.
//
// These routines are named Word32At(), Word64At() and Google1At().
// Long ago, the 32-bit version of this operation was implemented using
// signed characters.  The hash function that used this variant creates
// persistent hash values.  The hash routine needs to remain backwards
// compatible, so we renamed the word loading function 'Google1At' to
// make it clear this implements special functionality.
//
// If a machine has alignment constraints or is big endian, we must
// load the word a byte at a time.  Otherwise we can load the whole word
// from memory.
//
// [Plausibly, Word32At() and Word64At() should really be called
// UNALIGNED_LITTLE_ENDIAN_LOAD32() and UNALIGNED_LITTLE_ENDIAN_LOAD64()
// but that seems overly verbose.]

#if defined(IS_LITTLE_ENDIAN)
inline uint64_t Word64At(const char* ptr) { return UNALIGNED_LOAD64(ptr); }

inline uint32_t Word32At(const char* ptr) { return UNALIGNED_LOAD32(ptr); }

// This produces the same results as the byte-by-byte version below.
// Here, we mask off the sign bits and subtract off two copies.  To
// see why this is the same as adding together the sign extensions,
// start by considering the low-order byte.  If we loaded an unsigned
// word and wanted to sign extend it, we isolate the sign bit and subtract
// that from zero which gives us a sequence of bits matching the sign bit
// at and above the sign bit.  If we remove (subtract) the sign bit and
// add in the low order byte, we now have a sign-extended byte as desired.
// We can then operate on all four bytes in parallel because addition
// is associative and commutative.
//
// For example, consider sign extending the bytes 0x01 and 0x81.  For 0x01,
// the sign bit is zero, and 0x01 - 0 -0 = 1.  For 0x81, the sign bit is 1
// and we are computing 0x81 - 0x80 + (-0x80) == 0x01 + 0xFFFFFF80.
//
// Similarily, if we start with 0x8200 and want to sign extend that,
// we end up calculating 0x8200 - 0x8000 + (-0x8000) == 0xFFFF8000 + 0x0200
//
// Suppose we have two bytes at the same time.  Doesn't the adding of all
// those F's generate something wierd?  Ignore the F's and reassociate
// the addition.  For 0x8281, processing the bytes one at a time (like
// we used to do) calculates
//      [0x8200 - 0x8000 + (-0x8000)] + [0x0081 - 0x80 + (-0x80)]
//   == 0x8281 - 0x8080 - 0x8000 - 0x80
//   == 0x8281 - 0x8080 - 0x8080

inline uint32_t Google1At(const char* ptr) {
  uint32_t t = UNALIGNED_LOAD32(ptr);
  uint32_t masked = (t << 1) & 0x1010100;
  return t - masked;
}

// This is equivalent to calling 'Google1At' twice. E.g. the following two
// code snippets are equivalent:
// 1)
//   uint32_t v0 = Google1At(ptr);
//   uint32_t v1 = Google1At(ptr + 4);
//
// 2)
//   uint32_t v0, v1;
//   Google1At2x(ptr, v0, v1);
inline void Google1At2x(const char* ptr, uint32_t& v0, uint32_t& v1) {
  uint64_t t = UNALIGNED_LOAD64(ptr);
  // Please note that at first it may seem like the most significant bit of
  // uint32_t at ptr may interfere with the least significant bit of the masked
  // for the uint32_t at 'ptr + 4'. But, it won't happen because the mask we are
  // using to 'and' has 0 at that bit location. That's why 't << 1' here is ok.
  uint64_t masked = (t << 1) & 0x0101010001010100ULL;
  v0 = static_cast<uint32_t>(t) - static_cast<uint32_t>(masked);
  v1 = static_cast<uint32_t>(t >> 32) - static_cast<uint32_t>(masked >> 32);
}

#else

// NOTE:  This code is not normally used or tested.

inline uint64_t Word64At(const char* ptr) {
  return (static_cast<uint64_t>(ptr[0]) + (static_cast<uint64_t>(ptr[1]) << 8) +
          (static_cast<uint64_t>(ptr[2]) << 16) +
          (static_cast<uint64_t>(ptr[3]) << 24) +
          (static_cast<uint64_t>(ptr[4]) << 32) +
          (static_cast<uint64_t>(ptr[5]) << 40) +
          (static_cast<uint64_t>(ptr[6]) << 48) +
          (static_cast<uint64_t>(ptr[7]) << 56));
}

inline uint32_t Word32At(const char* ptr) {
  return (static_cast<uint32_t>(ptr[0]) + (static_cast<uint32_t>(ptr[1]) << 8) +
          (static_cast<uint32_t>(ptr[2]) << 16) +
          (static_cast<uint32_t>(ptr[3]) << 24));
}

inline uint32_t Google1At(const char* ptr2) {
  const signed char* ptr = reinterpret_cast<const signed char*>(ptr2);
  return (static_cast<signed char>(ptr[0]) +
          (static_cast<uint32_t>(ptr[1]) << 8) +
          (static_cast<uint32_t>(ptr[2]) << 16) +
          (static_cast<uint32_t>(ptr[3]) << 24));
}

inline void Google1At2x(const char* ptr, uint32_t& v0, uint32_t& v1) {
  v0 = Google1At(ptr);
  v1 = Google1At(ptr + 4);
}

#endif /* IS_LITTLE_ENDIAN */

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_JENKINS_LOOKUP2_H_
