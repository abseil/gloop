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

#include "gloop/base/raw_printer.h"

#include <assert.h>

namespace base {

RawPrinter::RawPrinter(char* buf, int length)
    : base_(buf), limit_(buf + length - 1) {
  assert(length > 0);
  *limit_ = '\0';
  reset();
  assert(ptr_ == base_);
  assert(*ptr_ == '\0');
}

void RawPrinter::reset() {
  ptr_ = base_;
  *ptr_ = '\0';
}

}  // namespace base
