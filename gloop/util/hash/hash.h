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

//
// Copyright (C) 1999 and onwards Google, Inc.
//

//
// This file contains routines for hashing and fingerprinting.
//
// A hash function takes an arbitrary input bitstring (string, char*,
// number) and turns it into a hash value (a fixed-size number) such
// that unequal input values have a high likelihood of generating
// unequal hash values.  A fingerprint is a hash whose design is
// biased towards avoiding hash collisions, possibly at the expense of
// other characteristics such as execution speed.
//
// In general, if you are only using the hash values inside a single
// executable -- you're not writing the values to disk, and you don't
// depend on another instance of your program, running on another
// machine, generating the same hash values as you -- you want to use
// a HASH.  Otherwise, you want to use a FINGERPRINT.
//
// RECOMMENDED HASH FOR ALL TYPES: <link>
//
// RECOMMENDED FINGERPRINT:
//
// For string inputs, especially if 256 bytes or longer, use
// highwayhash. Please define a unique seed/key for your usage
// (example: one of the numbers can be a CL number).
//
// Before selecting alternatives including Fingerprint2011, please take note of
// known issues in <link>.
//
// For integer input, Fingerprint is still recommended though collisions are
// possible.
//
// OTHER HASHES AND FINGERPRINTS:
//
// See <link>
//
// The wiki page also has good advice for when to use a fingerprint vs
// a hash.
//
//
// Note: The preferred hash tables (`absl::*_hash_map` and `absl::*_hash_set`)
// will select reasonable defaults.
//
// Some of the hash functions below are documented to be fixed
// forever; the rest (whether they're documented as so or not) may
// change over time.  "Change over time" here means that the hash may
// depend on a value that's reseeded at process startup, and thus may
// not be consistent even across multiple runs of the same binary.  If
// you require a hash function that does not change over time, you
// should have unittests enforcing this property.  We already have
// several such functions; see hash_unittest.cc for the details and
// unittests.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_HASH_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_HASH_H_

#include <stddef.h>
#include <stdint.h>  // for uintptr_t
#include <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#include <ext/hash_set>
#pragma clang diagnostic pop

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/builtin_type_hash.h"
#include "gloop/util/hash/hash128to64.h"
#include "gloop/util/hash/jenkins.h"
#include "gloop/util/hash/jenkins_lookup2.h"
#include "gloop/util/hash/string_hash.h"

#define HASH_NAMESPACE_DECLARATION_START namespace __gnu_cxx {
#define HASH_NAMESPACE_DECLARATION_END }

namespace absl {
ABSL_NAMESPACE_BEGIN
class Cord;
ABSL_NAMESPACE_END
}  // namespace absl

// TODO: This is one of the legacy hashes, kept here only
// temporarily for making it easier to remove the physical dependency
// to legacy_hash.h
inline uint32_t HashTo32(const char* s, size_t slen) {
  uint32_t retval = Hash32StringWithSeed(absl::string_view(s, slen), MIX32);
  return retval == kIllegalHash32 ? static_cast<uint32_t>(retval - 1) : retval;
}

// HashTo32(const absl::Cord& c).
// This returns the same value as HashTo32(std::string(c).data(), c.size()),
// without having to copy or flatten the Cord.
// It is a template to avoid adding a hard dependency on Cord.
template <typename AbslCord, typename = typename std::enable_if<std::is_same<
                                 AbslCord, absl::Cord>::value>::type>
inline uint32_t HashTo32(const AbslCord& c) {
  std::string flat;
  for (absl::string_view chunk : c.Chunks()) {
    flat.append(chunk.data(), chunk.size());
  }
  return HashTo32(flat.data(), flat.size());
}

inline uint64_t Hash64StringWithSeed(absl::string_view s, uint64_t seed) {
  return Hash64StringWithSeed(s.data(), s.size(), seed);
}

inline uint64_t Hash64StringWithSeed(const char* s, uint64_t seed) {
  return Hash64StringWithSeed(absl::string_view(s), seed);
}

