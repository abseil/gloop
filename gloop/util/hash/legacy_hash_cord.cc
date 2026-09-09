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

#include "gloop/util/hash/legacy_hash_cord.h"

#include <cstdint>

#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/hash.h"
#include "gloop/util/hash/hasher.h"
#include "gloop/util/hash/jenkins.h"
#include "gloop/util/hash/seeds.h"

namespace util_hash {

uint32_t HashCordTo32(const absl::Cord& cord) {
  if (auto flat = cord.TryFlat()) {
    return HashTo32(*flat);
  }

  Hasher32 hasher(MIX32);
  for (absl::string_view chunk : cord.Chunks()) {
    hasher.AddString(chunk);
  }
  return hasher.ResultNonReserved();
}

uint64_t FingerprintCord(const absl::Cord& cord) {
  if (auto flat = cord.TryFlat()) {
    return Fingerprint(*flat);
  }

  Hasher32 hi(util_hash_internal::kFingerprintSeedHigh);
  Hasher32 lo(util_hash_internal::kFingerprintSeedLow);
  for (absl::string_view chunk : cord.Chunks()) {
    hi.AddString(chunk);
    lo.AddString(chunk);
  }
  return CombineFingerprintHalves(hi.Result(), lo.Result());
}

}  // namespace util_hash
