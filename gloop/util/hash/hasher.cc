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

// This module contains legacy compatibility code for the stream interface
// hashers Hasher32 and Hasher64. For simple hashing of strings with the same
// algorithm, use Hash{32,64}StringWithSeed in util/hash/hash.h.
//
// (They are still slow. See <link> for modern recommendations.)

#include "gloop/util/hash/hasher.h"

#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/log/absl_check.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/hash.h"
#include "gloop/util/hash/jenkins_lookup2.h"

// Defined in jenkins.cc
extern uint32_t Hash32StringWithSeedReferenceImplementation(const char* s,
                                                            size_t len,
                                                            uint32_t seed);
extern uint64_t Hash64StringWithSeedReferenceImplementation(const char* s,
                                                            size_t len,
                                                            uint64_t seed);

void Hasher32::Reset(uint32_t seed) {
  c_ = seed;
  // Set an arbitrary value for a_ and b_.
  a_ = b_ = 0x12f905ffUL;
  used_ = 0;
  len_ = 0;
  done_ = false;
}

uint32_t Hasher32::Result() {
  // Hash remaining tail.
  if (used_ > 0) {
    a_ = Hash32StringWithSeedReferenceImplementation(buf_, used_, c_);
    mix(a_, b_, c_);
    used_ = 0;
  }

  done_ = true;
  return c_;
}

void Hasher32::AddString(absl::string_view sv) {
  const char* s = sv.data();
  uint32_t len = sv.size();

  ABSL_RAW_CHECK(!done_, "Can not call AddString after Result.");

  // Hash until buffer is full.
  int offset = 0;
  const int buf_size = sizeof(buf_);
  len_ += len;

  while (len > 0) {
    const int bytes_copied = std::min(buf_size - used_, len);
    memcpy(buf_ + used_, s + offset, bytes_copied);
    offset += bytes_copied;
    len -= bytes_copied;
    used_ += bytes_copied;

    if (used_ == buf_size) {
      a_ = Hash32StringWithSeedReferenceImplementation(buf_, buf_size, c_);
      mix(a_, b_, c_);
      used_ = 0;
    }
  }
}

uint32_t Hasher32::ResultNonReserved() {
  const uint32_t result = Result();
  return (result == kIllegalHash32) ? (kIllegalHash32 - 1) : result;
}

void Hasher64::Reset(uint64_t seed) {
  c_ = seed;
  // Set an arbitrary value for a_ and b_.
  a_ = b_ = 0x2c2ca38cd0cc731bULL;

  used_ = 0;
  len_ = 0;
  done_ = false;
}

uint64_t Hasher64::Result() {
  // Hash remaining tail.
  if (used_ > 0) {
    a_ = Hash64StringWithSeedReferenceImplementation(buf_, used_, c_);
    mix(a_, b_, c_);
    used_ = 0;
  }

  done_ = true;
  return c_;
}

void Hasher64::AddString(absl::string_view sv) {
  const char* s = sv.data();
  uint32_t len = sv.size();

  ABSL_RAW_CHECK(!done_, "Can not call AddString after Result.");

  // Hash until buffer is full.
  int offset = 0;
  const int buf_size = sizeof(buf_);
  len_ += len;

  while (len > 0) {
    const int bytes_copied = std::min(buf_size - used_, len);
    memcpy(buf_ + used_, s + offset, bytes_copied);
    offset += bytes_copied;
    len -= bytes_copied;
    used_ += bytes_copied;

    if (used_ == buf_size) {
      a_ = Hash64StringWithSeedReferenceImplementation(buf_, buf_size, c_);
      mix(a_, b_, c_);
      used_ = 0;
    }
  }
}
