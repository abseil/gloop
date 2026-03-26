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

// What follows is the concatenation of farmhash.h and farmhash.cc from
// FarmHash 1.1; all modifications are listed:
//  .  Add FARMHASH_ prefix to x86, x86_64, is_64bit macros.
//  .  NAMESPACE_FOR_HASH_FUNCTIONS defaults to farmhash (instead of util)
//  .  Moved farmhash::Fingerprint* from here to farmhash_fingerprint.{h,cc}
//  .  Removed #include "farmhash.h".
//  .  Added #include "gloop/base/port.h"; #define FARMHASH_LITTLE_ENDIAN or
//     FARMHASH_BIG_ENDIAN.
//  .  Include guards are changed to this Google's style
//  .  Removed "using namespace std"
//  .  Removed Hash128to64(); made its two uses explicitly use the
//     same constants as before so that farmhash::Fingerprint() is unchanging.
//  .  Modified Check macros to use std::hex instead of just hex, and
//     reformatted them.
//  .  Marked functions defined here "inline" as is normal for functions
//     defined in header files.  This prevents ODR violations.
//     (Functions to mark were found by
//       egrep -nH -e '^uint.*[y ]Hash|^size_t Hash|^uint.* Fing' farmhash.h.)
//  .  Added #include "gloop/base/int128.h"; added ToGoogleU128() to convert
//  "our"
//     128-bit integer type to Abseil's; added ToFarmHashU128() to convert
//     Abseil's 128-bit integer type to ours.
//  .  Do not warn about possible data loss on conversion from 'size_t' to
//     'uint32_t' when building with MSVC
//  .  Do not define LIKELY with __builtin_expect with MSVC.
//  .  Made farmhashxx namespaces be sub-namespaces of
//     NAMESPACE_FOR_HASH_FUNCTIONS, thereby permitting us to remove
//     "using namespace NAMESPACE_FOR_HASH_FUNCTIONS".
//  .  Removed Fetch, Rotate, and Bswap macros, as they are too
//     likely to collide with other uses of those names.
//  .  Removed Farmhash self-test functionality.
//  .  Remove debug_mode macro and merge it with FARMHASH_DEBUG.
//  .  Replaces the code responsible for the portable bswap_32/bswap_64
//     implementation with calls to absl/base:endian interfaces.
//  .  Replaced the bodies of farmhash::Hash64WithSeed{,s} to make them faster.
//  .  Changes to make `Farmhash64` callable from CUDA:
//     (a) Replaced use of pair and make_pair with a struct (uint128_parts) so
//         that the code works with CUDA compiler. And changed its member names
//         from first/second to low/high to better describe their intent.
//     (b) Decorate Farmhash64 and any function it calls with __host__
//         __device__
//     (c) Eliminate pre-C++11 #include <algorithm> for std::swap (since we
//         don't support pre-C++11).
//     (d) use locally defined swap() instead of std::swap.
//  .  Disable warnings related to shortening of 64-bit to 32-bit,
//     sign conversion and shadow variables when building with Clang.
//  .  If NAMESPACE_FOR_HASH_FUNCTIONS is not defined, undef it at the end of
//     the header file so that targets that use both farmhash and
//     the TP/their internal copy continue to work together.
// This is used by other files in this package, but please do not use it
// for anything else.  Please ask <internal team> if you want to break this
// rule.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_FARMHASH_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_FARMHASH_H_

#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"

// Using farmhashte via this header encourages the compiler to use a function
// call rather than inlining.
#include "gloop/util/hash/farmhashte.h"

// This file provides a few functions for hashing strings and other
// data.  All of them are high-quality functions in the sense that
// they do well on standard tests such as Austin Appleby's SMHasher.
// They're also fast.  FarmHash is the successor to CityHash.
//
// Functions in the FarmHash family are not suitable for cryptography.
//
// WARNING: This code has been only lightly tested on big-endian platforms!
// It is known to work well on little-endian platforms that have a small penalty
// for unaligned reads, such as current Intel and AMD moderate-to-high-end CPUs.
// It should work on all 32-bit and 64-bit platforms that allow unaligned reads;
// bug reports are welcome.
//
// By the way, for some hash functions, given strings a and b, the hash
// of a+b is easily derived from the hashes of a and b.  This property
// doesn't hold for any hash functions in this file.

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>  // for memcpy and memset

#include <utility>

#include "gloop/util/endian/endian.h"

#ifndef NAMESPACE_FOR_HASH_FUNCTIONS
#define NAMESPACE_FOR_HASH_FUNCTIONS farmhash
// define flag to undef the namespace for farmhash at the end.
#define UNDEF_NAMESPACE_FOR_HASH_FUNCTIONS
#endif

namespace NAMESPACE_FOR_HASH_FUNCTIONS {

// If compiling with CUDA, enable a subset of functions for GPU by tagging them
// with __device__ __host__.
#ifdef __CUDACC__
#define FARMHASH_HOST_DEVICE __host__ __device__
#else
#define FARMHASH_HOST_DEVICE
#endif  // __CUDACC__

// __CUDA_ARCH__ is defined when compiling device code.
#ifdef __CUDA_ARCH__
// CUDA code is always little endian.
__device__ inline uint64_t ToHost64(uint64_t x) { return x; }
__device__ inline uint32_t ToHost32(uint32_t x) { return x; }
#else
inline uint64_t ToHost64(uint64_t x) { return LittleEndian::ToHost64(x); }
inline uint32_t ToHost32(uint32_t x) { return LittleEndian::ToHost32(x); }
#endif

// Cannot use std::swap from CUDA code.
template <typename T>
FARMHASH_HOST_DEVICE inline void swap(T& x, T& y) noexcept {
#ifdef __CUDA_ARCH__
  T t = x;
  x = y;
  y = t;
#else
  std::swap(x, y);
#endif
}

#if defined(FARMHASH_UINT128_T_DEFINED)
inline uint64_t Uint128Low64(const uint128_t x) {
  return static_cast<uint64_t>(x);
}
inline uint64_t Uint128High64(const uint128_t x) {
  return static_cast<uint64_t>(x >> 64);
}
inline uint128_t Uint128(uint64_t lo, uint64_t hi) {
  return lo + (((uint128_t)hi) << 64);
}
#else
// 128-bit integer defined using low and high 64-bit parts.
struct uint128_parts {
  uint64_t low;
  uint64_t high;
};

using uint128_t = uint128_parts;

FARMHASH_HOST_DEVICE inline uint64_t Uint128Low64(const uint128_t x) {
  return x.low;
}
FARMHASH_HOST_DEVICE inline uint64_t Uint128High64(const uint128_t x) {
  return x.high;
}
FARMHASH_HOST_DEVICE inline uint128_t Uint128(uint64_t lo, uint64_t hi) {
  return uint128_t{lo, hi};
}
inline uint128_t ToFarmHashU128(absl::uint128 x) {
  return uint128_t{::absl::Uint128Low64(x), ::absl::Uint128High64(x)};
}
#endif

inline absl::uint128 ToGoogleU128(uint128_t x) {
  return absl::MakeUint128(Uint128High64(x), Uint128Low64(x));
}

// BASIC STRING HASHING

// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline size_t Hash(const char* s, size_t len);

// Hash function for a byte array.  Most useful in 32-bit binaries.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint32_t Hash32(const char* s, size_t len);

// Hash function for a byte array.  For convenience, a 32-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed);

// Hash a byte array down to 64 bits of output.
// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint64_t Hash64(const char* s, size_t len);

// Hash function for a byte array.  For convenience, a 64-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed);

// Hash function for a byte array.  For convenience, two seeds are also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1);

// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint128_t Hash128(const char* s, size_t len);

// Hash function for a byte array.  For convenience, a 128-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint128_t Hash128WithSeed(const char* s, size_t len, uint128_t seed);

#ifndef FARMHASH_NO_CXX_STRING

// Convenience functions to hash or fingerprint C++ strings.
// These require that Str::data() return a pointer to the first char
// (as a const char*) and that Str::length() return the string's length;
// they work with std::string, for example.

// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline size_t Hash(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash(s.data(), s.length());
}

// Hash function for a byte array.  Most useful in 32-bit binaries.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint32_t Hash32(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash32(s.data(), s.length());
}

// Hash function for a byte array.  For convenience, a 32-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint32_t Hash32WithSeed(const Str& s, uint32_t seed) {
  assert(sizeof(s[0]) == 1);
  return Hash32WithSeed(s.data(), s.length(), seed);
}

// Hash 128 input bits down to 64 bits of output.
// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint64_t Hash64(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash64(s.data(), s.length());
}

// Hash function for a byte array.  For convenience, a 64-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint64_t Hash64WithSeed(const Str& s, uint64_t seed) {
  assert(sizeof(s[0]) == 1);
  return Hash64WithSeed(s.data(), s.length(), seed);
}

// Hash function for a byte array.  For convenience, two seeds are also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint64_t Hash64WithSeeds(const Str& s, uint64_t seed0, uint64_t seed1) {
  assert(sizeof(s[0]) == 1);
  return Hash64WithSeeds(s.data(), s.length(), seed0, seed1);
}

// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint128_t Hash128(const Str& s) {
  assert(sizeof(s[0]) == 1);
  return Hash128(s.data(), s.length());
}

// Hash function for a byte array.  For convenience, a 128-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
template <typename Str>
inline uint128_t Hash128WithSeed(const Str& s, uint128_t seed) {
  assert(sizeof(s[0]) == 1);
  return Hash128(s.data(), s.length(), seed);
}

#endif

}  // namespace NAMESPACE_FOR_HASH_FUNCTIONS

// FARMHASH ASSUMPTIONS: Modify as needed, or use -DFARMHASH_ASSUME_SSE42 etc.
// Note that if you use -DFARMHASH_ASSUME_SSE42 you likely need -msse42
// (or its equivalent for your compiler); if you use -DFARMHASH_ASSUME_AESNI
// you likely need -maes (or its equivalent for your compiler).

#ifdef FARMHASH_ASSUME_SSSE3
#undef FARMHASH_ASSUME_SSSE3
#define FARMHASH_ASSUME_SSSE3 1
#endif

#ifdef FARMHASH_ASSUME_SSE41
#undef FARMHASH_ASSUME_SSE41
#define FARMHASH_ASSUME_SSE41 1
#endif

#ifdef FARMHASH_ASSUME_SSE42
#undef FARMHASH_ASSUME_SSE42
#define FARMHASH_ASSUME_SSE42 1
#endif

#ifdef FARMHASH_ASSUME_AESNI
#undef FARMHASH_ASSUME_AESNI
#define FARMHASH_ASSUME_AESNI 1
#endif

#ifdef FARMHASH_ASSUME_AVX
#undef FARMHASH_ASSUME_AVX
#define FARMHASH_ASSUME_AVX 1
#endif

// FARMHASH PORTABILITY LAYER: Runtime error if misconfigured

#ifndef FARMHASH_DIE_IF_MISCONFIGURED
#define FARMHASH_DIE_IF_MISCONFIGURED \
  do {                                \
    *(char*)(len % 17) = 0;           \
  } while (0)
#endif

// FARMHASH PORTABILITY LAYER: LIKELY and UNLIKELY

