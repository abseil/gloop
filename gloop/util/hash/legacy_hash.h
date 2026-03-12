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

// Copyright 2011 Google Inc. All Rights Reserved.

//
// This is a library of legacy hashing routines. These routines are still in
// use, but are not encouraged for any new code, and may be removed at some
// point in the future.
//
// New code should use one of the targeted libraries that provide hash
// interfaces for the types needed. See the package README for details.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_LEGACY_HASH_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_LEGACY_HASH_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "gloop/util/hash/builtin_type_hash.h"
#include "gloop/util/hash/hash.h"  // HashTo32 is partially there temporarily
#include "gloop/util/hash/jenkins.h"

// Hash8, Hash16 and Hash32 are for legacy use only.
typedef uint32_t Hash32;
typedef uint16_t Hash16;
typedef uint8_t Hash8;

// ----------------------------------------------------------------------
// HashTo32()
// HashTo16()
//    These functions take various types of input (through operator
//    overloading) and return 32 or 16 bit quantities, respectively.
//    The basic rule of our hashing is: always mix().  Thus, even for
//    char outputs we cast to a uint32 and mix with two arbitrary numbers.
//    HashTo32 never returns kIllegalHash32, and similary,
//    HashTo16 never returns kIllegalHash16.
//
// Note that these methods avoid returning certain reserved values, while
// the corresponding HashXXStringWithSeed() methods may return any value.
// ----------------------------------------------------------------------

// This macro defines the HashTo32 and HashTo16 versions all in one go.
// It takes the argument list and a command that hashes your number.
// (For 16 we just mod retval before returning it.)  Example:
//    HASH_TO((char c), Hash32NumWithSeed(c, MIX32_1))
// evaluates to
//    uint32 retval;
//    retval = Hash32NumWithSeed(c, MIX32_1);
//    return retval == kIllegalHash32 ? retval-1 : retval;
//

#define HASH_TO(arglist, command)                          \
  inline uint32_t HashTo32 arglist {                       \
    uint32_t retval = command;                             \
    return retval == kIllegalHash32 ? retval - 1 : retval; \
  }

// This defines:
// HashToXX(char *s, int slen);
// HashToXX(char c);
// etc

namespace hash_internal {
template <typename IntType>
uint32_t HashTo32Num(IntType num) {
  static_assert(sizeof(IntType) <= sizeof(uint64_t),
                "IntType must be at most 64-bit");
  // This decision happens at compile time.
  if (sizeof(IntType) <= sizeof(uint32_t)) {
    return Hash32NumWithSeed(static_cast<uint32_t>(num), MIX32);
  } else {
    return static_cast<uint32_t>(
        Hash64NumWithSeed(static_cast<uint64_t>(num), MIX64) >> 32);
  }
}
}  // namespace hash_internal

HASH_TO((const wchar_t* s, uint32_t slen),
        Hash32StringWithSeed(
            absl::string_view(reinterpret_cast<const char*>(s),
                              static_cast<uint32_t>(sizeof(wchar_t) * slen)),
            MIX32))
HASH_TO((char c), hash_internal::HashTo32Num(c))
HASH_TO((signed char c), hash_internal::HashTo32Num(c))
HASH_TO((unsigned char c), hash_internal::HashTo32Num(c))
HASH_TO((short c), hash_internal::HashTo32Num(c))           // NOLINT
HASH_TO((unsigned short c), hash_internal::HashTo32Num(c))  // NOLINT
HASH_TO((int c), hash_internal::HashTo32Num(c))
HASH_TO((unsigned int c), hash_internal::HashTo32Num(c))        // NOLINT
HASH_TO((long c), hash_internal::HashTo32Num(c))                // NOLINT
HASH_TO((unsigned long c), hash_internal::HashTo32Num(c))       // NOLINT
HASH_TO((long long c), hash_internal::HashTo32Num(c))           // NOLINT
HASH_TO((unsigned long long c), hash_internal::HashTo32Num(c))  // NOLINT

#undef HASH_TO  // clean up the macro space

inline uint16_t HashTo16(const char* s, uint32_t slen) {
  uint16_t retval =
      Hash32StringWithSeed(absl::string_view(s, slen), MIX32) >> 16;
  return retval == kIllegalHash16 ? static_cast<uint16_t>(retval - 1) : retval;
}

template <typename T>
struct LegacyHash {};

// An actual implementation is needed for a few other types to support code
// like maps/util/expmapvar.h, which needs LegacyHash for template use.
template <>
struct LegacyHash<int> {
  size_t operator()(int v) const { return std::hash<int>()(v); }
};

template <>
struct LegacyHash<int64_t> {
  size_t operator()(int64_t v) const { return std::hash<int64_t>()(v); }
};

template <>
struct LegacyHash<uint64_t> {
  size_t operator()(uint64_t v) const { return std::hash<uint64_t>()(v); }
};

template <class First, class Second>
struct LegacyHash<std::pair<First, Second> > {
  size_t operator()(const std::pair<First, Second>& p) const {
    size_t h1 = LegacyHash<First>()(p.first);
    size_t h2 = LegacyHash<Second>()(p.second);
    // The decision below is at compile time
    return (sizeof(h1) <= sizeof(uint32_t)) ? Hash32NumWithSeed(h1, h2)
                                            : Hash64NumWithSeed(h1, h2);
  }
  // Less than operator for MSVC.
  bool operator()(const std::pair<First, Second>& a,
                  const std::pair<First, Second>& b) const {
    return a < b;
  }
  static constexpr size_t bucket_size = 4;  // These are required by MSVC
  static constexpr size_t min_buckets = 8;  // 4 and 8 are defaults.
};

#if defined(__GNUC__)
// A slow old hash function for strings
template <class _CharT, class _Traits, class _Alloc>
struct LegacyHash<std::basic_string<_CharT, _Traits, _Alloc> > {
  size_t operator()(const std::basic_string<_CharT, _Traits, _Alloc>& k) const {
    return HashTo32(k.data(), static_cast<uint32_t>(k.length()));
  }
};

// A slow old hash function for const string
template <>
struct LegacyHash<const std::string> {
  size_t operator()(const std::string& k) const {
    return HashTo32(k.data(), static_cast<uint32_t>(k.length()));
  }
};
#endif  // defined(__GNUC__)

// MSVC's STL requires an ever-so slightly different decl
#if defined(STL_MSVC)
template <>
struct LegacyHash<char const*> {
  size_t operator()(char const* const k) const {
    // A slow old hash function for const strings
    return HashTo32(k, strlen(k));
  }
  // Less than operator:
  bool operator()(char const* const a, char const* const b) const {
    return strcmp(a, b) < 0;
  }
  static const size_t bucket_size = 4;  // These are required by MSVC
  static const size_t min_buckets = 8;  // 4 and 8 are defaults.
};

#if !defined(_MSC_VER) || _MSC_VER < 1600
template <>
struct LegacyHash<std::string> {
  size_t operator()(const std::string& k) const {
    // A slow old hash function for const strings
    return HashTo32(k.data(), k.length());
  }
  // Less than operator:
  bool operator()(const std::string& a, const std::string& b) const {
    return a < b;
  }
  static const size_t bucket_size = 4;  // These are required by MSVC
  static const size_t min_buckets = 8;  // 4 and 8 are defaults.
};
#endif  // !defined(_MSC_VER) || _MSC_VER < 1600

#endif  // defined(STL_MSVC)

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_LEGACY_HASH_H_