namespace util_hash {
// A few hash specializations just perform static_cast<size_t>.
template <typename T>
struct TrivialHashBase {
  size_t operator()(const T& x) const {
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    return (reinterpret_cast<size_t>(&::std::nothrow) >> 12) +
           static_cast<size_t>(x);
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    return 0x346C7D9D + static_cast<size_t>(x);
#else
    return static_cast<size_t>(x);
#endif
  }
};
}  // namespace util_hash

HASH_NAMESPACE_DECLARATION_START

// The MSVC STL provides the following specializations of
// HASH_NAMESPACE::hash itself; we cannot redefine them.
#ifndef _MSVC_STL_VERSION
template <>
struct hash<long long>                           // NOLINT(runtime/int)
    : util_hash::TrivialHashBase<long long> {};  // NOLINT(runtime/int)
template <>
struct hash<unsigned long long>                           // NOLINT(runtime/int)
    : util_hash::TrivialHashBase<unsigned long long> {};  // NOLINT(runtime/int)
template <>
struct hash<bool> : util_hash::TrivialHashBase<bool> {};

// This intended to be a "good" hash function.  It may change from time to time.
// TODO: If the need arises to use hash_map with unit128 in apps that
// build for Windows, then define the equivalent as stdext::hash_compare
template <>
struct hash<absl::uint128> {
  size_t operator()(const absl::uint128 x) const {
    if (sizeof(&x) == 8) {  // 64-bit systems have 8-byte pointers.
      return static_cast<size_t>(Hash128to64(x));
    } else {
      uint32_t a = static_cast<uint32_t>(absl::Uint128Low64(x)) +
                   static_cast<uint32_t>(0x9e3779b9UL);
      uint32_t b = static_cast<uint32_t>(absl::Uint128Low64(x) >> 32) +
                   static_cast<uint32_t>(0x9e3779b9UL);
      uint32_t c = static_cast<uint32_t>(absl::Uint128High64(x)) + MIX32;
      mix(a, b, c);
      a += static_cast<uint32_t>(absl::Uint128High64(x) >> 32);
      mix(a, b, c);
      return c;
    }
  }
};

// Hash pointers as integers, but bring more entropy to the lower bits.
template <typename T>
struct hash<T*> {
  size_t operator()(T* x) const {
    size_t k = static_cast<size_t>(reinterpret_cast<uintptr_t>(x));
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    k += reinterpret_cast<size_t>(&::std::nothrow) >> 12;
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    k += 0xC664CD26;
#endif
    return k + (k >> 6);
  }
};

// Use our nice hash function for strings
template <typename CharT, typename Traits, typename Alloc>
struct hash<std::basic_string<CharT, Traits, Alloc>> {
  size_t operator()(const std::basic_string<CharT, Traits, Alloc>& k) const {
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    return (reinterpret_cast<size_t>(&::std::nothrow) >> 12) +
           Hash32StringWithSeed(k.data(), k.size(), MIX32);
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    return 0xDF7B5A63 + Hash32StringWithSeed(k.data(), k.size(), MIX32);
#else
    return Hash32StringWithSeed(k.data(), k.size(), MIX32);
#endif
  }
};

// Hasher for STL pairs. Requires hashers for both members to be defined.
// Prefer <link>, particularly if speed is important.
template <typename First, typename Second>
struct hash<std::pair<First, Second>> {
  size_t operator()(const std::pair<First, Second>& p) const {
    size_t h1 = HashPart(p.first);
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    h1 += reinterpret_cast<size_t>(&::std::nothrow) >> 12;
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    h1 += 0x5A756F67;
#endif
    size_t h2 = HashPart(p.second);
    // The decision below is at compile time
    return (sizeof(h1) <= sizeof(uint32_t)) ? Hash32NumWithSeed(h1, h2)
                                            : Hash64NumWithSeed(h1, h2);
  }