#if !defined(LIKELY)
#if defined(FARMHASH_NO_BUILTIN_EXPECT) ||        \
    (defined(FARMHASH_OPTIONAL_BUILTIN_EXPECT) && \
     !defined(HAVE_BUILTIN_EXPECT)) ||            \
    defined(_MSC_VER)
#define LIKELY(x) (x)
#else
#define LIKELY(x) (__builtin_expect(!!(x), 1))
#endif
#endif

#undef UNLIKELY
#define UNLIKELY(x) !LIKELY(!(x))

namespace NAMESPACE_FOR_HASH_FUNCTIONS {

// FARMHASH PORTABILITY LAYER: endianness and byteswapping functions

FARMHASH_HOST_DEVICE inline uint64_t Fetch64(const char* p) {
  uint64_t result;
  memcpy(&result, p, sizeof(result));
  return ToHost64(result);
}

FARMHASH_HOST_DEVICE inline uint32_t Fetch32(const char* p) {
  uint32_t result;
  memcpy(&result, p, sizeof(result));
  return ToHost32(result);
}

inline uint32_t Bswap32(uint32_t val) { return absl::byteswap(val); }
inline uint64_t Bswap64(uint64_t val) { return absl::byteswap(val); }

// FARMHASH PORTABILITY LAYER: bitwise rot

FARMHASH_HOST_DEVICE inline uint32_t BasicRotate32(uint32_t val, int shift) {
  // Avoid shifting by 32: doing so yields an undefined result.
  return shift == 0 ? val : ((val >> shift) | (val << (32 - shift)));
}

FARMHASH_HOST_DEVICE inline uint64_t BasicRotate64(uint64_t val, int shift) {
  // Avoid shifting by 64: doing so yields an undefined result.
  return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}

#if defined(_MSC_VER) && defined(FARMHASH_ROTR)

inline uint32_t Rotate32(uint32_t val, int shift) {
  return sizeof(unsigned long) == sizeof(val) ? _lrotr(val, shift)
                                              : BasicRotate32(val, shift);
}

inline uint64_t Rotate64(uint64_t val, int shift) {
  return sizeof(unsigned long) == sizeof(val) ? _lrotr(val, shift)
                                              : BasicRotate64(val, shift);
}

#else

inline uint32_t Rotate32(uint32_t val, int shift) {
  return BasicRotate32(val, shift);
}
FARMHASH_HOST_DEVICE inline uint64_t Rotate64(uint64_t val, int shift) {
  return BasicRotate64(val, shift);
}

#endif

}  // namespace NAMESPACE_FOR_HASH_FUNCTIONS

// FARMHASH PORTABILITY LAYER: debug mode or max speed?
// One may use -DFARMHASH_DEBUG=1 or -DFARMHASH_DEBUG=0 to force the issue.

#if !defined(FARMHASH_DEBUG)
#if !defined(NDEBUG) || defined(_DEBUG)
#define FARMHASH_DEBUG 1
#else
#define FARMHASH_DEBUG 0
#endif
#endif

// PLATFORM-SPECIFIC FUNCTIONS AND MACROS

#undef FARMHASH_x86_64
#if defined(__x86_64) || defined(__x86_64__)
#define FARMHASH_x86_64 1
#else
#define FARMHASH_x86_64 0
#endif

#undef FARMHASH_aarch64
#if defined(__aarch64__)
#define FARMHASH_aarch64 1
#else
#define FARMHASH_aarch64 0
#endif

#undef FARMHASH_x86
#if defined(__i386__) || defined(__i386) || defined(__X86__)
#define FARMHASH_x86 1
#else
#define FARMHASH_x86 FARMHASH_x86_64
#endif

#if !defined(FARMHASH_is_64bit)
#define FARMHASH_is_64bit \
  (FARMHASH_x86_64 || FARMHASH_aarch64 || (sizeof(void*) == 8))
#endif

#undef can_use_ssse3
#if defined(__SSSE3__) || defined(FARMHASH_ASSUME_SSSE3)

#include <emmintrin.h>
#include <immintrin.h>
#define can_use_ssse3 1
// Now we can use _mm_hsub_epi16 and so on.

#else
#define can_use_ssse3 0
#endif

#undef can_use_sse41
#if defined(__SSE4_1__) || defined(FARMHASH_ASSUME_SSE41)

#include <immintrin.h>
#define can_use_sse41 1
// Now we can use _mm_insert_epi64 and so on.

#else
#define can_use_sse41 0
#endif

#undef can_use_sse42
#if defined(__SSE4_2__) || defined(FARMHASH_ASSUME_SSE42)

#include <nmmintrin.h>
#define can_use_sse42 1
// Now we can use _mm_crc32_u{32,16,8}.  And on 64-bit platforms, _mm_crc32_u64.

#else
#define can_use_sse42 0
#endif

#undef can_use_aesni
#if defined(__AES__) || defined(FARMHASH_ASSUME_AESNI)

#include <wmmintrin.h>
#define can_use_aesni 1
// Now we can use _mm_aesimc_si128 and so on.

#else
#define can_use_aesni 0
#endif

#undef can_use_avx
#if defined(__AVX__) || defined(FARMHASH_ASSUME_AVX)

#include <immintrin.h>
#define can_use_avx 1

#else
#define can_use_avx 0
#endif

#undef can_use_neon
#if defined(__aarch64__) && (defined(__ARM_NEON) || defined(__ARM_NEON__) || \
                             defined(FARMHASH_ASSUME_NEON))
#include <arm_neon.h>
#define can_use_neon 1
#else
#define can_use_neon 0
#endif

#undef can_use_aarch_crypto
#if defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#define can_use_aarch_crypto 1
#else
#define can_use_aarch_crypto 0
#endif

namespace farmhash_helpers {
#if can_use_ssse3 || can_use_sse41 || can_use_sse42 || can_use_aesni || \
    can_use_avx
using V128 = __m128i;
#elif can_use_neon
using V128 = uint8x16_t;
#endif

#if can_use_ssse3 || can_use_sse41 || can_use_sse42 || can_use_aesni || \
    can_use_avx
inline V128 Fetch128(const char* s) {
  return _mm_loadu_si128(reinterpret_cast<const V128*>(s));
}
inline void Store128(void* p, V128 s) {
  return _mm_storeu_si128(reinterpret_cast<V128*>(p), s);
}
inline V128 Add(V128 x, V128 y) { return _mm_add_epi64(x, y); }
inline V128 Xor(V128 x, V128 y) { return _mm_xor_si128(x, y); }
inline V128 Or(V128 x, V128 y) { return _mm_or_si128(x, y); }
#if can_use_sse41
inline V128 Mul(V128 x, V128 y) { return _mm_mullo_epi32(x, y); }
#endif
#if can_use_ssse3
inline V128 Shuf(V128 x, V128 y) { return _mm_shuffle_epi8(y, x); }
#endif
inline V128 Shuffle0321(V128 x) {
  return _mm_shuffle_epi32(x, _MM_SHUFFLE(0, 3, 2, 1));
}
inline V128 Set16(signed char b15, signed char b14, signed char b13,
                  signed char b12, signed char b11, signed char b10,
                  signed char b9, signed char b8, signed char b7,
                  signed char b6, signed char b5, signed char b4,
                  signed char b3, signed char b2, signed char b1,
                  signed char b0) {
  return _mm_set_epi8(b15, b14, b13, b12, b11, b10, b9, b8, b7, b6, b5, b4, b3,
                      b2, b1, b0);
}
inline V128 SetSame32(int32_t a) { return _mm_set1_epi32(a); }
#if can_use_sse41
inline V128 Move64Low(int64_t a) { return _mm_cvtsi64_si128(a); }
#endif
inline V128 Mul5(V128 x) { return Add(x, _mm_slli_epi32(x, 2)); }
inline V128 RotateLeft(V128 x, int c) {
  return Or(_mm_slli_epi32(x, c), _mm_srli_epi32(x, 32 - c));
}
inline V128 Rol17(V128 x) { return RotateLeft(x, 17); }
inline V128 Rol19(V128 x) { return RotateLeft(x, 19); }
#if can_use_sse42
inline uint32_t CRC32_u32(uint32_t crc, uint32_t v) {
  return _mm_crc32_u32(crc, v);
}
#endif
#if can_use_aesni
inline V128 AES_IMC(V128 x) { return _mm_aesimc_si128(x); }
#endif

#elif can_use_neon

inline V128 Fetch128(const char* s) {
  return vld1q_u8(reinterpret_cast<const uint8_t*>(s));
}
inline void Store128(void* p, V128 s) {
  return vst1q_u8(reinterpret_cast<uint8_t*>(p), s);
}
inline V128 Add(V128 x, V128 y) {
  return vreinterpretq_u8_s64(
      vaddq_s64(vreinterpretq_s64_u8(x), vreinterpretq_s64_u8(y)));
}
inline V128 Or(V128 x, V128 y) { return vorrq_u8(x, y); }
inline V128 Xor(V128 x, V128 y) { return veorq_u8(x, y); }
inline V128 Mul(V128 x, V128 y) {
  return vreinterpretq_u8_s32(
      vmulq_s32(vreinterpretq_s32_u8(x), vreinterpretq_s32_u8(y)));
}
inline V128 Shuf(V128 x, V128 y) {
  return vreinterpretq_u8_s8(vqtbl1q_s8(vreinterpretq_s8_u8(y), x));
}
inline V128 Shuffle0321(V128 x) {
  return vreinterpretq_u8_s32(
      vextq_s32(vreinterpretq_s32_u8(x), vreinterpretq_s32_u8(x), 1));
}
inline V128 Set16(signed char b15, signed char b14, signed char b13,
                  signed char b12, signed char b11, signed char b10,
                  signed char b9, signed char b8, signed char b7,
                  signed char b6, signed char b5, signed char b4,
                  signed char b3, signed char b2, signed char b1,
                  signed char b0) {
  int8_t data[16] = {(int8_t)b0,  (int8_t)b1,  (int8_t)b2,  (int8_t)b3,
                     (int8_t)b4,  (int8_t)b5,  (int8_t)b6,  (int8_t)b7,
                     (int8_t)b8,  (int8_t)b9,  (int8_t)b10, (int8_t)b11,
                     (int8_t)b12, (int8_t)b13, (int8_t)b14, (int8_t)b15};
  return vreinterpretq_u8_s8(vld1q_s8(data));
}
inline V128 SetSame32(int32_t a) {
  return vreinterpretq_u8_s32(vdupq_n_s32(a));
}
inline V128 Move64Low(int64_t a) {
  return vreinterpretq_u8_s64(vsetq_lane_s64(a, vdupq_n_s64(0), 0));
}
inline V128 Mul5(V128 x) {
  return Add(x, vreinterpretq_u8_s32(
                    vshlq_s32(vreinterpretq_s32_u8(x), vdupq_n_s32(2))));
}
inline V128 RotateLeft(V128 x, int c) {
  return Or(
      vreinterpretq_u8_s32(vshlq_s32(vreinterpretq_s32_u8(x), vdupq_n_s32(c))),
      vreinterpretq_u8_s32(
          vshlq_s32(vreinterpretq_s32_u8(x), vdupq_n_s32(-32 + c))));
}
inline V128 Rol17(V128 x) { return RotateLeft(x, 17); }
inline V128 Rol19(V128 x) { return RotateLeft(x, 19); }
#if can_use_aarch_crypto
inline uint32_t CRC32_u32(uint32_t crc, uint32_t v) {
  return __crc32cw(crc, v);
}
#endif
inline V128 AES_IMC(V128 v) {
  const uint8_t kRor32by8[] = {
      0x1, 0x2, 0x3, 0x0, 0x5, 0x6, 0x7, 0x4,
      0x9, 0xa, 0xb, 0x8, 0xd, 0xe, 0xf, 0xc,
  };
  uint8x16_t w;
  w = (v << 1) ^ vreinterpretq_u8_s8((vreinterpretq_s8_u8(v) >> 7) & 0x1b);
  w = (w << 1) ^ vreinterpretq_u8_s8((vreinterpretq_s8_u8(w) >> 7) & 0x1b);
  v ^= w;
  v ^= vreinterpretq_u8_u16(vrev32q_u16(vreinterpretq_u16_u8(w)));
  w = (v << 1) ^ vreinterpretq_u8_s8((vreinterpretq_s8_u8(v) >> 7) & 0x1b);
  w ^= vreinterpretq_u8_u16(vrev32q_u16(vreinterpretq_u16_u8(v)));
  w ^= vqtbl1q_u8(v ^ w, vld1q_u8(kRor32by8));
  return w;
}
#endif
}  //  namespace farmhash_helpers

