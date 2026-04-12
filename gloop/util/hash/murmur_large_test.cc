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
// Tests for the fast hashing algorithm based on Austin Appleby's
// MurmurHash 2.0 algorithm. See http://murmurhash.googlepages.com/
//
// Testing is split into small and large parts. The small test runs in
// 10 seconds and the large takes around 30 seconds in optimized mode, and
// substantially longer in debug.

#include <stdint.h>

#include <cstddef>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/hash/murmur.h"
#include "gloop/util/random/acmrandom.h"

namespace util_hash {
namespace {

// Reference public domain implementation from
// http://murmurhash.googlepages.com/
// If you need this code or parts of it in production code, move it first to
// third_party.
uint64_t MurmurHash64Reference(const void* key, size_t len) {
  unsigned int seed = 0;
  const uint64_t m = 0xc6a4a7935bd1e995ULL;
  const int r = 47;

  uint64_t h = seed ^ (len * m);

  const uint64_t* data = (const uint64_t*)key;
  const uint64_t* end = data + (len / 8);

  while (data != end) {
    uint64_t k = LittleEndian::Load64(data++);

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const unsigned char* data2 = (const unsigned char*)data;

  switch (len & 7) {
    case 7:
      h ^= uint64_t{data2[6]} << 48;
      ABSL_FALLTHROUGH_INTENDED;
    case 6:
      h ^= uint64_t{data2[5]} << 40;
      ABSL_FALLTHROUGH_INTENDED;
    case 5:
      h ^= uint64_t{data2[4]} << 32;
      ABSL_FALLTHROUGH_INTENDED;
    case 4:
      h ^= uint64_t{data2[3]} << 24;
      ABSL_FALLTHROUGH_INTENDED;
    case 3:
      h ^= uint64_t{data2[2]} << 16;
      ABSL_FALLTHROUGH_INTENDED;
    case 2:
      h ^= uint64_t{data2[1]} << 8;
      ABSL_FALLTHROUGH_INTENDED;
    case 1:
      h ^= uint64_t{data2[0]};
      h *= m;
  }

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

constexpr size_t kLargeInputLen = (1ULL << 16) + 97;

TEST(MurmurCatLargeInput, AgainstReferenceImplementation) {
  const size_t substring_len = kLargeInputLen;
  const size_t substrings = 2;
  const size_t total_len = substring_len * substrings;
  char* data = new char[total_len];
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (size_t i = 0; i < total_len; ++i) {
    data[i] = rnd.Rand8();
  }
  MurmurCat murmur;
  murmur.Init(uint64_t{0}, total_len);
  for (size_t i = 0; i < substrings; ++i) {
    murmur.Append(absl::string_view(&data[substring_len * i], substring_len));
  }
  const uint64_t hash = murmur.GetHash();
  uint64_t reference_hash = MurmurHash64Reference(data, total_len);
  delete[] data;
  EXPECT_EQ(reference_hash, hash);
}

TEST(Murmur64LargeInput, AgainstReferenceImplementation) {
  const size_t len = kLargeInputLen;
  char* data = new char[len];
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (size_t i = 0; i < len; ++i) {
    data[i] = rnd.Rand8();
  }
  const uint64_t hash = util_hash::MurmurHash64(absl::string_view(data, len));
  uint64_t reference_hash = MurmurHash64Reference(data, len);
  delete[] data;
  EXPECT_EQ(reference_hash, hash);
}
}  // namespace
}  // namespace util_hash
