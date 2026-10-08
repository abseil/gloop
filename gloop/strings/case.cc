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

// This file contains string processing functions related to
// uppercase, lowercase, etc.

#include "gloop/strings/case.h"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>

#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/memutil.h"

namespace strings {

std::ostream& operator<<(std::ostream& os,
                         const AsciiCapitalizationType& type) {
  switch (type) {
    case AsciiCapitalizationType::kLower:
      return os << "kLower";
    case AsciiCapitalizationType::kUpper:
      return os << "kUpper";
    case AsciiCapitalizationType::kFirst:
      return os << "kFirst";
    case AsciiCapitalizationType::kMixed:
      return os << "kMixed";
    case AsciiCapitalizationType::kNoAlpha:
      return os << "kNoAlpha";
    default:
      return os << "INVALID";
  }
}

AsciiCapitalizationType GetAsciiCapitalization(const absl::string_view input) {
  const auto end = input.cend();
  // find the caps type of the first alpha char
  auto s = std::find_if(input.cbegin(), end, absl::ascii_isalpha);
  if (s == end) return AsciiCapitalizationType::kNoAlpha;
  const AsciiCapitalizationType firstcapstype =
      (absl::ascii_islower(*s)) ? AsciiCapitalizationType::kLower
                                : AsciiCapitalizationType::kUpper;

  // skip ahead to the next alpha char
  s = std::find_if(++s, end, absl::ascii_isalpha);
  if (s == end) return firstcapstype;
  const AsciiCapitalizationType capstype =
      (absl::ascii_islower(*s)) ? AsciiCapitalizationType::kLower
                                : AsciiCapitalizationType::kUpper;

  if (firstcapstype == AsciiCapitalizationType::kLower &&
      capstype == AsciiCapitalizationType::kUpper) {
    return AsciiCapitalizationType::kMixed;
  }

  if (std::find_if(s, end, [capstype](unsigned char ch) {
        return (absl::ascii_isupper(ch) &&
                capstype != AsciiCapitalizationType::kUpper) ||
               (absl::ascii_islower(ch) &&
                capstype != AsciiCapitalizationType::kLower);
      }) != end) {
    return AsciiCapitalizationType::kMixed;
  }

  if (firstcapstype == AsciiCapitalizationType::kUpper &&
      capstype == AsciiCapitalizationType::kLower) {
    return AsciiCapitalizationType::kFirst;
  }
  return capstype;
}

int AsciiCaseInsensitiveCompare(absl::string_view s1, absl::string_view s2) {
  if (s1.size() == s2.size()) {
    return memcasecmp(s1.data(), s2.data(), s1.size());
  } else if (s1.size() < s2.size()) {
    int res = memcasecmp(s1.data(), s2.data(), s1.size());
    return (res == 0) ? -1 : res;
  } else {
    int res = memcasecmp(s1.data(), s2.data(), s2.size());
    return (res == 0) ? 1 : res;
  }
}

size_t AsciiCaseInsensitiveHash::operator()(absl::string_view s) const {
  return absl::HashOf(absl::AsciiStrToLower(s));
}

bool AsciiCaseInsensitiveEq::operator()(absl::string_view s1,
                                        absl::string_view s2) const {
  return s1.size() == s2.size() &&
         memcasecmp(s1.data(), s2.data(), s1.size()) == 0;
}

void MakeAsciiTitlecase(std::string* s, absl::string_view delimiters) {
  bool upper = true;
  for (auto& ch : *s) {
    if (upper) {
      ch = absl::ascii_toupper(ch);
    }
    upper = (absl::StrContains(delimiters, ch));
  }
}

std::string MakeAsciiTitlecase(absl::string_view s,
                               absl::string_view delimiters) {
  std::string result(s);
  MakeAsciiTitlecase(&result, delimiters);
  return result;
}

}  // namespace strings
