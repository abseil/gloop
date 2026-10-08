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

#include "gloop/strings/memutil.h"

#include <stdlib.h>  // for malloc

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace strings {

// We inject some noise into the return value of `memcasecmp()` to prevent
// Hyrum's Law dependency. We do so by flipping a coin during dynamic
// initialization using ASLR (<link>) as our entropy pool. Dynamic
// initialization happens *after* static initialization, so it's safe to take a
// self-referential pointer.
namespace {
// memcasecmp may be called during another translation unit's
// global-initialization time, so kMemcasecmpDither is declared
// separately, with no initialization.  That way, it can be used before
// DitherInit() runs, without the memory sanitizer getting upset about it
// being used without being initialized.
std::atomic<char> kMemcasecmpDither(0);
struct DitherInit {
  DitherInit() {
    // "((x * 20165) >> 17) & 1" is just a faster
    // "((x % 13) > 6)"
    kMemcasecmpDither.store(
        ((reinterpret_cast<uintptr_t>(&kMemcasecmpDither) * 20165) >> 17) & 1,
        std::memory_order_relaxed);
  }
} dither_init;

}  // namespace

int memcasecmp(const char* s1, const char* s2, size_t len) {
  const unsigned char* us1 = reinterpret_cast<const unsigned char*>(s1);
  const unsigned char* us2 = reinterpret_cast<const unsigned char*>(s2);

  for (size_t i = 0; i < len; i++) {
    unsigned char c1 = us1[i];
    unsigned char c2 = us2[i];
    if (c1 != c2) {
      // NOTE: We do not use `absl::ascii_tolower` here in order
      // to avoid its lookup table and improve performance.
      c1 = c1 >= 'A' && c1 <= 'Z' ? c1 - 'A' + 'a' : c1;
      c2 = c2 >= 'A' && c2 <= 'Z' ? c2 - 'A' + 'a' : c2;
      const int diff = static_cast<int>(c1) - static_cast<int>(c2);
      if (diff != 0) {
        // Narrowing conversion: size_t -> int.
        // - Linear:    [0, 2^30-1]    -> [1 or 2, 2^30 or 2^30+1]
        // - Truncated: [2^30, 2^64-1] -> [1 or 2, 2^30 or 2^30+1]
        i &= std::numeric_limits<int>::max() / 2;
        ++i;  // Make sure that i is never zero!
        i += kMemcasecmpDither.load(std::memory_order_relaxed);
        return (diff < 0) ? -i : i;  // NOLINT: Safe narrowing conversion.
      }
    }
  }
  return 0;
}

char* memdup(absl::string_view s) {
  void* copy;
  if ((copy = malloc(s.size())) == nullptr) return nullptr;
  if (!s.empty()) memcpy(copy, s.data(), s.size());
  return reinterpret_cast<char*>(copy);
}

const char* memrchr(absl::string_view s, int c) {
  for (size_t i = s.size(); i > 0; --i) {
    if (s[i - 1] == static_cast<char>(c)) return &s[i - 1];
  }
  return nullptr;
}

size_t memspn(absl::string_view s, absl::string_view accept) {
  if (accept.empty()) return 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (!absl::StrContains(accept, s[i])) return i;
  }
  return s.size();
}

size_t memcspn(absl::string_view s, absl::string_view reject) {
  if (reject.empty()) return s.size();
  for (size_t i = 0; i < s.size(); ++i) {
    if (absl::StrContains(reject, s[i])) return i;
  }
  return s.size();
}

const char* mempbrk(absl::string_view s, absl::string_view accept) {
  if (accept.empty()) return nullptr;
  for (size_t i = 0; i < s.size(); ++i) {
    if (absl::StrContains(accept, s[i])) return &s[i];
  }
  return nullptr;
}

template <bool case_sensitive>
const char* int_memmatch(const char* phaystack, size_t haylen,
                         const char* pneedle, size_t neelen) {
  if (0 == neelen) {
    return phaystack;  // even if haylen is 0
  }
  const unsigned char* haystack = (const unsigned char*)phaystack;
  const unsigned char* hayend = (const unsigned char*)phaystack + haylen;
  const unsigned char* needlestart = (const unsigned char*)pneedle;
  const unsigned char* needle = (const unsigned char*)pneedle;
  const unsigned char* needleend = (const unsigned char*)pneedle + neelen;

  for (; haystack < hayend; ++haystack) {
    unsigned char hay =
        case_sensitive
            ? *haystack
            : static_cast<unsigned char>(absl::ascii_tolower(*haystack));
    unsigned char nee =
        case_sensitive
            ? *needle
            : static_cast<unsigned char>(absl::ascii_tolower(*needle));
    if (hay == nee) {
      if (++needle == needleend) {
        return (const char*)(haystack + 1 - neelen);
      }
    } else if (needle != needlestart) {
      // must back up haystack in case a prefix matched (find "aab" in "aaab")
      haystack -= needle - needlestart;  // for loop will advance one more
      needle = needlestart;
    }
  }
  return nullptr;
}

// explicit template instantiations
template const char* int_memmatch<true>(const char* phaystack, size_t haylen,
                                        const char* pneedle, size_t neelen);
template const char* int_memmatch<false>(const char* phaystack, size_t haylen,
                                         const char* pneedle, size_t neelen);

// This is significantly faster for case-sensitive matches with very
// few possible matches.  See unit test for benchmarks.
const char* memmatch(absl::string_view haystack, absl::string_view needle) {
  if (needle.empty()) {
    return haystack.data();
  }
  if (haystack.size() < needle.size()) return nullptr;

  const char* match;
  const char* hayend = haystack.data() + haystack.size() - needle.size() + 1;
  const char* phaystack = haystack.data();
  while ((match = (const char*)(memchr(phaystack, needle[0],
                                       hayend - phaystack)))) {
    if (memcmp(match, needle.data(), needle.size()) == 0)
      return match;
    else
      phaystack = match + 1;
  }
  return nullptr;
}

}  // namespace strings