 private:
  template <typename T>
  static size_t HashPart(const T& x) {
    return hash<T>()(x);
  }
};
#endif  // !defined(_MSVC_STL_VERSION)

HASH_NAMESPACE_DECLARATION_END

namespace util_hash {
namespace internal {

// Uses GoodFastHash<T> if available, otherwise HASH_NAMESPACE::hash<T>.
// This is a *private* internal detail of the util/hash library.
template <typename T>
size_t ChooseHasher(const T& x);

// Implementation for the primary template of GoodFastHash.
// Only supported when `std::is_enum<T>::value == true`.
// Otherwise, it provides no operator() function.
template <typename T, bool is_enum = true>
struct GoodFastHashImpl {
  // Implements hashing of enums by hashing their underlying values.  This has
  // to be in the primary template, because we cannot partially specialize on
  // enum types, and cannot disable a class specialization with SFINAE without
  // adding an extra template parameter, which breaks all forward declarations.
  size_t operator()(const T& v) const {
    return util_hash::internal::ChooseHasher(
        static_cast<typename std::underlying_type<T>::type>(v));
  }
};

template <typename T>
struct GoodFastHashImpl<T, false> {
  // Not supported.
};

#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
// Hashes a sequence of characters in a way that is consistent within a
// process but likely to change between consecutive runs of the same binary.
// Used as a common implementation for GoodFastHash<StringType> to make
// GoodFastHash return the same value whether passed a const char*, a
// string, or a string_view.
inline size_t SaltedHashStringThoroughly(const char* s, size_t n) {
  return (reinterpret_cast<size_t>(&::std::nothrow) >> 12) +
         HashStringThoroughly(s, n);
}
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
inline size_t SaltedHashStringThoroughly(const char* s, size_t n) {
  return 0x37344347 + HashStringThoroughly(s, n);
}
#endif

// Detail: privately inherit from this class to disallow aggregate
// initialization.
struct AggregateBarrier {};

}  // namespace internal
}  // namespace util_hash

// GoodFastHash is deprecated and is being actively removed.  Use <link>.
//
// By the way, when deleting the contents of a hash_set of pointers, it is
// unsafe to delete *iterator because the hash function may be called on
// the next iterator advance.  Use STLDeleteContainerPointers().

template <typename T>
struct ABSL_DEPRECATED("Use absl::Hash instead") GoodFastHash
    : util_hash::internal::GoodFastHashImpl<T, std::is_enum<T>::value> {};

// These hashers are poisoned, to prevent use.  Hashing char* used to hash the
// string contents, meaning GoodFastHash<char*> was incompatible with
// operator==().  Break any code that relies on the old behavior by refusing
// to hash char*.
template <>
struct GoodFastHash<char*> : private util_hash::internal::AggregateBarrier {
  GoodFastHash() = delete;
  GoodFastHash(const GoodFastHash&) = delete;
  GoodFastHash& operator=(const GoodFastHash&) = delete;
};
template <>
struct GoodFastHash<const char*>
    : private util_hash::internal::AggregateBarrier {
  GoodFastHash() = delete;
  GoodFastHash(const GoodFastHash&) = delete;
  GoodFastHash& operator=(const GoodFastHash&) = delete;
};

template <typename CharT, typename Traits, typename Alloc>
struct ABSL_DEPRECATED("Use absl::Hash<std::string> instead")
    GoodFastHash<std::basic_string<CharT, Traits, Alloc>> {
  size_t operator()(const std::basic_string<CharT, Traits, Alloc>& k) const {
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 1)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    static_assert(sizeof(CharT) == 1, "GoodFastHash requires CharT == char");
    return util_hash::internal::SaltedHashStringThoroughly(k.data(), k.size());
#else
    return HashStringThoroughly(k.data(), k.size() * sizeof(k[0]));
#endif
  }
};

template <>
struct ABSL_DEPRECATED("Use absl::Hash<absl::string_view> instead")
    GoodFastHash<absl::string_view> {
  size_t operator()(absl::string_view s) const {
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 1)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    return util_hash::internal::SaltedHashStringThoroughly(s.data(), s.size());
#else
    return HashStringThoroughly(s.data(), s.size());
#endif
  }
};

