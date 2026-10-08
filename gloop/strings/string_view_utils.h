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

// Utility functions for operating on absl::string_views
// Collected here for convenience

#ifndef THIRD_PARTY_GLOOP_STRINGS_STRING_VIEW_UTILS_H_
#define THIRD_PARTY_GLOOP_STRINGS_STRING_VIEW_UTILS_H_

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace strings {

// Assigns 'src' to '*dest'.
inline void CopyToString(absl::string_view src,
                         std::string* absl_nonnull dest) {
  dest->assign(src.data(), src.size());
}

// Removes leading absl::ascii_isspace() characters.
// Returns number of characters removed.
absl::string_view::difference_type RemoveLeadingWhitespace(
    absl::string_view* absl_nonnull text);

// Removes trailing absl::ascii_isspace() characters.
// Returns number of characters removed.
absl::string_view::difference_type RemoveTrailingWhitespace(
    absl::string_view* absl_nonnull text);

// Removes leading and trailing absl::ascii_isspace() chars.
// Returns number of chars removed.
absl::string_view::difference_type RemoveWhitespaceContext(
    absl::string_view* absl_nonnull text);

// Removes all characters up to and including specified char. If the specified
// char does not appear in the string, all characters are removed.
// Returns number of characters removed.
absl::string_view::difference_type RemoveUntil(
    absl::string_view* absl_nonnull text, char sentinel);

// Consume a leading positive integer value.  If any digits
// were found, store the value of the leading unsigned number in
// "*val", advance "*s" past the consumed number, and return true.
// If overflow occurred, returns false.
// Otherwise, returns false.
// Equivalent to RE::Consume(s, "(\\d+)", val) but significantly
// faster:
//
//   Run on panther (4 X 2200 MHz CPUs); 2008/10/21-09:20:57
//   CPU: AMD Opteron Engineering Sample (4 cores) dL1:64KB dL2:1024KB
//   Benchmark                     Time(ns)    CPU(ns) Iterations
//   ------------------------------------------------------------
//   BM_ConsumeDigits                    20         20   38049268
//   BM_ConsumeDigitsWithRE             420        416    1678915
//
// (i.e. 20 ns vs. 420 ns (even with the regexp pre-created)).
bool ConsumeLeadingDigits(absl::string_view* absl_nonnull s,
                          uint64_t* absl_nonnull val);

// If *s starts with 'expected', consume it and return true.
// Otherwise, return false.
inline bool ConsumeLeadingChar(absl::string_view* absl_nonnull s,
                               char expected) {
  if (!s->empty() && (*s)[0] == expected) {
    s->remove_prefix(1);
    return true;
  } else {
    return false;
  }
}

// This is similar to gstrncasestr() in strutil.h, except that it works with
// absl::string_views. It acts the same as absl::string_view::find(), except
// that it is case insensitive.
absl::string_view::difference_type FindIgnoreCase(
    absl::string_view haystack, absl::string_view needle,
    absl::string_view::size_type pos = 0);

// Like ConsumePrefix, but case insensitive.
inline bool ConsumeCasePrefix(absl::string_view* absl_nonnull s,
                              absl::string_view expected) {
  if (absl::StartsWithIgnoreCase(*s, expected)) {
    s->remove_prefix(expected.size());
    return true;
  }
  return false;
}

// Like ConsumeSuffix, but case insensitive.
inline bool ConsumeCaseSuffix(absl::string_view* absl_nonnull s,
                              absl::string_view expected) {
  if (absl::EndsWithIgnoreCase(*s, expected)) {
    s->remove_suffix(expected.size());
    return true;
  }
  return false;
}

}  // namespace strings

// ----------------------------------------------------------------------
// StringPieceCaseHash
// StringPieceCaseEqual
//
// Function objects for case-insensitive hash_map from absl::string_view.  E.g.,
// hash_map<absl::string_view, int, StringPieceCaseHash, StringPieceCaseEqual>
// ht;
// ----------------------------------------------------------------------

struct StringPieceCaseHash {
  using is_transparent = void;
  size_t operator()(absl::string_view sp) const;
};

struct StringPieceCaseEqual {
  using is_transparent = void;
  bool operator()(absl::string_view piece1, absl::string_view piece2) const {
    return absl::EqualsIgnoreCase(piece1, piece2);
  }
};

#endif  // THIRD_PARTY_GLOOP_STRINGS_STRING_VIEW_UTILS_H_
