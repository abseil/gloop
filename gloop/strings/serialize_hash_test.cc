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

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "gloop/strings/serialize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using ::testing::UnorderedElementsAreArray;

TEST(Serialize, DictionaryEncodeDecode) {
  const absl::flat_hash_map<std::string, int32_t> intmap = {
      {"google", 1}, {"yahoo", 2}, {"", 4}};

  absl::flat_hash_map<std::string, int32_t> intmap_copy;
  ASSERT_TRUE(DictionaryInt32Decode(&intmap_copy, DictionaryEncode(intmap)));
  EXPECT_THAT(intmap_copy, UnorderedElementsAreArray(intmap));

  const absl::flat_hash_map<std::string, int64_t> int64map = {
      {"google", 1}, {"cnn", 2}, {"", 4}};

  absl::flat_hash_map<std::string, int64_t> int64map_copy;
  ASSERT_TRUE(
      DictionaryInt64Decode(&int64map_copy, DictionaryEncode(int64map)));
  EXPECT_THAT(int64map_copy, UnorderedElementsAreArray(int64map));

  const absl::flat_hash_map<std::string, double> doublemap = {
      {"google", 1.0}, {"cnn", 2.0}, {"yahoo", 3.0}, {"", 12.0}};

  absl::flat_hash_map<std::string, double> doublemap_copy;
  ASSERT_TRUE(
      DictionaryDoubleDecode(&doublemap_copy, DictionaryEncode(doublemap)));
  EXPECT_THAT(doublemap_copy, UnorderedElementsAreArray(doublemap));

  absl::flat_hash_map<std::string, double> bad_input_map;
  // "2x" should fail parse.
  EXPECT_FALSE(DictionaryDoubleDecode(&bad_input_map, "google:2x,yahoo:1"));
}

}  // namespace
}  // namespace strings