// Building blocks for hash functions

#undef PERMUTE3
#define PERMUTE3(a, b, c) \
  do {                    \
    swap(a, b);           \
    swap(a, c);           \
  } while (false)

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4267)
#endif  // defined(_MSC_VER)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshadow"
#endif  // defined(__clang__)

namespace NAMESPACE_FOR_HASH_FUNCTIONS {

// Some primes between 2^63 and 2^64 for various uses.
static const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
static const uint64_t k1 = 0xb492b66fbe98f273ULL;
static const uint64_t k2 = 0x9ae16a3b2f90404fULL;

// Magic numbers for 32-bit hashing.  Copied from Murmur3.
static const uint32_t c1 = 0xcc9e2d51;
static const uint32_t c2 = 0x1b873593;

// A 32-bit to 32-bit integer hash copied from Murmur3.
inline uint32_t fmix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

inline uint32_t Mur(uint32_t a, uint32_t h) {
  // Helper from Murmur3 for combining two 32-bit values.
  a *= c1;
  a = Rotate32(a, 17);
  a *= c2;
  h ^= a;
  h = Rotate32(h, 19);
  return h * 5 + 0xe6546b64;
}

inline uint32_t DebugTweak(uint32_t x) {
  if (FARMHASH_DEBUG) {
    x = ~Bswap32(x * c1);
  }
  return x;
}

inline uint64_t DebugTweak(uint64_t x) {
  if (FARMHASH_DEBUG) {
    x = ~Bswap64(x * k1);
  }
  return x;
}

inline uint128_t DebugTweak(uint128_t x) {
  if (FARMHASH_DEBUG) {
    uint64_t y = DebugTweak(Uint128Low64(x));
    uint64_t z = DebugTweak(Uint128High64(x));
    y += z;
    z += y;
    x = uint128_t{y, z * k1};
  }
  return x;
}

namespace farmhashna {
FARMHASH_HOST_DEVICE inline uint64_t ShiftMix(uint64_t val) {
  return val ^ (val >> 47);
}

FARMHASH_HOST_DEVICE inline uint64_t HashLen16(
    uint64_t u, uint64_t v, uint64_t mul = 0x9ddfea08eb382d69ULL) {
  // Murmur-inspired hashing.
  uint64_t a = (u ^ v) * mul;
  a ^= (a >> 47);
  uint64_t b = (v ^ a) * mul;
  b ^= (b >> 47);
  b *= mul;
  return b;
}

FARMHASH_HOST_DEVICE inline uint64_t HashLen0to16(const char* s, size_t len) {
  if (len >= 8) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch64(s) + k2;
    uint64_t b = Fetch64(s + len - 8);
    uint64_t c = Rotate64(b, 37) * mul + a;
    uint64_t d = (Rotate64(a, 25) + b) * mul;
    return HashLen16(c, d, mul);
  }
  if (len >= 4) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch32(s);
    return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
  }
  if (len > 0) {
    uint8_t a = s[0];
    uint8_t b = s[len >> 1];
    uint8_t c = s[len - 1];
    uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
    uint32_t z = len + (static_cast<uint32_t>(c) << 2);
    return ShiftMix(y * k2 ^ z * k0) * k2;
  }
  return k2;
}

// This probably works well for 16-byte strings as well, but it may be overkill
// in that case.
FARMHASH_HOST_DEVICE inline uint64_t HashLen17to32(const char* s, size_t len) {
  uint64_t mul = k2 + len * 2;
  uint64_t a = Fetch64(s) * k1;
  uint64_t b = Fetch64(s + 8);
  uint64_t c = Fetch64(s + len - 8) * mul;
  uint64_t d = Fetch64(s + len - 16) * k2;
  return HashLen16(Rotate64(a + b, 43) + Rotate64(c, 30) + d,
                   a + Rotate64(b + k2, 18) + c, mul);
}

// Return a 16-byte hash for 48 bytes.  Quick and dirty.
// Callers do best to use "random-looking" values for a and b.
FARMHASH_HOST_DEVICE inline uint128_t WeakHashLen32WithSeeds(
    uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b) {
  a += w;
  b = Rotate64(b + a + z, 21);
  uint64_t c = a;
  a += x;
  a += y;
  b += Rotate64(a, 44);
  return Uint128(a + z, b + c);
}

// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
FARMHASH_HOST_DEVICE inline uint128_t WeakHashLen32WithSeeds(const char* s,
                                                             uint64_t a,
                                                             uint64_t b) {
  return WeakHashLen32WithSeeds(Fetch64(s), Fetch64(s + 8), Fetch64(s + 16),
                                Fetch64(s + 24), a, b);
}

// Return an 8-byte hash for 33 to 64 bytes.
FARMHASH_HOST_DEVICE inline uint64_t HashLen33to64(const char* s, size_t len) {
  uint64_t mul = k2 + len * 2;
  uint64_t a = Fetch64(s) * k2;
  uint64_t b = Fetch64(s + 8);
  uint64_t c = Fetch64(s + len - 8) * mul;
  uint64_t d = Fetch64(s + len - 16) * k2;
  uint64_t y = Rotate64(a + b, 43) + Rotate64(c, 30) + d;
  uint64_t z = HashLen16(y, a + Rotate64(b + k2, 18) + c, mul);
  uint64_t e = Fetch64(s + 16) * mul;
  uint64_t f = Fetch64(s + 24);
  uint64_t g = (y + Fetch64(s + len - 32)) * mul;
  uint64_t h = (z + Fetch64(s + len - 24)) * mul;
  return HashLen16(Rotate64(e + f, 43) + Rotate64(g, 30) + h,
                   e + Rotate64(f + a, 18) + g, mul);
}

FARMHASH_HOST_DEVICE inline uint64_t Hash64(const char* s, size_t len) {
  const uint64_t seed = 81;
  if (len <= 32) {
    if (len <= 16) {
      return HashLen0to16(s, len);
    } else {
      return HashLen17to32(s, len);
    }
  } else if (len <= 64) {
    return HashLen33to64(s, len);
  }

  // For strings over 64 bytes we loop.  Internal state consists of
  // 56 bytes: v, w, x, y, and z.
  uint64_t x = seed;

#ifdef COMPILER_MSVC
  // This computation overflows. This is intentional.
#pragma warning(push)
#pragma warning(disable : 4307)
#endif  // COMPILER_MSVC

  uint64_t y = seed * k1 + 113;

#ifdef COMPILER_MSVC
#pragma warning(pop)
#endif  // COMPILER_MSVC

  uint64_t z = ShiftMix(y * k2 + 113) * k2;
  uint128_t v = Uint128(0, 0);
  uint128_t w = Uint128(0, 0);
  x = x * k2 + Fetch64(s);

  // Set end so that after the loop we have 1 to 64 bytes left to process.
  const char* end = s + ((len - 1) / 64) * 64;
  const char* last64 = end + ((len - 1) & 63) - 63;
  assert(s + len - 64 == last64);
  do {
    x = Rotate64(x + y + v.low + Fetch64(s + 8), 37) * k1;
    y = Rotate64(y + v.high + Fetch64(s + 48), 42) * k1;
    x ^= w.high;
    y += v.low + Fetch64(s + 40);
    z = Rotate64(z + w.low, 33) * k1;
    v = WeakHashLen32WithSeeds(s, v.high * k1, x + w.low);
    w = WeakHashLen32WithSeeds(s + 32, z + w.high, y + Fetch64(s + 16));
    swap(z, x);
    s += 64;
  } while (s != end);
  uint64_t mul = k1 + ((z & 0xff) << 1);
  // Make s point to the last 64 bytes of input.
  s = last64;
  w.low += ((len - 1) & 63);
  v.low += w.low;
  w.low += v.low;
  x = Rotate64(x + y + v.low + Fetch64(s + 8), 37) * mul;
  y = Rotate64(y + v.high + Fetch64(s + 48), 42) * mul;
  x ^= w.high * 9;
  y += v.low * 9 + Fetch64(s + 40);
  z = Rotate64(z + w.low, 33) * mul;
  v = WeakHashLen32WithSeeds(s, v.high * mul, x + w.low);
  w = WeakHashLen32WithSeeds(s + 32, z + w.high, y + Fetch64(s + 16));
  swap(z, x);
  return HashLen16(HashLen16(v.low, w.low, mul) + ShiftMix(y) * k0 + z,
                   HashLen16(v.high, w.high, mul) + x, mul);
}

inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1);

inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
  return Hash64WithSeeds(s, len, k2, seed);
}

inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1) {
  return HashLen16(Hash64(s, len) - seed0, seed1);
}
}  // namespace farmhashna
namespace farmhashuo {
inline uint64_t H(uint64_t x, uint64_t y, uint64_t mul, int r) {
  uint64_t a = (x ^ y) * mul;
  a ^= (a >> 47);
  uint64_t b = (y ^ a) * mul;
  return Rotate64(b, r) * mul;
}

inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1) {
  if (len <= 64) {
    return farmhashna::Hash64WithSeeds(s, len, seed0, seed1);
  }

  // For strings over 64 bytes we loop.  Internal state consists of
  // 64 bytes: u, v, w, x, y, and z.
  uint64_t x = seed0;
  uint64_t y = seed1 * k2 + 113;
  uint64_t z = farmhashna::ShiftMix(y * k2) * k2;
  uint128_t v = Uint128(seed0, seed1);
  uint128_t w = Uint128(0, 0);
  uint64_t u = x - z;
  x *= k2;
  uint64_t mul = k2 + (u & 0x82);

  // Set end so that after the loop we have 1 to 64 bytes left to process.
  const char* end = s + ((len - 1) / 64) * 64;
  const char* last64 = end + ((len - 1) & 63) - 63;
  assert(s + len - 64 == last64);
  do {
    uint64_t a0 = Fetch64(s);
    uint64_t a1 = Fetch64(s + 8);
    uint64_t a2 = Fetch64(s + 16);
    uint64_t a3 = Fetch64(s + 24);
    uint64_t a4 = Fetch64(s + 32);
    uint64_t a5 = Fetch64(s + 40);
    uint64_t a6 = Fetch64(s + 48);
    uint64_t a7 = Fetch64(s + 56);
    x += a0 + a1;
    y += a2;
    z += a3;
    v.low += a4;
    v.high += a5 + a1;
    w.low += a6;
    w.high += a7;

    x = Rotate64(x, 26);
    x *= 9;
    y = Rotate64(y, 29);
    z *= mul;
    v.low = Rotate64(v.low, 33);
    v.high = Rotate64(v.high, 30);
    w.low ^= x;
    w.low *= 9;
    z = Rotate64(z, 32);
    z += w.high;
    w.high += z;
    z *= 9;
    swap(u, y);

    z += a0 + a6;
    v.low += a2;
    v.high += a3;
    w.low += a4;
    w.high += a5 + a6;
    x += a1;
    y += a7;

    y += v.low;
    v.low += x - y;
    v.high += w.low;
    w.low += v.high;
    w.high += x - y;
    x += w.high;
    w.high = Rotate64(w.high, 34);
    swap(u, z);
    s += 64;
  } while (s != end);
  // Make s point to the last 64 bytes of input.
  s = last64;
  u *= 9;
  v.high = Rotate64(v.high, 28);
  v.low = Rotate64(v.low, 20);
  w.low += ((len - 1) & 63);
  u += y;
  y += u;
  x = Rotate64(y - x + v.low + Fetch64(s + 8), 37) * mul;
  y = Rotate64(y ^ v.high ^ Fetch64(s + 48), 42) * mul;
  x ^= w.high * 9;
  y += v.low + Fetch64(s + 40);
  z = Rotate64(z + w.low, 33) * mul;
  v = farmhashna::WeakHashLen32WithSeeds(s, v.high * mul, x + w.low);
  w = farmhashna::WeakHashLen32WithSeeds(s + 32, z + w.high,
                                         y + Fetch64(s + 16));
  return H(farmhashna::HashLen16(v.low + x, w.low ^ y, mul) + z - u,
           H(v.high + y, w.high + z, k2, 30) ^ x, k2, 31);
}

inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
  return len <= 64 ? farmhashna::Hash64WithSeed(s, len, seed)
                   : Hash64WithSeeds(s, len, 0, seed);
}

inline uint64_t Hash64(const char* s, size_t len) {
  return len <= 64 ? farmhashna::Hash64(s, len)
                   : Hash64WithSeeds(s, len, 81, 0);
}
}  // namespace farmhashuo
namespace farmhashxo {
inline uint64_t H32(const char* s, size_t len, uint64_t mul, uint64_t seed0 = 0,
                    uint64_t seed1 = 0) {
  uint64_t a = Fetch64(s) * k1;
  uint64_t b = Fetch64(s + 8);
  uint64_t c = Fetch64(s + len - 8) * mul;
  uint64_t d = Fetch64(s + len - 16) * k2;
  uint64_t u = Rotate64(a + b, 43) + Rotate64(c, 30) + d + seed0;
  uint64_t v = a + Rotate64(b + k2, 18) + c + seed1;
  a = farmhashna::ShiftMix((u ^ v) * mul);
  b = farmhashna::ShiftMix((v ^ a) * mul);
  return b;
}

// Return an 8-byte hash for 33 to 64 bytes.
inline uint64_t HashLen33to64(const char* s, size_t len) {
  uint64_t mul0 = k2 - 30;
  uint64_t mul1 = k2 - 30 + 2 * len;
  uint64_t h0 = H32(s, 32, mul0);
  uint64_t h1 = H32(s + len - 32, 32, mul1);
  return ((h1 * mul1) + h0) * mul1;
}

// Return an 8-byte hash for 65 to 96 bytes.
inline uint64_t HashLen65to96(const char* s, size_t len) {
  uint64_t mul0 = k2 - 114;
  uint64_t mul1 = k2 - 114 + 2 * len;
  uint64_t h0 = H32(s, 32, mul0);
  uint64_t h1 = H32(s + 32, 32, mul1);
  uint64_t h2 = H32(s + len - 32, 32, mul1, h0, h1);
  return (h2 * 9 + (h0 >> 17) + (h1 >> 21)) * mul1;
}

inline uint64_t Hash64(const char* s, size_t len) {
  if (len <= 32) {
    if (len <= 16) {
      return farmhashna::HashLen0to16(s, len);
    } else {
      return farmhashna::HashLen17to32(s, len);
    }
  } else if (len <= 64) {
    return HashLen33to64(s, len);
  } else if (len <= 96) {
    return HashLen65to96(s, len);
  } else if (len <= 256) {
    return farmhashna::Hash64(s, len);
  } else {
    return farmhashuo::Hash64(s, len);
  }
}

inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1) {
  return farmhashuo::Hash64WithSeeds(s, len, seed0, seed1);
}

inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
  return farmhashuo::Hash64WithSeed(s, len, seed);
}
}  // namespace farmhashxo
namespace farmhashte {

#if (can_use_sse41 && FARMHASH_x86_64) || (can_use_neon && FARMHASH_aarch64)

// Requires n >= 256.  Requires SSE4.1 | NEON. Should be slightly faster if the
// compiler uses AVX instructions (e.g., use the -mavx flag with GCC).
inline uint64_t Hash64Long(const char* s, size_t n, uint64_t seed0,
                           uint64_t seed1) {
  const farmhash_helpers::V128 kShuf = farmhash_helpers::Set16(
      4, 11, 10, 5, 8, 15, 6, 9, 12, 2, 14, 13, 0, 7, 3, 1);
  const farmhash_helpers::V128 kMult =
      farmhash_helpers::Set16(0xbd, 0xd6, 0x33, 0x39, 0x45, 0x54, 0xfa, 0x03,
                              0x34, 0x3e, 0x33, 0xed, 0xcc, 0x9e, 0x2d, 0x51);
  uint64_t seed2 = (seed0 + 113) * (seed1 + 9);
  uint64_t seed3 = (Rotate64(seed0, 23) + 27) * (Rotate64(seed1, 30) + 111);
  farmhash_helpers::V128 d0 = farmhash_helpers::Move64Low(seed0);
  farmhash_helpers::V128 d1 = farmhash_helpers::Move64Low(seed1);
  farmhash_helpers::V128 d2 = farmhash_helpers::Shuf(kShuf, d0);
  farmhash_helpers::V128 d3 = farmhash_helpers::Shuf(kShuf, d1);
  farmhash_helpers::V128 d4 = farmhash_helpers::Xor(d0, d1);
  farmhash_helpers::V128 d5 = farmhash_helpers::Xor(d1, d2);
  farmhash_helpers::V128 d6 = farmhash_helpers::Xor(d2, d4);
  farmhash_helpers::V128 d7 = farmhash_helpers::SetSame32(seed2 >> 32);
  farmhash_helpers::V128 d8 = farmhash_helpers::Mul(kMult, d2);
  farmhash_helpers::V128 d9 = farmhash_helpers::SetSame32(seed3 >> 32);
  farmhash_helpers::V128 d10 = farmhash_helpers::SetSame32(seed3);
  farmhash_helpers::V128 d11 =
      farmhash_helpers::Add(d2, farmhash_helpers::SetSame32(seed2));
  const char* end = s + (n & ~static_cast<size_t>(255));
  do {
    farmhash_helpers::V128 z;
    z = farmhash_helpers::Fetch128(s);
    d0 = farmhash_helpers::Add(d0, z);
    d1 = farmhash_helpers::Shuf(kShuf, d1);
    d2 = farmhash_helpers::Xor(d2, d0);
    d4 = farmhash_helpers::Xor(d4, z);
    d4 = farmhash_helpers::Xor(d4, d1);
    swap(d0, d6);
    z = farmhash_helpers::Fetch128(s + 16);
    d5 = farmhash_helpers::Add(d5, z);
    d6 = farmhash_helpers::Shuf(kShuf, d6);
    d8 = farmhash_helpers::Shuf(kShuf, d8);
    d7 = farmhash_helpers::Xor(d7, d5);
    d0 = farmhash_helpers::Xor(d0, z);
    d0 = farmhash_helpers::Xor(d0, d6);
    swap(d5, d11);
    z = farmhash_helpers::Fetch128(s + 32);
    d1 = farmhash_helpers::Add(d1, z);
    d2 = farmhash_helpers::Shuf(kShuf, d2);
    d4 = farmhash_helpers::Shuf(kShuf, d4);
    d5 = farmhash_helpers::Xor(d5, z);
    d5 = farmhash_helpers::Xor(d5, d2);
    swap(d10, d4);
    z = farmhash_helpers::Fetch128(s + 48);
    d6 = farmhash_helpers::Add(d6, z);
    d7 = farmhash_helpers::Shuf(kShuf, d7);
    d0 = farmhash_helpers::Shuf(kShuf, d0);
    d8 = farmhash_helpers::Xor(d8, d6);
    d1 = farmhash_helpers::Xor(d1, z);
    d1 = farmhash_helpers::Add(d1, d7);
    z = farmhash_helpers::Fetch128(s + 64);
    d2 = farmhash_helpers::Add(d2, z);
    d5 = farmhash_helpers::Shuf(kShuf, d5);
    d4 = farmhash_helpers::Add(d4, d2);
    d6 = farmhash_helpers::Xor(d6, z);
    d6 = farmhash_helpers::Xor(d6, d11);
    swap(d8, d2);
    z = farmhash_helpers::Fetch128(s + 80);
    d7 = farmhash_helpers::Xor(d7, z);
    d8 = farmhash_helpers::Shuf(kShuf, d8);
    d1 = farmhash_helpers::Shuf(kShuf, d1);
    d0 = farmhash_helpers::Add(d0, d7);
    d2 = farmhash_helpers::Add(d2, z);
    d2 = farmhash_helpers::Add(d2, d8);
    swap(d1, d7);
    z = farmhash_helpers::Fetch128(s + 96);
    d4 = farmhash_helpers::Shuf(kShuf, d4);
    d6 = farmhash_helpers::Shuf(kShuf, d6);
    d8 = farmhash_helpers::Mul(kMult, d8);
    d5 = farmhash_helpers::Xor(d5, d11);
    d7 = farmhash_helpers::Xor(d7, z);
    d7 = farmhash_helpers::Add(d7, d4);
    swap(d6, d0);
    z = farmhash_helpers::Fetch128(s + 112);
    d8 = farmhash_helpers::Add(d8, z);
    d0 = farmhash_helpers::Shuf(kShuf, d0);
    d2 = farmhash_helpers::Shuf(kShuf, d2);
    d1 = farmhash_helpers::Xor(d1, d8);
    d10 = farmhash_helpers::Xor(d10, z);
    d10 = farmhash_helpers::Xor(d10, d0);
    swap(d11, d5);
    z = farmhash_helpers::Fetch128(s + 128);
    d4 = farmhash_helpers::Add(d4, z);
    d5 = farmhash_helpers::Shuf(kShuf, d5);
    d7 = farmhash_helpers::Shuf(kShuf, d7);
    d6 = farmhash_helpers::Add(d6, d4);
    d8 = farmhash_helpers::Xor(d8, z);
    d8 = farmhash_helpers::Xor(d8, d5);
    swap(d4, d10);
    z = farmhash_helpers::Fetch128(s + 144);
    d0 = farmhash_helpers::Add(d0, z);
    d1 = farmhash_helpers::Shuf(kShuf, d1);
    d2 = farmhash_helpers::Add(d2, d0);
    d4 = farmhash_helpers::Xor(d4, z);
    d4 = farmhash_helpers::Xor(d4, d1);
    z = farmhash_helpers::Fetch128(s + 160);
    d5 = farmhash_helpers::Add(d5, z);
    d6 = farmhash_helpers::Shuf(kShuf, d6);
    d8 = farmhash_helpers::Shuf(kShuf, d8);
    d7 = farmhash_helpers::Xor(d7, d5);
    d0 = farmhash_helpers::Xor(d0, z);
    d0 = farmhash_helpers::Xor(d0, d6);
    swap(d2, d8);
    z = farmhash_helpers::Fetch128(s + 176);
    d1 = farmhash_helpers::Add(d1, z);
    d2 = farmhash_helpers::Shuf(kShuf, d2);
    d4 = farmhash_helpers::Shuf(kShuf, d4);
    d5 = farmhash_helpers::Mul(kMult, d5);
    d5 = farmhash_helpers::Xor(d5, z);
    d5 = farmhash_helpers::Xor(d5, d2);
    swap(d7, d1);
    z = farmhash_helpers::Fetch128(s + 192);
    d6 = farmhash_helpers::Add(d6, z);
    d7 = farmhash_helpers::Shuf(kShuf, d7);
    d0 = farmhash_helpers::Shuf(kShuf, d0);
    d8 = farmhash_helpers::Add(d8, d6);
    d1 = farmhash_helpers::Xor(d1, z);
    d1 = farmhash_helpers::Xor(d1, d7);
    swap(d0, d6);
    z = farmhash_helpers::Fetch128(s + 208);
    d2 = farmhash_helpers::Add(d2, z);
    d5 = farmhash_helpers::Shuf(kShuf, d5);
    d4 = farmhash_helpers::Xor(d4, d2);
    d6 = farmhash_helpers::Xor(d6, z);
    d6 = farmhash_helpers::Xor(d6, d9);
    swap(d5, d11);
    z = farmhash_helpers::Fetch128(s + 224);
    d7 = farmhash_helpers::Add(d7, z);
    d8 = farmhash_helpers::Shuf(kShuf, d8);
    d1 = farmhash_helpers::Shuf(kShuf, d1);
    d0 = farmhash_helpers::Xor(d0, d7);
    d2 = farmhash_helpers::Xor(d2, z);
    d2 = farmhash_helpers::Xor(d2, d8);
    swap(d10, d4);
    z = farmhash_helpers::Fetch128(s + 240);
    d3 = farmhash_helpers::Add(d3, z);
    d4 = farmhash_helpers::Shuf(kShuf, d4);
    d6 = farmhash_helpers::Shuf(kShuf, d6);
    d7 = farmhash_helpers::Mul(kMult, d7);
    d5 = farmhash_helpers::Add(d5, d3);
    d7 = farmhash_helpers::Xor(d7, z);
    d7 = farmhash_helpers::Xor(d7, d4);
    swap(d3, d9);
    s += 256;
  } while (s != end);
  d6 = farmhash_helpers::Add(farmhash_helpers::Mul(kMult, d6),
                             farmhash_helpers::Move64Low(n));
  if (n % 256 != 0) {
    d7 = farmhash_helpers::Add(farmhash_helpers::Shuffle0321(d8), d7);
    d8 = farmhash_helpers::Add(
        farmhash_helpers::Mul(kMult, d8),
        farmhash_helpers::Move64Low(farmhashxo::Hash64(s, n % 256)));
  }
  int32_t t[32];
  d0 = farmhash_helpers::Mul(
      kMult, farmhash_helpers::Shuf(kShuf, farmhash_helpers::Mul(kMult, d0)));
  d3 = farmhash_helpers::Mul(
      kMult, farmhash_helpers::Shuf(kShuf, farmhash_helpers::Mul(kMult, d3)));
  d9 = farmhash_helpers::Mul(
      kMult, farmhash_helpers::Shuf(kShuf, farmhash_helpers::Mul(kMult, d9)));
  d1 = farmhash_helpers::Mul(
      kMult, farmhash_helpers::Shuf(kShuf, farmhash_helpers::Mul(kMult, d1)));
  d0 = farmhash_helpers::Add(d11, d0);
  d3 = farmhash_helpers::Xor(d7, d3);
  d9 = farmhash_helpers::Add(d8, d9);
  d1 = farmhash_helpers::Add(d10, d1);
  d4 = farmhash_helpers::Add(d3, d4);
  d5 = farmhash_helpers::Add(d9, d5);
  d6 = farmhash_helpers::Xor(d1, d6);
  d2 = farmhash_helpers::Add(d0, d2);
  farmhash_helpers::Store128(t, d0);
  farmhash_helpers::Store128(t + 4, d3);
  farmhash_helpers::Store128(t + 8, d9);
  farmhash_helpers::Store128(t + 12, d1);
  farmhash_helpers::Store128(t + 16, d4);
  farmhash_helpers::Store128(t + 20, d5);
  farmhash_helpers::Store128(t + 24, d6);
  farmhash_helpers::Store128(t + 28, d2);
  return farmhashxo::Hash64(reinterpret_cast<const char*>(t), sizeof(t));
}

inline uint64_t Hash64(const char* s, size_t len) {
  // Empirically, farmhashxo seems faster until length 512.
  return len >= 512 ? Hash64Long(s, len, k2, k1) : farmhashxo::Hash64(s, len);
}

inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
  return len >= 512 ? Hash64Long(s, len, k1, seed)
                    : farmhashxo::Hash64WithSeed(s, len, seed);
}

inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1) {
  return len >= 512 ? Hash64Long(s, len, seed0, seed1)
                    : farmhashxo::Hash64WithSeeds(s, len, seed0, seed1);
}

#else

inline uint64_t Hash64(const char* s, size_t len) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return s == nullptr ? 0 : len;
}

inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return seed + Hash64(s, len);
}

inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return seed0 + seed1 + Hash64(s, len);
}

#endif  // (can_use_sse41 && FARMHASH_x86_64) || (can_use_neon &&
        // FARMHASH_aarch64)

}  // namespace farmhashte
namespace farmhashnt {
#if (can_use_sse41 && FARMHASH_x86_64) || (can_use_neon && FARMHASH_aarch64)

inline uint32_t Hash32(const char* s, size_t len) {
  return static_cast<uint32_t>(farmhashte::Hash64(s, len));
}

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  return static_cast<uint32_t>(farmhashte::Hash64WithSeed(s, len, seed));
}

#else

inline uint32_t Hash32(const char* s, size_t len) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return s == nullptr ? 0 : len;
}

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return seed + Hash32(s, len);
}

#endif  // (can_use_sse41 && FARMHASH_x86_64) || (can_use_neon &&
        // FARMHASH_aarch64)
}  // namespace farmhashnt
namespace farmhashmk {
inline uint32_t Hash32Len13to24(const char* s, size_t len, uint32_t seed = 0) {
  uint32_t a = Fetch32(s - 4 + (len >> 1));
  uint32_t b = Fetch32(s + 4);
  uint32_t c = Fetch32(s + len - 8);
  uint32_t d = Fetch32(s + (len >> 1));
  uint32_t e = Fetch32(s);
  uint32_t f = Fetch32(s + len - 4);
  uint32_t h = d * c1 + len + seed;
  a = Rotate32(a, 12) + f;
  h = Mur(c, h) + a;
  a = Rotate32(a, 3) + c;
  h = Mur(e, h) + a;
  a = Rotate32(a + f, 12) + d;
  h = Mur(b ^ seed, h) + a;
  return fmix(h);
}

inline uint32_t Hash32Len0to4(const char* s, size_t len, uint32_t seed = 0) {
  uint32_t b = seed;
  uint32_t c = 9;
  for (size_t i = 0; i < len; i++) {
    signed char v = s[i];
    b = b * c1 + v;
    c ^= b;
  }
  return fmix(Mur(b, Mur(len, c)));
}

inline uint32_t Hash32Len5to12(const char* s, size_t len, uint32_t seed = 0) {
  uint32_t a = len, b = len * 5, c = 9, d = b + seed;
  a += Fetch32(s);
  b += Fetch32(s + len - 4);
  c += Fetch32(s + ((len >> 1) & 4));
  return fmix(seed ^ Mur(c, Mur(b, Mur(a, d))));
}

inline uint32_t Hash32(const char* s, size_t len) {
  if (len <= 24) {
    return len <= 12
               ? (len <= 4 ? Hash32Len0to4(s, len) : Hash32Len5to12(s, len))
               : Hash32Len13to24(s, len);
  }

  // len > 24
  uint32_t h = len, g = c1 * len, f = g;
  uint32_t a0 = Rotate32(Fetch32(s + len - 4) * c1, 17) * c2;
  uint32_t a1 = Rotate32(Fetch32(s + len - 8) * c1, 17) * c2;
  uint32_t a2 = Rotate32(Fetch32(s + len - 16) * c1, 17) * c2;
  uint32_t a3 = Rotate32(Fetch32(s + len - 12) * c1, 17) * c2;
  uint32_t a4 = Rotate32(Fetch32(s + len - 20) * c1, 17) * c2;
  h ^= a0;
  h = Rotate32(h, 19);
  h = h * 5 + 0xe6546b64;
  h ^= a2;
  h = Rotate32(h, 19);
  h = h * 5 + 0xe6546b64;
  g ^= a1;
  g = Rotate32(g, 19);
  g = g * 5 + 0xe6546b64;
  g ^= a3;
  g = Rotate32(g, 19);
  g = g * 5 + 0xe6546b64;
  f += a4;
  f = Rotate32(f, 19) + 113;
  size_t iters = (len - 1) / 20;
  do {
    uint32_t a = Fetch32(s);
    uint32_t b = Fetch32(s + 4);
    uint32_t c = Fetch32(s + 8);
    uint32_t d = Fetch32(s + 12);
    uint32_t e = Fetch32(s + 16);
    h += a;
    g += b;
    f += c;
    h = Mur(d, h) + e;
    g = Mur(c, g) + a;
    f = Mur(b + e * c1, f) + d;
    f += g;
    g += f;
    s += 20;
  } while (--iters != 0);
  g = Rotate32(g, 11) * c1;
  g = Rotate32(g, 17) * c1;
  f = Rotate32(f, 11) * c1;
  f = Rotate32(f, 17) * c1;
  h = Rotate32(h + g, 19);
  h = h * 5 + 0xe6546b64;
  h = Rotate32(h, 17) * c1;
  h = Rotate32(h + f, 19);
  h = h * 5 + 0xe6546b64;
  h = Rotate32(h, 17) * c1;
  return h;
}

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  if (len <= 24) {
    if (len >= 13)
      return Hash32Len13to24(s, len, seed * c1);
    else if (len >= 5)
      return Hash32Len5to12(s, len, seed);
    else
      return Hash32Len0to4(s, len, seed);
  }
  uint32_t h = Hash32Len13to24(s, 24, seed ^ len);
  return Mur(Hash32(s + 24, len - 24) + seed, h);
}
}  // namespace farmhashmk
namespace farmhashsu {
#if (can_use_sse42 && can_use_aesni) || (can_use_neon && can_use_aarch_crypto)

inline uint32_t Hash32(const char* s, size_t len) {
  const uint32_t seed = 81;
  if (len <= 24) {
    return len <= 12 ? (len <= 4 ? farmhashmk::Hash32Len0to4(s, len)
                                 : farmhashmk::Hash32Len5to12(s, len))
                     : farmhashmk::Hash32Len13to24(s, len);
  }

  if (len < 40) {
    uint32_t a = len, b = seed * c2, c = a + b;
    a += Fetch32(s + len - 4);
    b += Fetch32(s + len - 20);
    c += Fetch32(s + len - 16);
    uint32_t d = a;
    a = NAMESPACE_FOR_HASH_FUNCTIONS::Rotate32(a, 21);
    a = Mur(a, Mur(b, farmhash_helpers::CRC32_u32(c, d)));
    a += Fetch32(s + len - 12);
    b += Fetch32(s + len - 8);
    d += a;
    a += d;
    b = Mur(b, d) * c2;
    a = farmhash_helpers::CRC32_u32(a, b + c);
    return farmhashmk::Hash32Len13to24(s, (len + 1) / 2, a) + b;
  }

#undef Mulc1
#define Mulc1(x) farmhash_helpers::Mul((x), cc1)

#undef Mulc2
#define Mulc2(x) farmhash_helpers::Mul((x), cc2)

#undef Murk
#define Murk(a, h)                                                             \
  farmhash_helpers::Add(                                                       \
      k, farmhash_helpers::Mul5(farmhash_helpers::Rol19(farmhash_helpers::Xor( \
             Mulc2(farmhash_helpers::Rol17(Mulc1(a))), (h)))))

  const farmhash_helpers::V128 cc1 = farmhash_helpers::SetSame32(c1);
  const farmhash_helpers::V128 cc2 = farmhash_helpers::SetSame32(c2);
  farmhash_helpers::V128 h = farmhash_helpers::SetSame32(seed);
  farmhash_helpers::V128 g = farmhash_helpers::SetSame32(c1 * seed);
  farmhash_helpers::V128 f = g;
  farmhash_helpers::V128 k = farmhash_helpers::SetSame32(0xe6546b64);
  farmhash_helpers::V128 q;
  if (len < 80) {
    farmhash_helpers::V128 a = farmhash_helpers::Fetch128(s);
    farmhash_helpers::V128 b = farmhash_helpers::Fetch128(s + 16);
    farmhash_helpers::V128 c = farmhash_helpers::Fetch128(s + (len - 15) / 2);
    farmhash_helpers::V128 d = farmhash_helpers::Fetch128(s + len - 32);
    farmhash_helpers::V128 e = farmhash_helpers::Fetch128(s + len - 16);
    h = farmhash_helpers::Add(h, a);
    g = farmhash_helpers::Add(g, b);
    q = g;
    g = farmhash_helpers::Shuffle0321(g);
    f = farmhash_helpers::Add(f, c);
    farmhash_helpers::V128 be = farmhash_helpers::Add(b, Mulc1(e));
    h = farmhash_helpers::Add(h, f);
    f = farmhash_helpers::Add(f, h);
    h = farmhash_helpers::Add(Murk(d, h), e);
    k = farmhash_helpers::Xor(k, farmhash_helpers::Shuf(g, f));
    g = farmhash_helpers::Add(farmhash_helpers::Xor(c, g), a);
    f = farmhash_helpers::Add(farmhash_helpers::Xor(be, f), d);
    k = farmhash_helpers::Add(k, be);
    k = farmhash_helpers::Add(k, farmhash_helpers::Shuf(f, h));
    f = farmhash_helpers::Add(f, g);
    g = farmhash_helpers::Add(g, f);
    g = farmhash_helpers::Add(farmhash_helpers::SetSame32(len), Mulc1(g));
  } else {
    // len >= 80
    // The following is loosely modelled after farmhashmk::Hash32.
    size_t iters = (len - 1) / 80;
    len -= iters * 80;

#undef Chunk
#define Chunk()                                                     \
  do {                                                              \
    farmhash_helpers::V128 a = farmhash_helpers::Fetch128(s);       \
    farmhash_helpers::V128 b = farmhash_helpers::Fetch128(s + 16);  \
    farmhash_helpers::V128 c = farmhash_helpers::Fetch128(s + 32);  \
    farmhash_helpers::V128 d = farmhash_helpers::Fetch128(s + 48);  \
    farmhash_helpers::V128 e = farmhash_helpers::Fetch128(s + 64);  \
    h = farmhash_helpers::Add(h, a);                                \
    g = farmhash_helpers::Add(g, b);                                \
    g = farmhash_helpers::Shuffle0321(g);                           \
    f = farmhash_helpers::Add(f, c);                                \
    farmhash_helpers::V128 be = farmhash_helpers::Add(b, Mulc1(e)); \
    h = farmhash_helpers::Add(h, f);                                \
    f = farmhash_helpers::Add(f, h);                                \
    h = farmhash_helpers::Add(h, d);                                \
    q = farmhash_helpers::Add(q, e);                                \
    h = farmhash_helpers::Rol17(h);                                 \
    h = Mulc1(h);                                                   \
    k = farmhash_helpers::Xor(k, farmhash_helpers::Shuf(g, f));     \
    g = farmhash_helpers::Add(farmhash_helpers::Xor(c, g), a);      \
    f = farmhash_helpers::Add(farmhash_helpers::Xor(be, f), d);     \
    swap(f, q);                                                     \
    q = farmhash_helpers::AES_IMC(q);                               \
    k = farmhash_helpers::Add(k, be);                               \
    k = farmhash_helpers::Add(k, farmhash_helpers::Shuf(f, h));     \
    f = farmhash_helpers::Add(f, g);                                \
    g = farmhash_helpers::Add(g, f);                                \
    f = Mulc1(f);                                                   \
  } while (0)

    q = g;
    while (iters-- != 0) {
      Chunk();
      s += 80;
    }

    if (len != 0) {
      h = farmhash_helpers::Add(h, farmhash_helpers::SetSame32(len));
      s = s + len - 80;
      Chunk();
    }
  }

  g = farmhash_helpers::Shuffle0321(g);
  k = farmhash_helpers::Xor(k, g);
  k = farmhash_helpers::Xor(k, q);
  h = farmhash_helpers::Xor(h, q);
  f = Mulc1(f);
  k = Mulc2(k);
  g = Mulc1(g);
  h = Mulc2(h);
  k = farmhash_helpers::Add(k, farmhash_helpers::Shuf(g, f));
  h = farmhash_helpers::Add(h, f);
  f = farmhash_helpers::Add(f, h);
  g = farmhash_helpers::Add(g, k);
  k = farmhash_helpers::Add(k, g);
  k = farmhash_helpers::Xor(k, farmhash_helpers::Shuf(f, h));
  uint32_t buf[16];
  farmhash_helpers::Store128(buf, f);
  farmhash_helpers::Store128(buf + 4, g);
  farmhash_helpers::Store128(buf + 8, k);
  farmhash_helpers::Store128(buf + 12, h);
  uint32_t x = buf[0];
  uint32_t y = buf[1];
  uint32_t z = buf[2];
  x = farmhash_helpers::CRC32_u32(x, buf[3]);
  y = farmhash_helpers::CRC32_u32(y, buf[4]);
  z = farmhash_helpers::CRC32_u32(z * c1, buf[5]);
  x = farmhash_helpers::CRC32_u32(x, buf[6]);
  y = farmhash_helpers::CRC32_u32(y * c1, buf[7]);
  uint32_t o = y;
  z = farmhash_helpers::CRC32_u32(z, buf[8]);
  x = farmhash_helpers::CRC32_u32(x * c1, buf[9]);
  y = farmhash_helpers::CRC32_u32(y, buf[10]);
  z = farmhash_helpers::CRC32_u32(z * c1, buf[11]);
  x = farmhash_helpers::CRC32_u32(x, buf[12]);
  y = farmhash_helpers::CRC32_u32(y * c1, buf[13]);
  z = farmhash_helpers::CRC32_u32(z, buf[14]);
  x = farmhash_helpers::CRC32_u32(x, buf[15]);
  return (o - x + y - z) * c1;
}

#undef Chunk
#undef Murk
#undef Mulc2
#undef Mulc1

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  if (len <= 24) {
    if (len >= 13)
      return farmhashmk::Hash32Len13to24(s, len, seed * c1);
    else if (len >= 5)
      return farmhashmk::Hash32Len5to12(s, len, seed);
    else
      return farmhashmk::Hash32Len0to4(s, len, seed);
  }
  uint32_t h = farmhashmk::Hash32Len13to24(s, 24, seed ^ len);
  return farmhash_helpers::CRC32_u32(Hash32(s + 24, len - 24) + seed, h);
}

