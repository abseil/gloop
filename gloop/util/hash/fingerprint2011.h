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

//
// Fingerprint2011() is a fingerprinting function that is both
// higher-quality and faster than Fingerprint() for fingerprinting strings.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_FINGERPRINT2011_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_FINGERPRINT2011_H_

#include <stddef.h>  // for size_t

#include <cstdint>

#include "absl/base/macros.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

// This is a portable, high-quality hash function that will never change.
// However, it is not suitable for cryptography.  It never returns 0 or 1.
uint64_t Fingerprint2011(absl::string_view sv);
ABSL_DEPRECATE_AND_INLINE()
inline uint64_t Fingerprint2011(const char* s, size_t len) {
  return Fingerprint2011(absl::string_view(s, len));
}

uint64_t Fingerprint2011(const absl::Cord& s);

// This hashes two 64-bit fingerprints into one.  It assumes that each
// input is already a high-quality fingerprint itself.  This function
// never returns 0 or 1 and will never change.
//
// FingerprintCat2011() is designed for speed. Unlike Fingerprint2011(), it is a
// low-quality hash function. For stronger mixing, store the inputs in an array
// and use Fingerprint2011(reinterpret_cast<const char*>(&fps), ...) instead.
//
// This method is often good enough for basic equality comparisons and sharding
// (fp % N), but problematic for threshold comparisons (fp < threshold). See
// <link> for a longer explanation.
//
// Note that in general it's impossible to construct Fingerprint2011(str)
// from the fingerprints of substrings of str.  One shouldn't expect
// FingerprintCat2011(Fingerprint2011(x), Fingerprint2011(y)) to indicate
// anything about Fingerprint2011(StrCat(x, y)).
uint64_t FingerprintCat2011(uint64_t fp1, uint64_t fp2);

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_FINGERPRINT2011_H_
