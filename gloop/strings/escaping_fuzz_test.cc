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

#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gloop/strings/escaping.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

void FuzzUnescapeCEscapeSequences(const std::string& s) {
  // UnescapeCEscapeSequences reads until null terminator.
  // We ensure the input string does not contain embedded nulls so that
  // the entire string is processed.
  if (absl::StrContains(s, '\0')) return;

  // By using carefully allocated char* instead of ::string, we increase the
  // ability to detect accessing past the end of the buffer with short strings.
  auto src = std::make_unique<char[]>(s.size() + 1);
  memcpy(src.get(), s.data(), s.size());
  src[s.size()] = '\0';
  auto dst = std::make_unique<char[]>(s.size() + 1);
  UnescapeCEscapeSequences(src.get(), dst.get());
}
FUZZ_TEST(EscapingFuzz, FuzzUnescapeCEscapeSequences);

void Base32EscapeRoundTrip(const std::string& src) {
  std::string escaped;
  EXPECT_TRUE(Base32Escape(src, &escaped));
  std::string unescaped;
  EXPECT_TRUE(Base32Unescape(escaped, &unescaped));
  EXPECT_EQ(src, unescaped);
}
FUZZ_TEST(EscapingFuzz, Base32EscapeRoundTrip);

// This only tests that Base32Unescape() does not crash.
void FuzzBase32Unescape(absl::string_view src) {
  std::string dst;
  static_cast<void>(Base32Unescape(src, &dst));
}
FUZZ_TEST(EscapingFuzz, FuzzBase32Unescape);

void Base32HexEscapeRoundTrip(const std::string& src) {
  std::string escaped;
  EXPECT_TRUE(Base32HexEscape(src, &escaped));
  std::string unescaped;
  EXPECT_TRUE(Base32HexUnescape(escaped, &unescaped));
  EXPECT_EQ(src, unescaped);
}
FUZZ_TEST(EscapingFuzz, Base32HexEscapeRoundTrip);

// This only tests that Base32HexUnescape() does not crash.
void FuzzBase32HexUnescape(absl::string_view src) {
  std::string dst;
  static_cast<void>(Base32HexUnescape(src, &dst));
}
FUZZ_TEST(EscapingFuzz, FuzzBase32HexUnescape);

}  // namespace
}  // namespace strings