#else  // non-vectorized

inline uint32_t Hash32(const char* s, size_t len) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return s == nullptr ? 0 : len;
}

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return seed + Hash32(s, len);
}

#endif  // (can_use_sse42 && can_use_aesni) || (can_use_neon &&
        // can_use_aarch_crypto)

}  // namespace farmhashsu
namespace farmhashsa {
#if can_use_sse42 || (can_use_neon && can_use_aarch_crypto)

inline uint32_t Hash32(const char* s, size_t len) {
  const uint32_t seed = 81;
  if (len <= 24) {
    return len <= 12 ? (len <= 4 ? farmhashmk::Hash32Len0to4(s, len)
                                 : farmhashmk::Hash32Len5to12(s, len))
                     : farmhashmk::Hash32Len13to24(s, len);
  }

  if (len < 40) {
    uint32_t a = len, b = seed * c2, c = a + b;
    a += Fetch32(s + len - 4);
    b += Fetch32(s + len - 20);
    c += Fetch32(s + len - 16);
    uint32_t d = a;
    a = NAMESPACE_FOR_HASH_FUNCTIONS::Rotate32(a, 21);
    a = Mur(a, Mur(b, Mur(c, d)));
    a += Fetch32(s + len - 12);
    b += Fetch32(s + len - 8);
    d += a;
    a += d;
    b = Mur(b, d) * c2;
    a = farmhash_helpers::CRC32_u32(a, b + c);
    return farmhashmk::Hash32Len13to24(s, (len + 1) / 2, a) + b;
  }

#undef Mulc1
#define Mulc1(x) farmhash_helpers::Mul((x), cc1)

#undef Mulc2
#define Mulc2(x) farmhash_helpers::Mul((x), cc2)

#undef Murk
#define Murk(a, h)                                                             \
  farmhash_helpers::Add(                                                       \
      k, farmhash_helpers::Mul5(farmhash_helpers::Rol19(farmhash_helpers::Xor( \
             Mulc2(farmhash_helpers::Rol17(Mulc1(a))), (h)))))

  const farmhash_helpers::V128 cc1 = farmhash_helpers::SetSame32(c1);
  const farmhash_helpers::V128 cc2 = farmhash_helpers::SetSame32(c2);
  farmhash_helpers::V128 h = farmhash_helpers::SetSame32(seed);
  farmhash_helpers::V128 g = farmhash_helpers::SetSame32(c1 * seed);
  farmhash_helpers::V128 f = g;
  farmhash_helpers::V128 k = farmhash_helpers::SetSame32(0xe6546b64);
  if (len < 80) {
    farmhash_helpers::V128 a = farmhash_helpers::Fetch128(s);
    farmhash_helpers::V128 b = farmhash_helpers::Fetch128(s + 16);
    farmhash_helpers::V128 c = farmhash_helpers::Fetch128(s + (len - 15) / 2);
    farmhash_helpers::V128 d = farmhash_helpers::Fetch128(s + len - 32);
    farmhash_helpers::V128 e = farmhash_helpers::Fetch128(s + len - 16);
    h = farmhash_helpers::Add(h, a);
    g = farmhash_helpers::Add(g, b);
    g = farmhash_helpers::Shuffle0321(g);
    f = farmhash_helpers::Add(f, c);
    farmhash_helpers::V128 be = farmhash_helpers::Add(b, Mulc1(e));
    h = farmhash_helpers::Add(h, f);
    f = farmhash_helpers::Add(f, h);
    h = farmhash_helpers::Add(Murk(d, h), e);
    k = farmhash_helpers::Xor(k, farmhash_helpers::Shuf(g, f));
    g = farmhash_helpers::Add(farmhash_helpers::Xor(c, g), a);
    f = farmhash_helpers::Add(farmhash_helpers::Xor(be, f), d);
    k = farmhash_helpers::Add(k, be);
    k = farmhash_helpers::Add(k, farmhash_helpers::Shuf(f, h));
    f = farmhash_helpers::Add(f, g);
    g = farmhash_helpers::Add(g, f);
    g = farmhash_helpers::Add(farmhash_helpers::SetSame32(len), Mulc1(g));
  } else {
    // len >= 80
    // The following is loosely modelled after farmhashmk::Hash32.
    size_t iters = (len - 1) / 80;
    len -= iters * 80;

#undef Chunk
#define Chunk()                                                     \
  do {                                                              \
    farmhash_helpers::V128 a = farmhash_helpers::Fetch128(s);       \
    farmhash_helpers::V128 b = farmhash_helpers::Fetch128(s + 16);  \
    farmhash_helpers::V128 c = farmhash_helpers::Fetch128(s + 32);  \
    farmhash_helpers::V128 d = farmhash_helpers::Fetch128(s + 48);  \
    farmhash_helpers::V128 e = farmhash_helpers::Fetch128(s + 64);  \
    h = farmhash_helpers::Add(h, a);                                \
    g = farmhash_helpers::Add(g, b);                                \
    g = farmhash_helpers::Shuffle0321(g);                           \
    f = farmhash_helpers::Add(f, c);                                \
    farmhash_helpers::V128 be = farmhash_helpers::Add(b, Mulc1(e)); \
    h = farmhash_helpers::Add(h, f);                                \
    f = farmhash_helpers::Add(f, h);                                \
    h = farmhash_helpers::Add(Murk(d, h), e);                       \
    k = farmhash_helpers::Xor(k, farmhash_helpers::Shuf(g, f));     \
    g = farmhash_helpers::Add(farmhash_helpers::Xor(c, g), a);      \
    f = farmhash_helpers::Add(farmhash_helpers::Xor(be, f), d);     \
    k = farmhash_helpers::Add(k, be);                               \
    k = farmhash_helpers::Add(k, farmhash_helpers::Shuf(f, h));     \
    f = farmhash_helpers::Add(f, g);                                \
    g = farmhash_helpers::Add(g, f);                                \
    f = Mulc1(f);                                                   \
  } while (0)

    while (iters-- != 0) {
      Chunk();
      s += 80;
    }

    if (len != 0) {
      h = farmhash_helpers::Add(h, farmhash_helpers::SetSame32(len));
      s = s + len - 80;
      Chunk();
    }
  }

  g = farmhash_helpers::Shuffle0321(g);
  k = farmhash_helpers::Xor(k, g);
  f = Mulc1(f);
  k = Mulc2(k);
  g = Mulc1(g);
  h = Mulc2(h);
  k = farmhash_helpers::Add(k, farmhash_helpers::Shuf(g, f));
  h = farmhash_helpers::Add(h, f);
  f = farmhash_helpers::Add(f, h);
  g = farmhash_helpers::Add(g, k);
  k = farmhash_helpers::Add(k, g);
  k = farmhash_helpers::Xor(k, farmhash_helpers::Shuf(f, h));
  uint32_t buf[16];
  farmhash_helpers::Store128(buf, f);
  farmhash_helpers::Store128(buf + 4, g);
  farmhash_helpers::Store128(buf + 8, k);
  farmhash_helpers::Store128(buf + 12, h);
  uint32_t x = buf[0];
  uint32_t y = buf[1];
  uint32_t z = buf[2];
  x = farmhash_helpers::CRC32_u32(x, buf[3]);
  y = farmhash_helpers::CRC32_u32(y, buf[4]);
  z = farmhash_helpers::CRC32_u32(z * c1, buf[5]);
  x = farmhash_helpers::CRC32_u32(x, buf[6]);
  y = farmhash_helpers::CRC32_u32(y * c1, buf[7]);
  uint32_t o = y;
  z = farmhash_helpers::CRC32_u32(z, buf[8]);
  x = farmhash_helpers::CRC32_u32(x * c1, buf[9]);
  y = farmhash_helpers::CRC32_u32(y, buf[10]);
  z = farmhash_helpers::CRC32_u32(z * c1, buf[11]);
  x = farmhash_helpers::CRC32_u32(x, buf[12]);
  y = farmhash_helpers::CRC32_u32(y * c1, buf[13]);
  z = farmhash_helpers::CRC32_u32(z, buf[14]);
  x = farmhash_helpers::CRC32_u32(x, buf[15]);
  return (o - x + y - z) * c1;
}

#undef Chunk
#undef Murk
#undef Mulc2
#undef Mulc1

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  if (len <= 24) {
    if (len >= 13)
      return farmhashmk::Hash32Len13to24(s, len, seed * c1);
    else if (len >= 5)
      return farmhashmk::Hash32Len5to12(s, len, seed);
    else
      return farmhashmk::Hash32Len0to4(s, len, seed);
  }
  uint32_t h = farmhashmk::Hash32Len13to24(s, 24, seed ^ len);
  return farmhash_helpers::CRC32_u32(Hash32(s + 24, len - 24) + seed, h);
}

