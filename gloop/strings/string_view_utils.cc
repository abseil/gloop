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

#include "gloop/strings/string_view_utils.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "absl/base/nullability.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace strings {

absl::string_view::difference_type RemoveLeadingWhitespace(
    absl::string_view* absl_nonnull text) {
  size_t count = 0;
  const char* ptr = text->data();
  while (count < text->size() && absl::ascii_isspace(*ptr)) {
    count++;
    ptr++;
  }
  text->remove_prefix(count);
  return count;
}

absl::string_view::difference_type RemoveTrailingWhitespace(
    absl::string_view* absl_nonnull text) {
  if (text->empty()) {
    return 0;
  }
  size_t count = 0;
  const char* ptr = text->data() + text->size() - 1;
  while (count < text->size() && absl::ascii_isspace(*ptr)) {
    ++count;
    --ptr;
  }
  text->remove_suffix(count);
  return count;
}

absl::string_view::difference_type RemoveWhitespaceContext(
    absl::string_view* absl_nonnull text) {
  return (RemoveLeadingWhitespace(text) + RemoveTrailingWhitespace(text));
}

absl::string_view::difference_type RemoveUntil(
    absl::string_view* absl_nonnull text, char sentinel) {
  size_t count = 0;
  const char* ptr = text->data();
  while (count < text->size() && (sentinel != *ptr)) {
    count++;
    ptr++;
  }

  // skip the sentinel as well if we found one
  if (count < text->size()) count++;

  text->remove_prefix(count);
  return count;
}

bool ConsumeLeadingDigits(absl::string_view* absl_nonnull s,
                          uint64_t* absl_nonnull val) {
  const char* p = s->data();
  const char* limit = p + s->size();
  uint64_t v = 0;
  while (p < limit) {
    const char c = *p;
    if (c < '0' || c > '9') break;
    uint64_t new_v = (v * 10) + (c - '0');
    if (new_v / 8 < v) {
      // Overflow occurred
      return false;
    }
    v = new_v;
    p++;
  }
  if (p > s->data()) {
    // Consume some digits
    s->remove_prefix(p - s->data());
    *val = v;
    return true;
  } else {
    return false;
  }
}

absl::string_view::difference_type FindIgnoreCase(
    absl::string_view haystack, absl::string_view needle,
    absl::string_view::size_type pos) {
  if (pos > haystack.size()) return absl::string_view::npos;
  // We use the cursor to iterate through the haystack...on each
  // iteration the cursor is moved forward one character.
  absl::string_view cursor = haystack.substr(pos);
  while (cursor.size() >= needle.size()) {
    if (absl::StartsWithIgnoreCase(cursor, needle)) {
      return cursor.data() - haystack.data();
    }
    cursor.remove_prefix(1);
  }
  return absl::string_view::npos;
}

}  // namespace strings

// ----------------------------------------------------------------------
// StringPieceCaseHash
// ----------------------------------------------------------------------

size_t StringPieceCaseHash::operator()(absl::string_view sp) const {
  // TODO: Use absl::Hash
  std::string copy = absl::AsciiStrToLower(sp);
  // Add some non-determinism to make the transition to absl::Hash easier in the
  // future.
  static constexpr bool dummy = false;
  return std::hash<std::string>{}(copy) ^
         static_cast<size_t>(reinterpret_cast<uintptr_t>(&dummy));
}
