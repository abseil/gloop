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

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/strings/serialize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

TEST(Serialize, DictionaryEncodeDecode) {
  const std::string google = "google";
  const std::string yahoo = "yahoo";
  const std::string cnn = "cnn";
  const std::string empty = "";

  LOG(INFO) << "Testing intmap encode/decode";
  absl::flat_hash_map<std::string, int32_t> intmap = {
      {google, 1}, {yahoo, 2}, {empty, 4}};

  std::string encoded_intmap = DictionaryEncode(intmap);
  absl::flat_hash_map<std::string, int32_t> intmap_copy;
  CHECK(DictionaryInt32Decode(&intmap_copy, encoded_intmap))
      << " decode failed for " << encoded_intmap;
  EXPECT_THAT(intmap, testing::UnorderedElementsAreArray(intmap_copy));

  LOG(INFO) << "Testing int64map encode/decode";
  absl::flat_hash_map<std::string, int64_t> int64map = {
      {google, 1}, {cnn, 2}, {empty, 4}};

  std::string encoded_int64map = DictionaryEncode(int64map);
  absl::flat_hash_map<std::string, int64_t> int64map_copy;
  CHECK(DictionaryInt64Decode(&int64map_copy, encoded_int64map))
      << " decode failed for " << encoded_int64map;
  EXPECT_THAT(int64map, testing::UnorderedElementsAreArray(int64map_copy));

  LOG(INFO) << "Testing double encode/decode";
  absl::flat_hash_map<std::string, double> doublemap = {
      {google, 1.0}, {cnn, 2.0}, {yahoo, 3.0}, {empty, 12.0}};

  std::string encoded_doublemap = DictionaryEncode(doublemap);
  absl::flat_hash_map<std::string, double> doublemap_copy;
  CHECK(DictionaryDoubleDecode(&doublemap_copy, encoded_doublemap))
      << " decode failed for " << encoded_doublemap;
  EXPECT_THAT(doublemap, testing::UnorderedElementsAreArray(doublemap_copy));

  LOG(INFO) << "Testing bad input parse";
  std::string encoded_bad_input("google:2x,yahoo:1");  // "2x" should fail parse
  CHECK(!DictionaryDoubleDecode(&doublemap_copy, encoded_bad_input))
      << " decode succeeded for " << encoded_bad_input;
}

}  // namespace strings
