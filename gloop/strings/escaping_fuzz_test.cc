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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <memory>

#include "gloop/strings/escaping.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // By using carefully allocated char* instead of ::string, we increase the
  // ability to detect accessing past the end of the buffer with short strings.
  auto src = std::make_unique<char[]>(size + 1);
  memcpy(src.get(), data, size);
  src[size] = '\0';
  auto dst = std::make_unique<char[]>(size + 1);
  strings::UnescapeCEscapeSequences(src.get(), dst.get());
  return 0;
}
