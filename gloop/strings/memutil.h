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

// These routines provide mem versions of standard C string routines,
// such as strpbrk.  They function exactly the same as the str versions,
// so if you wonder what they are, replace the word "mem" by
// "str" and check out the man page.  I could return void*, as the
// strutil.h mem*() routines tend to do, but I return char* instead
// since this is by far the most common way these functions are called.
//
// The difference between the mem and str versions is the mem version
// takes a pointer and a length, rather than a '\0'-terminated string.
// The memcase* routines defined here assume the locale is "C"
// (they use ascii_tolower instead of tolower).
//
// These routines are based on the BSD library.
//
// Here's a list of routines from string.h, and their mem analogues.
// Functions in lowercase are defined in string.h; those in UPPERCASE
// are defined here:
//
// strlen                  --
// strcat strncat          MEMCAT
// strcpy strncpy          memcpy
// --                      memccpy   (very cool function, btw)
// --                      memmove
// --                      memset
// strcmp strncmp          memcmp
// strcasecmp strncasecmp  MEMCASECMP
// strchr                  memchr
// strcoll                 --
// strxfrm                 --
// strdup strndup          MEMDUP
// strrchr                 MEMRCHR
// strspn                  MEMSPN
// strcspn                 MEMCSPN
// strpbrk                 MEMPBRK
// strstr                  MEMSTR MEMMEM
// (g)strcasestr           MEMCASESTR MEMCASEMEM
// strtok                  --
// strprefix               MEMPREFIX      (strprefix is from strutil.h)
// strcaseprefix           MEMCASEPREFIX  (strcaseprefix is from strutil.h)
// strsuffix               MEMSUFFIX      (strsuffix is from strutil.h)
// strcasesuffix           MEMCASESUFFIX  (strcasesuffix is from strutil.h)
// --                      MEMIS
// --                      MEMCASEIS
// strcount                MEMCOUNT       (strcount is from strutil.h)

#ifndef THIRD_PARTY_GLOOP_STRINGS_MEMUTIL_H_
#define THIRD_PARTY_GLOOP_STRINGS_MEMUTIL_H_

#include <stddef.h>
#include <string.h>  // to get the POSIX mem*() routines

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"