#else

inline uint32_t Hash32(const char* s, size_t len) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return s == nullptr ? 0 : len;
}

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  FARMHASH_DIE_IF_MISCONFIGURED;
  return seed + Hash32(s, len);
}

#endif  // can_use_sse42 || (can_use_neon && can_use_aarch_crypto)
}  // namespace farmhashsa
namespace farmhashcc {
// This file provides a 32-bit hash equivalent to CityHash32 (v1.1.1)
// and a 128-bit hash equivalent to CityHash128 (v1.1.1).  It also provides
// a seeded 32-bit hash function similar to CityHash32.

inline uint32_t Hash32Len13to24(const char* s, size_t len) {
  uint32_t a = Fetch32(s - 4 + (len >> 1));
  uint32_t b = Fetch32(s + 4);
  uint32_t c = Fetch32(s + len - 8);
  uint32_t d = Fetch32(s + (len >> 1));
  uint32_t e = Fetch32(s);
  uint32_t f = Fetch32(s + len - 4);
  uint32_t h = len;

  return fmix(Mur(f, Mur(e, Mur(d, Mur(c, Mur(b, Mur(a, h)))))));
}

inline uint32_t Hash32Len0to4(const char* s, size_t len) {
  uint32_t b = 0;
  uint32_t c = 9;
  for (size_t i = 0; i < len; i++) {
    signed char v = s[i];
    b = b * c1 + v;
    c ^= b;
  }
  return fmix(Mur(b, Mur(len, c)));
}

inline uint32_t Hash32Len5to12(const char* s, size_t len) {
  uint32_t a = len, b = len * 5, c = 9, d = b;
  a += Fetch32(s);
  b += Fetch32(s + len - 4);
  c += Fetch32(s + ((len >> 1) & 4));
  return fmix(Mur(c, Mur(b, Mur(a, d))));
}

inline uint32_t Hash32(const char* s, size_t len) {
  if (len <= 24) {
    return len <= 12
               ? (len <= 4 ? Hash32Len0to4(s, len) : Hash32Len5to12(s, len))
               : Hash32Len13to24(s, len);
  }

  // len > 24
  uint32_t h = len, g = c1 * len, f = g;
  uint32_t a0 = Rotate32(Fetch32(s + len - 4) * c1, 17) * c2;
  uint32_t a1 = Rotate32(Fetch32(s + len - 8) * c1, 17) * c2;
  uint32_t a2 = Rotate32(Fetch32(s + len - 16) * c1, 17) * c2;
  uint32_t a3 = Rotate32(Fetch32(s + len - 12) * c1, 17) * c2;
  uint32_t a4 = Rotate32(Fetch32(s + len - 20) * c1, 17) * c2;
  h ^= a0;
  h = Rotate32(h, 19);
  h = h * 5 + 0xe6546b64;
  h ^= a2;
  h = Rotate32(h, 19);
  h = h * 5 + 0xe6546b64;
  g ^= a1;
  g = Rotate32(g, 19);
  g = g * 5 + 0xe6546b64;
  g ^= a3;
  g = Rotate32(g, 19);
  g = g * 5 + 0xe6546b64;
  f += a4;
  f = Rotate32(f, 19);
  f = f * 5 + 0xe6546b64;
  size_t iters = (len - 1) / 20;
  do {
    uint32_t a0 = Rotate32(Fetch32(s) * c1, 17) * c2;
    uint32_t a1 = Fetch32(s + 4);
    uint32_t a2 = Rotate32(Fetch32(s + 8) * c1, 17) * c2;
    uint32_t a3 = Rotate32(Fetch32(s + 12) * c1, 17) * c2;
    uint32_t a4 = Fetch32(s + 16);
    h ^= a0;
    h = Rotate32(h, 18);
    h = h * 5 + 0xe6546b64;
    f += a1;
    f = Rotate32(f, 19);
    f = f * c1;
    g += a2;
    g = Rotate32(g, 18);
    g = g * 5 + 0xe6546b64;
    h ^= a3 + a1;
    h = Rotate32(h, 19);
    h = h * 5 + 0xe6546b64;
    g ^= a4;
    g = Bswap32(g) * 5;
    h += a4 * 5;
    h = Bswap32(h);
    f += a0;
    PERMUTE3(f, h, g);
    s += 20;
  } while (--iters != 0);
  g = Rotate32(g, 11) * c1;
  g = Rotate32(g, 17) * c1;
  f = Rotate32(f, 11) * c1;
  f = Rotate32(f, 17) * c1;
  h = Rotate32(h + g, 19);
  h = h * 5 + 0xe6546b64;
  h = Rotate32(h, 17) * c1;
  h = Rotate32(h + f, 19);
  h = h * 5 + 0xe6546b64;
  h = Rotate32(h, 17) * c1;
  return h;
}

inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  if (len <= 24) {
    if (len >= 13)
      return farmhashmk::Hash32Len13to24(s, len, seed * c1);
    else if (len >= 5)
      return farmhashmk::Hash32Len5to12(s, len, seed);
    else
      return farmhashmk::Hash32Len0to4(s, len, seed);
  }
  uint32_t h = farmhashmk::Hash32Len13to24(s, 24, seed ^ len);
  return Mur(Hash32(s + 24, len - 24) + seed, h);
}

