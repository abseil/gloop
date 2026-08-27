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

#ifndef THIRD_PARTY_GLOOP_BASE_STRERROR_H_
#define THIRD_PARTY_GLOOP_BASE_STRERROR_H_

#include <string>

#include "absl/base/internal/strerror.h"

namespace base {
// A portable and thread-safe alternative to `strerror()`.
// Returns a human-readable string describing the given POSIX error code. If the
// error code is not translatable, the string will be "Unknown error nnn".
// `errno` will not be modified by this call.
inline std::string StrError(int errnum) {
  return absl::base_internal::StrError(errnum);
}
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_STRERROR_H_