namespace strings {

int memcasecmp(const char* s1, const char* s2, size_t len);

char* memdup(absl::string_view s);
ABSL_DEPRECATE_AND_INLINE() inline char* memdup(const char* s, size_t slen) {
  return memdup(absl::string_view(s, slen));
}

const char* memrchr(absl::string_view s, int c);
ABSL_DEPRECATE_AND_INLINE()
inline char* memrchr(const char* s, int c, size_t slen) {
  return const_cast<char*>(memrchr(absl::string_view(s, slen), c));
}

size_t memspn(absl::string_view s, absl::string_view accept);
ABSL_DEPRECATE_AND_INLINE()
inline size_t memspn(const char* s, size_t slen, const char* accept) {
  return memspn(absl::string_view(s, slen), accept);
}

size_t memcspn(absl::string_view s, absl::string_view reject);
ABSL_DEPRECATE_AND_INLINE()
inline size_t memcspn(const char* s, size_t slen, const char* reject) {
  return memcspn(absl::string_view(s, slen), reject);
}

const char* mempbrk(absl::string_view s, absl::string_view accept);
ABSL_DEPRECATE_AND_INLINE()
inline char* mempbrk(const char* s, size_t slen, const char* accept) {
  return const_cast<char*>(mempbrk(absl::string_view(s, slen), accept));
}

// This is for internal use only.  Don't call this directly
template <bool case_sensitive>
const char* int_memmatch(const char* phaystack, size_t haylen,
                         const char* pneedle, size_t neelen);

// explicit template instantiations
extern template const char* int_memmatch<true>(const char* phaystack,
                                               size_t haylen,
                                               const char* pneedle,
                                               size_t neelen);
extern template const char* int_memmatch<false>(const char* phaystack,
                                                size_t haylen,
                                                const char* pneedle,
                                                size_t neelen);

// These are the guys you can call directly
inline const char* memmem(absl::string_view haystack,
                          absl::string_view needle) {
  return int_memmatch<true>(haystack.data(), haystack.size(), needle.data(),
                            needle.size());
}

inline const char* memcasemem(absl::string_view haystack,
                              absl::string_view needle) {
  return int_memmatch<false>(haystack.data(), haystack.size(), needle.data(),
                             needle.size());
}

inline const char* memstr(absl::string_view haystack,
                          absl::string_view needle) {
  return memmem(haystack, needle);
}

inline const char* memcasestr(absl::string_view haystack,
                              absl::string_view needle) {
  return memcasemem(haystack, needle);
}

const char* memmatch(absl::string_view haystack, absl::string_view needle);

ABSL_DEPRECATE_AND_INLINE()
inline const char* memstr(const char* phaystack, size_t haylen,
                          const char* pneedle) {
  return memstr(absl::string_view(phaystack, haylen), pneedle);
}

ABSL_DEPRECATE_AND_INLINE()
inline const char* memcasestr(const char* phaystack, size_t haylen,
                              const char* pneedle) {
  return memcasestr(absl::string_view(phaystack, haylen), pneedle);
}

ABSL_DEPRECATE_AND_INLINE()
inline const char* memmem(const char* phaystack, size_t haylen,
                          const char* pneedle, size_t needlelen) {
  return memmem(absl::string_view(phaystack, haylen),
                absl::string_view(pneedle, needlelen));
}

ABSL_DEPRECATE_AND_INLINE()
inline const char* memcasemem(const char* phaystack, size_t haylen,
                              const char* pneedle, size_t needlelen) {
  return memcasemem(absl::string_view(phaystack, haylen),
                    absl::string_view(pneedle, needlelen));
}

// This is significantly faster for case-sensitive matches with very
// few possible matches.  See unit test for benchmarks.
ABSL_DEPRECATE_AND_INLINE()
inline const char* memmatch(const char* phaystack, size_t haylen,
                            const char* pneedle, size_t neelen) {
  return memmatch(absl::string_view(phaystack, haylen),
                  absl::string_view(pneedle, neelen));
}

// The ""'s catch people who don't pass in a literal for "str"
#define strliterallen(str) (sizeof("" str "") - 1)

// Must use a string literal for prefix.
#define memprefix(str, len, prefix)                  \
  ((((len) >= strliterallen(prefix)) &&              \
    memcmp(str, prefix, strliterallen(prefix)) == 0) \
       ? (str) + strliterallen(prefix)               \
       : nullptr)

#define memcaseprefix(str, len, prefix)                             \
  ((((len) >= strliterallen(prefix)) &&                             \
    ::strings::memcasecmp(str, prefix, strliterallen(prefix)) == 0) \
       ? (str) + strliterallen(prefix)                              \
       : nullptr)

// Must use a string literal for suffix.
#define memsuffix(str, len, suffix)                       \
  ((((len) >= strliterallen(suffix)) &&                   \
    memcmp((str) + (len) - strliterallen(suffix), suffix, \
           strliterallen(suffix)) == 0)                   \
       ? (str) + (len) - strliterallen(suffix)            \
       : nullptr)

#define memcasesuffix(str, len, suffix)                                  \
  ((((len) >= strliterallen(suffix)) &&                                  \
    ::strings::memcasecmp((str) + (len) - strliterallen(suffix), suffix, \
                          strliterallen(suffix)) == 0)                   \
       ? (str) + (len) - strliterallen(suffix)                           \
       : nullptr)

#define memis(str, len, literal)         \
  ((((len) == strliterallen(literal)) && \
    memcmp(str, literal, strliterallen(literal)) == 0))

#define memcaseis(str, len, literal)     \
  ((((len) == strliterallen(literal)) && \
    ::strings::memcasecmp(str, literal, strliterallen(literal)) == 0))

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_MEMUTIL_H_