template <typename T, typename U>
struct ABSL_DEPRECATED("Use absl::Hash<std::pair<T,U>> instead")
    GoodFastHash<std::pair<T, U>> {
  size_t operator()(const std::pair<T, U>& k) const {
    size_t h1 = util_hash::internal::ChooseHasher(k.first);
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(_GOOGLE3_RANDOMIZATION_ROLLOUT) &&          \
      _GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&             \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    h1 += reinterpret_cast<size_t>(&::std::nothrow) >> 12;
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
    h1 += 0x76F2F333;
#endif
    size_t h2 = util_hash::internal::ChooseHasher(k.second);

    // Mix the hashes together.  Multiplicative hashing mixes the high-order
    // bits better than the low-order bits, and rotating moves the high-order
    // bits down to the low end, where they matter more for most hashtable
    // implementations.
    static const size_t kMul = static_cast<size_t>(0xc6a4a7935bd1e995ULL);
    if (std::is_integral<T>::value) {
      // We want to avoid GoodFastHash({x, y}) == 0 for common values of {x, y}.
      // hash<X> is the identity function for integral types X, so without this,
      // GoodFastHash({0, 0}) would be 0.
      h1 += 109;
    }
    h1 = h1 * kMul;
    h1 = (h1 << 21) | (h1 >> (std::numeric_limits<size_t>::digits - 21));
    return h1 + h2;
  }
};

namespace util_hash {
namespace internal {

// Lightly hash two hash codes together. When used repetitively to mix more
// than two values, the new values should be in the first argument.
inline size_t Mix(size_t new_hash, size_t accu) {
#if (defined(__GOOGLE3_RANDOMIZE_UNORDERED_CONTAINERS) || \
     (defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) &&         \
      __GOOGLE3_RANDOMIZATION_ROLLOUT > 3)) &&            \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
  static const size_t kMul =
      (reinterpret_cast<size_t>(&::std::nothrow) >> 12) * 2 +
      static_cast<size_t>(0xc6a4a7935bd1e995ULL);
#elif defined(__GOOGLE3_RANDOMIZATION_ROLLOUT) && \
    __GOOGLE3_RANDOMIZATION_ROLLOUT > 1 &&        \
    !defined(__GOOGLE3_RANDOMIZATION_TEMPORARY_OPTOUT)
  static const size_t kMul = static_cast<size_t>(0xC7463E63C) +
                             static_cast<size_t>(0xc6a4a7935bd1e995ULL);
#else
  static const size_t kMul = static_cast<size_t>(0xc6a4a7935bd1e995ULL);
#endif
  // Multiplicative hashing will mix bits better in the msb end ...
  accu *= kMul;
  // ... and rotating will move the better mixed msb-bits to lsb-bits.
  return ((accu << 21) | (accu >> (std::numeric_limits<size_t>::digits - 21))) +
         new_hash;
}

// Iteratively hash and mix, starting with the end of the tuple.
template <typename Tup, size_t I = 0, size_t N = std::tuple_size<Tup>::value>
struct HashTupleImpl {
  size_t operator()(const Tup& t, size_t seed) const {
    using std::get;
    return Mix(ChooseHasher(get<I>(t)), HashTupleImpl<Tup, I + 1>{}(t, seed));
  }
};
template <typename Tup, size_t N>
struct HashTupleImpl<Tup, N, N> {
  size_t operator()(const Tup&, size_t seed) const { return seed; }
};

template <typename Tup>
size_t HashTuple(const Tup& t, size_t seed) {
  return 0xc6a4a7935bd1e995ULL * HashTupleImpl<Tup>{}(t, seed);
}

}  // namespace internal

