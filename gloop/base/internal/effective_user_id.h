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

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_EFFECTIVE_USER_ID_H_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_EFFECTIVE_USER_ID_H_

#include <cstdint>
#include <optional>

namespace base::internal {

// Empty for non-UNIX binaries.
std::optional<uint64_t> GetEffectiveUserId();

}  // namespace base::internal

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_EFFECTIVE_USER_ID_H_
