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

#include <iostream>
#include <random>
#include <string>

// This script generates carefully constructed testdata.
//
// The data is designed to probe a number of edge cases with embedding data in a
// C string literal.

int main() {
  // Nuls followed by every character
  std::cout << '\0';
  for (int i = 0; i < 256; ++i) {
    if (i > 0) std::cout << '\0';
    std::cout << static_cast<char>(i);
  }
  std::cout << '\n';

  // Double-question-mark (i.e. potential trigraph) followed by every character.
  std::cout << "??";
  for (int i = 0; i < 256; ++i) {
    if (i > 0) std::cout << "??";
    std::cout << static_cast<char>(i);
  }
  std::cout << '\n';

  // Triple-question-mark followed by every 2-byte sequence from the
  // "interesting" set chr(0:48) + chr(127:129).
  std::string interesting_chars;
  for (int i = 0; i < 48; ++i)
    interesting_chars.push_back(static_cast<char>(i));
  for (int i = 127; i < 129; ++i)
    interesting_chars.push_back(static_cast<char>(i));

  std::cout << "???";
  bool first = true;
  for (char x : interesting_chars) {
    for (char y : interesting_chars) {
      if (!first) std::cout << "???";
      std::cout << x << y;
      first = false;
    }
  }
  std::cout << '\n';

  // Random characters from the "difficult" set
  std::string difficult = std::string("\0\n\r\\\"=/'()!<>-?01234567", 23);
  std::mt19937 rand(0);  // fixed seed for determinism
  std::uniform_int_distribution<> dist(0, 22);
  for (int i = 0; i < 2000; ++i) {
    std::cout << difficult[dist(rand)];
  }
  std::cout << '\n';

  std::cout << std::string(1500, 'a') << '\n';
  return 0;
}