// ::util_hash::Hash(a, b, c, ...) hashes a, b, c, and so on (using
// GoodFastHash<>, if it's available for the given type, otherwise using
// hash<>), and then combines the individual hashes.
//
// DEPRECATED: Prefer `absl::HashOf(a, b, c, ...)`.
//
// This is intended to be a pretty good hash function, which may change from
// time to time.  (Its quality mostly depends on the quality of GoodFastHash<>
// and/or hash<>.)
//
// In the somewhat unusual case of nested calls to Hash(), it is best if
// the new values should appear first in the arguments list.  For example:
//
//  size_t Hash(int x, int y, vector<T> v, vector<T> w) {
//    auto combine = [](size_t h, const T& elem) {
//      return util_hash::Hash(elem, h);  // Note that elem is the first arg.
//    };
//    size_t vh =
//        std::accumulate(v.begin(), v.end(), static_cast<size_t>(0), combine);
//    size_t wh =
//        std::accumulate(w.begin(), w.end(), static_cast<size_t>(0), combine);
//    // Note that x and y come before vh and wh.
//    return util_hash::Hash(x, y, vh, wh);
//  }
//
// A stronger (and slower) way to combine multiple hash codes together is to
// use hash<uint128>.  The order of args in hash<uint128> doesn't matter.  For
// example:
//
//  size_t Hash(T x, U y) {
//    return hash<uint128>()(
//        absl::MakeUint128(util_hash::Hash(x), util_hash::Hash(y)));
//  }
ABSL_DEPRECATED("Use absl::HashOf instead")
inline size_t Hash() { return 113; }

template <typename First, typename... T>
ABSL_DEPRECATED("Use absl::HashOf instead")
size_t Hash(const First& f, const T&... t) {
  return internal::Mix(internal::ChooseHasher(f), Hash(t...));
}
}  // namespace util_hash

// Don't define these for the MSVC STL.
#ifndef _MSVC_STL_VERSION
HASH_NAMESPACE_DECLARATION_START

// Hash functions for tuples.  These are intended to be "good" hash functions.
// They may change from time to time.  GoodFastHash<> or hash<> must be defined
// for the tuple elements.
template <typename... T>
struct hash<std::tuple<T...>> {
  size_t operator()(const std::tuple<T...>& t) const {
    return util_hash::internal::HashTuple(t, 113);
  }
};

// Hash functions for std::array.  These are intended to be "good" hash
// functions.  They may change from time to time.  GoodFastHash<> or hash<> must
// be defined for T.
template <typename T, std::size_t N>
struct hash<std::array<T, N>> {
  size_t operator()(const std::array<T, N>& t) const {
    return util_hash::internal::HashTuple(t, 71);
  }
};

HASH_NAMESPACE_DECLARATION_END
#endif  // !defined(_MSVC_STL_VERSION)

namespace util_hash {
namespace internal {

// Inheritance hierarchy of tag types to break ties in overload resolution
// below.  Lower rank number indicates abetter match.
struct Rank2 {};
struct Rank1 : Rank2 {};
struct Rank0 : Rank1 {};

// Best match: dispatch to GoodFastHash<T> if it exists.
template <typename T, decltype(GoodFastHash<T>()(std::declval<T>())) = 0>
size_t ChooseHasherImpl(const T& v, const Rank0&) {
  return GoodFastHash<T>()(v);
}

// Second best match: if T == absl::Cord, dispatch to HashTo32().  (This hash
// function is used for compatibility with legacy code.)
template <typename T, std::enable_if_t<std::is_same_v<T, absl::Cord>, int> = 0>
size_t ChooseHasherImpl(const T& c, const Rank1&) {
  return HashTo32(c);
}

// Third best match: dispatch to __gnu_cxx::hash<T> as a last resort.
template <typename T>
size_t ChooseHasherImpl(const T& v, const Rank2&) {
  return __gnu_cxx::hash<T>()(v);  // NOLINT
}

template <typename T>
size_t ChooseHasher(const T& x) {
  return ChooseHasherImpl<T>(x, Rank0{});
}

}  // namespace internal
}  // namespace util_hash

