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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_LEGECY_HASH_CORD_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_LEGECY_HASH_CORD_H_

// Legacy hash functions that operate on absl::Cord

#include <cstdint>

#include "absl/strings/cord.h"

namespace util_hash {

// Guaranteed to return the same value as HashTo32(std::string(cord))
uint32_t HashCordTo32(const absl::Cord& cord);

// Guaranteed to return the same value as Fingerprint(std::string(cord))
uint64_t FingerprintCord(const absl::Cord& cord);

}  // namespace util_hash

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_LEGECY_HASH_CORD_H_
