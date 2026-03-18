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

#include "gloop/util/hash/legacy_hash_cord.h"

#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/hash.h"
#include "gtest/gtest.h"

namespace {

TEST(LegacyHashCord, CordFunctions) {
  static constexpr absl::string_view str = "123456";
  absl::Cord flat_cord("123456");
  absl::Cord fragmented_cord = absl::MakeFragmentedCord({"123", "456"});

  EXPECT_EQ(Fingerprint(str), util_hash::FingerprintCord(flat_cord));
  EXPECT_EQ(Fingerprint(str), util_hash::FingerprintCord(fragmented_cord));
  EXPECT_EQ(Fingerprint(""), util_hash::FingerprintCord(absl::Cord()));

  EXPECT_EQ(HashTo32(str), util_hash::HashCordTo32(flat_cord));
  EXPECT_EQ(HashTo32(str), util_hash::HashCordTo32(fragmented_cord));
  EXPECT_EQ(HashTo32(""), util_hash::HashCordTo32(absl::Cord()));
}

}  // namespace