// ----------------------------------------------------------------------
// Fingerprint()
//   When used for string input, this is not recommended for new code.
//   Instead, use Fingerprint2011(), a higher-quality and faster hash function.
//   (See fingerprint2011.h.) The functions below that take integer input are
//   still recommended.
//
//   Fingerprinting a string (or char*) will never return 0 or 1,
//   in case you want a couple of special values.  However,
//   fingerprinting a numeric type may produce 0 or 1.
// ----------------------------------------------------------------------
uint64_t FingerprintReferenceImplementation(const char* s, size_t len);
uint64_t FingerprintInterleavedImplementation(const char* s, size_t len);
inline uint64_t Fingerprint(const char* s, size_t len) {
  if (sizeof(s) == 8) {  // 64-bit systems have 8-byte pointers.
    // The better choice when we have a decent number of registers.
    return FingerprintInterleavedImplementation(s, len);
  } else {
    return FingerprintReferenceImplementation(s, len);
  }
}

// Routine that combines together the hi/lo part of a fingerprint
// and changes the result appropriately to avoid returning 0/1.
inline uint64_t CombineFingerprintHalves(uint64_t hi, uint32_t lo) {
  uint64_t result = (hi << 32) | lo;
  // (result >> 1) is here the same as (result > 1), but slightly faster.
  if (ABSL_PREDICT_TRUE(result >> 1)) {
    return result;  // Not 0 or 1, return as is.
  }
  return result ^ uint64_t{0x130f9bef94a0a928};
}

inline uint64_t Fingerprint(absl::string_view s) {
  return Fingerprint(s.data(), s.size());
}

// Fingerprint(const absl::Cord& c).
// This returns the same value as Fingerprint(std::string(c)), without having
// to copy or flatten the Cord.
// It is a template to avoid adding a hard dependency on Cord.
template <typename AbslCord, typename = typename std::enable_if<std::is_same<
                                 AbslCord, absl::Cord>::value>::type>
inline uint64_t Fingerprint(const AbslCord& c) {
  std::string flat;
  for (absl::string_view chunk : c.Chunks()) {
    flat.append(chunk.data(), chunk.size());
  }
  return Fingerprint(flat.data(), flat.size());
}

inline uint64_t Fingerprint(char c) {
  return Hash64NumWithSeed(static_cast<uint64_t>(static_cast<unsigned char>(c)),
                           MIX64);
}
inline uint64_t Fingerprint(signed char c) {
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(unsigned char c) {
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(short c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(unsigned short c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(int c) {
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(unsigned int c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(long c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(unsigned long c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(long long c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}
inline uint64_t Fingerprint(unsigned long long c) {  // NOLINT(runtime/int)
  return Hash64NumWithSeed(static_cast<uint64_t>(c), MIX64);
}

// This concatenates two 64-bit fingerprints. It is a convenience function to
// get a fingerprint for a combination of already fingerprinted components.
// It assumes that each input is already a good fingerprint itself.
// Note that this is legacy code and new code should use its replacement
// FingerprintCat2011().
//
// Note that in general it's impossible to construct Fingerprint(str)
// from the fingerprints of substrings of str.  One shouldn't expect
// FingerprintCat(Fingerprint(x), Fingerprint(y)) to indicate
// anything about Fingerprint(StrCat(x, y)).
inline uint64_t FingerprintCat(uint64_t fp1, uint64_t fp2) {
  return Hash64NumWithSeed(fp1, fp2);
}

// Extern template declarations.
//
// gcc only for now.  msvc and others: this technique is likely to work with
// your compiler too.  changelists welcome.
//
// This technique is limited to template specializations whose hash key
// functions are declared in this file.

#if defined(__GNUC__)
HASH_NAMESPACE_DECLARATION_START
extern template class __gnu_cxx::hash_set<std::string>;
extern template class __gnu_cxx::hash_map<std::string, std::string>;
HASH_NAMESPACE_DECLARATION_END
#endif  // defined(__GNUC__)

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_HASH_H_