inline uint64_t ShiftMix(uint64_t val) { return val ^ (val >> 47); }

inline uint64_t HashLen16(uint64_t u, uint64_t v,
                          uint64_t mul = 0x9ddfea08eb382d69ULL) {
  // Murmur-inspired hashing.
  uint64_t a = (u ^ v) * mul;
  a ^= (a >> 47);
  uint64_t b = (v ^ a) * mul;
  b ^= (b >> 47);
  b *= mul;
  return b;
}

inline uint64_t HashLen0to16(const char* s, size_t len) {
  if (len >= 8) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch64(s) + k2;
    uint64_t b = Fetch64(s + len - 8);
    uint64_t c = Rotate64(b, 37) * mul + a;
    uint64_t d = (Rotate64(a, 25) + b) * mul;
    return HashLen16(c, d, mul);
  }
  if (len >= 4) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch32(s);
    return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
  }
  if (len > 0) {
    uint8_t a = s[0];
    uint8_t b = s[len >> 1];
    uint8_t c = s[len - 1];
    uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
    uint32_t z = len + (static_cast<uint32_t>(c) << 2);
    return ShiftMix(y * k2 ^ z * k0) * k2;
  }
  return k2;
}

// Return a 16-byte hash for 48 bytes.  Quick and dirty.
// Callers do best to use "random-looking" values for a and b.
inline uint128_t WeakHashLen32WithSeeds(uint64_t w, uint64_t x, uint64_t y,
                                        uint64_t z, uint64_t a, uint64_t b) {
  a += w;
  b = Rotate64(b + a + z, 21);
  uint64_t c = a;
  a += x;
  a += y;
  b += Rotate64(a, 44);
  return Uint128(a + z, b + c);
}

// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
inline uint128_t WeakHashLen32WithSeeds(const char* s, uint64_t a, uint64_t b) {
  return WeakHashLen32WithSeeds(Fetch64(s), Fetch64(s + 8), Fetch64(s + 16),
                                Fetch64(s + 24), a, b);
}

// A subroutine for CityHash128().  Returns a decent 128-bit hash for strings
// of any length representable in signed long.  Based on City and Murmur.
inline uint128_t CityMurmur(const char* s, size_t len, uint128_t seed) {
  uint64_t a = Uint128Low64(seed);
  uint64_t b = Uint128High64(seed);
  uint64_t c = 0;
  uint64_t d = 0;
  signed long l = len - 16;
  if (l <= 0) {  // len <= 16
    a = ShiftMix(a * k1) * k1;
    c = b * k1 + HashLen0to16(s, len);
    d = ShiftMix(a + (len >= 8 ? Fetch64(s) : c));
  } else {  // len > 16
    c = HashLen16(Fetch64(s + len - 8) + k1, a);
    d = HashLen16(b + len, c + Fetch64(s + len - 16));
    a += d;
    do {
      a ^= ShiftMix(Fetch64(s) * k1) * k1;
      a *= k1;
      b ^= a;
      c ^= ShiftMix(Fetch64(s + 8) * k1) * k1;
      c *= k1;
      d ^= c;
      s += 16;
      l -= 16;
    } while (l > 0);
  }
  a = HashLen16(a, c);
  b = HashLen16(d, b);
  return uint128_t{a ^ b, HashLen16(b, a)};
}

inline uint128_t CityHash128WithSeed(const char* s, size_t len,
                                     uint128_t seed) {
  if (len < 128) {
    return CityMurmur(s, len, seed);
  }

  // We expect len >= 128 to be the common case.  Keep 56 bytes of state:
  // v, w, x, y, and z.
  uint128_t v, w;
  uint64_t x = Uint128Low64(seed);
  uint64_t y = Uint128High64(seed);
  uint64_t z = len * k1;
  v.low = Rotate64(y ^ k1, 49) * k1 + Fetch64(s);
  v.high = Rotate64(v.low, 42) * k1 + Fetch64(s + 8);
  w.low = Rotate64(y + z, 35) * k1 + x;
  w.high = Rotate64(x + Fetch64(s + 88), 53) * k1;

  // This is the same inner loop as CityHash64(), manually unrolled.
  do {
    x = Rotate64(x + y + v.low + Fetch64(s + 8), 37) * k1;
    y = Rotate64(y + v.high + Fetch64(s + 48), 42) * k1;
    x ^= w.high;
    y += v.low + Fetch64(s + 40);
    z = Rotate64(z + w.low, 33) * k1;
    v = WeakHashLen32WithSeeds(s, v.high * k1, x + w.low);
    w = WeakHashLen32WithSeeds(s + 32, z + w.high, y + Fetch64(s + 16));
    swap(z, x);
    s += 64;
    x = Rotate64(x + y + v.low + Fetch64(s + 8), 37) * k1;
    y = Rotate64(y + v.high + Fetch64(s + 48), 42) * k1;
    x ^= w.high;
    y += v.low + Fetch64(s + 40);
    z = Rotate64(z + w.low, 33) * k1;
    v = WeakHashLen32WithSeeds(s, v.high * k1, x + w.low);
    w = WeakHashLen32WithSeeds(s + 32, z + w.high, y + Fetch64(s + 16));
    swap(z, x);
    s += 64;
    len -= 128;
  } while (LIKELY(len >= 128));
  x += Rotate64(v.low + z, 49) * k0;
  y = y * k0 + Rotate64(w.high, 37);
  z = z * k0 + Rotate64(w.low, 27);
  w.low *= 9;
  v.low *= k0;
  // If 0 < len < 128, hash up to 4 chunks of 32 bytes each from the end of s.
  for (size_t tail_done = 0; tail_done < len;) {
    tail_done += 32;
    y = Rotate64(x + y, 42) * k0 + v.high;
    w.low += Fetch64(s + len - tail_done + 16);
    x = x * k0 + w.low;
    z += w.high + Fetch64(s + len - tail_done);
    w.high += v.low;
    v = WeakHashLen32WithSeeds(s + len - tail_done, v.low + z, v.high);
    v.low *= k0;
  }
  // At this point our 56 bytes of state should contain more than
  // enough information for a strong 128-bit hash.  We use two
  // different 56-byte-to-8-byte hashes to get a 16-byte final result.
  x = HashLen16(x, v.low);
  y = HashLen16(y + z, w.low);
  return uint128_t{HashLen16(x + v.high, w.high) + y,
                   HashLen16(x + w.high, y + v.high)};
}

inline uint128_t CityHash128(const char* s, size_t len) {
  return len >= 16
             ? CityHash128WithSeed(s + 16, len - 16,
                                   uint128_t{Fetch64(s), Fetch64(s + 8) + k0})
             : CityHash128WithSeed(s, len, uint128_t{k0, k1});
}

inline uint128_t Fingerprint128(const char* s, size_t len) {
  return CityHash128(s, len);
}
}  // namespace farmhashcc

// BASIC STRING HASHING

// Hash function for a byte array.  See also Hash(), below.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint32_t Hash32(const char* s, size_t len) {
  const bool use_sse41 = can_use_sse41 & FARMHASH_x86_64;
  const bool use_neon = can_use_neon & can_use_aarch_crypto & FARMHASH_aarch64;
  return DebugTweak((use_sse41 | use_neon) ? farmhashnt::Hash32(s, len)
                    : (can_use_sse42 & can_use_aesni)
                        ? farmhashsu::Hash32(s, len)
                    : can_use_sse42 ? farmhashsa::Hash32(s, len)
                                    : farmhashmk::Hash32(s, len));
}

// Hash function for a byte array.  For convenience, a 32-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint32_t Hash32WithSeed(const char* s, size_t len, uint32_t seed) {
  const bool use_sse41 = can_use_sse41 & FARMHASH_x86_64;
  const bool use_neon = can_use_neon & can_use_aarch_crypto & FARMHASH_aarch64;
  return DebugTweak((use_sse41 | use_neon)
                        ? farmhashnt::Hash32WithSeed(s, len, seed)
                    : (can_use_sse42 & can_use_aesni)
                        ? farmhashsu::Hash32WithSeed(s, len, seed)
                    : can_use_sse42 ? farmhashsa::Hash32WithSeed(s, len, seed)
                                    : farmhashmk::Hash32WithSeed(s, len, seed));
}

// Hash function for a byte array.  For convenience, a 64-bit seed is also
// hashed into the result.  See also Hash(), below.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint64_t Hash64(const char* s, size_t len) {
  const bool use_sse42 = can_use_sse42 & FARMHASH_x86_64;
  const bool use_neon = can_use_neon & can_use_aarch_crypto & FARMHASH_aarch64;
  return DebugTweak((use_sse42 | use_neon) ? farmhashte::Hash64(s, len)
                                           : farmhashxo::Hash64(s, len));
}

// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline size_t Hash(const char* s, size_t len) {
  return sizeof(size_t) == 8 ? Hash64(s, len) : Hash32(s, len);
}

// Hash function for a byte array.  For convenience, a 64-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
  if ((can_use_sse41 & FARMHASH_x86_64) |
      (can_use_neon & can_use_aarch_crypto & FARMHASH_aarch64)) {
    if (len <= 64) {
      return DebugTweak(
          farmhashna::HashLen16(farmhashxo::Hash64(s, len) + seed, seed * 9));
    } else {
      return DebugTweak(::farmhashte::Fingerprint64WithSeed(s, len, seed));
    }
  } else {
    return DebugTweak(farmhashna::Hash64WithSeed(s, len, seed));
  }
}

// Hash function for a byte array.  For convenience, two seeds are also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1) {
  if ((can_use_sse41 & FARMHASH_x86_64) |
      (can_use_neon & can_use_aarch_crypto & FARMHASH_aarch64)) {
    if (len <= 64) {
      return DebugTweak(
          farmhashna::HashLen16(farmhashxo::Hash64(s, len) + seed1, seed0));
    } else {
      return DebugTweak(
          ::farmhashte::Fingerprint64WithSeeds(s, len, seed0, seed1));
    }
  } else {
    return DebugTweak(farmhashna::Hash64WithSeeds(s, len, seed0, seed1));
  }
}

// Hash function for a byte array.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint128_t Hash128(const char* s, size_t len) {
  return DebugTweak(farmhashcc::Fingerprint128(s, len));
}

// Hash function for a byte array.  For convenience, a 128-bit seed is also
// hashed into the result.
// May change from time to time, may differ on different platforms, may differ
// depending on NDEBUG.
inline uint128_t Hash128WithSeed(const char* s, size_t len, uint128_t seed) {
  return DebugTweak(farmhashcc::CityHash128WithSeed(s, len, seed));
}

// Older and still available but perhaps not as fast as the above:
//   farmhashns::Hash32{,WithSeed}()

}  // namespace NAMESPACE_FOR_HASH_FUNCTIONS

#if defined(_MSC_VER)
#pragma warning(pop)
#endif  // defined(_MSC_VER)

#if defined(__clang__)
#pragma clang diagnostic pop
#endif  // defined(__clang)

// Cleanup any macros that we might have defined.
#ifdef UNDEF_NAMESPACE_FOR_HASH_FUNCTIONS
#undef NAMESPACE_FOR_HASH_FUNCTIONS
#undef UNDEF_NAMESPACE_FOR_HASH_FUNCTIONS
#endif  // UNDEF_NAMESPACE_FOR_HASH_FUNCTIONS

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_FARMHASH_H_
