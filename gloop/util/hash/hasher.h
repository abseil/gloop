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
// WARNING: these are legacy hashers, avoid in new code.
// These algorithms ar about 10x slower than the state of the art, are
// platform-dependent, and should be avoided when possible.
// See <link> for up to date recommendations.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_HASHER_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_HASHER_H_

#include <cstdint>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"

// Hasher32/Hasher64
//
//   These are stream based objects for hashing data. Repeated calls
//   to Add* operations add data to the hash. When all the data is
//   added, Result() is called and returns the final hash value. The
//   result is indepedent of the granularity at which the data is
//   added: e.g. adding one byte at a time is the same as adding it
//   all at once. After Result is called, it is illegal to call any
//   Add* operations.
//
//   This is less efficient than Hash{32,64}StringWithSeed. They are
//   almost twice as slow for 32 byte blocks, 7% slower on 256 byte blocks
//   and 2% slower on 1K block. Use this only if you need the stream style
//   interaction.
//

class Hasher32 {
 private:
  uint32_t a_, b_, c_;  // internal state for the hash function
  char buf_[3 * sizeof(uint32_t)];
  uint32_t used_;  // How much of buf_ is in use
  uint32_t len_;   // How much data has been pushed through the hasher
  bool done_;      // Has Result() been called

 public:
  // Initialize a hasher with the seed
  explicit Hasher32(uint32_t seed) { Reset(seed); }

  // Add a string to the hash value
  void AddString(absl::string_view sv);
  ABSL_DEPRECATE_AND_INLINE()
  inline void AddString(const char* s, uint32_t len) {
    AddString(absl::string_view(s, len));
  }

  // Extract the result from the hasher.  Once Result() or
  // ResultNonReserved() is called, it is illegal to add data to the
  // hasher.
  //
  // Result() returns the same value as Hash32StringWithSeed() on the same
  // input and seed.  Thus, like Hash32StringWithSeed(), Result() may
  // return any value.  ResultNonReserved() returns kIllegalHash32-1 if
  // Result() returns kIllegalHash32 but otherwise returns the same value
  // as Result().  Thus, like HashTo32(), it never returns kIllegalHash32.
  //
  // ResultNonReserved() is added to address the discrepancy between
  // Result() and HashTo32() going forward.  Previous code likely uses
  // Result() and HashTo32() interchangeably without realizing that they're
  // not completely equivalent.  Hash values computed with one may be
  // verified with the other.  Changing the behavior of Result() directly
  // may cause backward compatibility problems when different versions of
  // software interface.  We settle for a new method ResultNonReserved().
  uint32_t Result();
  uint32_t ResultNonReserved();

  // Reset to pristine state
  void Reset(uint32_t seed);
};

class Hasher64 {
 private:
  uint64_t a_, b_, c_;  // internal state for the hash function
  char buf_[3 * sizeof(uint64_t)];
  uint32_t used_;  // How much of buf_ is in use
  bool done_;      // Has Result() been called
  uint64_t len_;   // How much data has been pushed through the hasher

 public:
  // Initialize a hasher with the seed
  explicit Hasher64(uint64_t seed) { Reset(seed); }

  // Add a string to the hash value
  void AddString(absl::string_view sv);
  ABSL_DEPRECATE_AND_INLINE()
  inline void AddString(const char* s, uint32_t len) {
    AddString(absl::string_view(s, len));
  }

  // Extract the result from the hasher. Once Result() is called, it is
  // illegal to add data to the hasher.  Result() returns the same value as
  // Hash64StringWithSeed() on the same seed and input.
  //
  // Unlike Hasher32, there is no ResultNonReserved() because there has not
  // been a function HashTo64() and hence no compatibility issue as in
  // Hasher32's case.  If a HashTo64() were created to avoid reserved
  // values like HashTo32() does, add a ResultNonReserved() here to prevent
  // the same problem that occurred with HashTo32() and Hasher32::Result().
  uint64_t Result();

  // Reset to pristine state
  void Reset(uint64_t seed);
};

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_HASHER_H_
