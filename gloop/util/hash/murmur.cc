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

#include "gloop/util/hash/murmur.h"

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/endian/endian.h"

namespace util_hash {

namespace {

// Murmur 2.0 multiplication constant.
static const uint64_t kMul = 0xc6a4a7935bd1e995ULL;

// We need to mix some of the bits that get propagated and mixed into the
// high bits by multiplication back into the low bits. 17 last bits get
// a more efficiently mixed with this.
inline uint64_t ShiftMix(uint64_t val) { return val ^ (val >> 47); }

// Accumulate 8 bytes into 64-bit Murmur hash
inline uint64_t MurmurStep(uint64_t hash, uint64_t data) {
  hash ^= ShiftMix(data * kMul) * kMul;
  hash *= kMul;
  return hash;
}

}  // namespace

void MurmurCat::Init(uint64_t seed, size_t total_len) {
  hash_ = seed ^ (total_len * kMul);
  offset_ = 0;
}

void MurmurCat::Append(absl::string_view sv) {
  const char* buf = sv.data();
  size_t len = sv.size();

  if (offset_ + len < 8) {
    if (len == 0) {
      return;
    }
    // Not enough data to murmur, accumulate more.
    last_val_ |= LittleEndian::Load64VariableLength(buf, static_cast<int>(len))
                 << (offset_ * 8);
    offset_ += static_cast<int>(len);
    return;  // We don't have enough to hash yet.
  }
  if (offset_ != 0) {
    // We have enough data to murmur, and there is old data
    // in last_val_.
    int num_bytes = 8 - offset_;
    last_val_ |= LittleEndian::Load64VariableLength(buf, num_bytes)
                 << (offset_ * 8);
    hash_ = MurmurStep(hash_, last_val_);
    buf += num_bytes;
    len -= num_bytes;
  }
  const size_t len_aligned = len & ~0x7;
  const char* const end = buf + len_aligned;
  for (const char* p = buf; p != end; p += 8) {
    hash_ = MurmurStep(hash_, LittleEndian::Load64(p));
  }
  if ((len & 0x7) != 0) {
    offset_ = len & 0x7;
    last_val_ = LittleEndian::Load64VariableLength(end, len & 0x7);
  } else {
    last_val_ = 0;
    offset_ = 0;
  }
}

void MurmurCat::AppendAligned(absl::Span<const uint64_t> span) {
  const uint64_t* buffer = span.data();
  size_t num_elements = span.size();

  DCHECK_EQ(offset_, 0);
  for (const uint64_t* end = buffer + num_elements; buffer != end; ++buffer) {
    hash_ = MurmurStep(hash_, *buffer);
  }
}

uint64_t MurmurCat::GetHash() const {
  uint64_t hash = hash_;
  if ((offset_ & 0x7) != 0) {
    const uint64_t data = last_val_;
    hash ^= data;
    hash *= kMul;
  }
  hash = ShiftMix(hash) * kMul;
  hash = ShiftMix(hash);
  return hash;
}

uint64_t MurmurHash64WithSeed(absl::string_view sv, const uint64_t seed) {
  const char* buf = sv.data();
  const size_t len = sv.size();

  // Let's remove the bytes not divisible by the sizeof(uint64).
  // This allows the inner loop to process the data as 64 bit integers.
  const size_t len_aligned = len & ~0x7;
  const char* const end = buf + len_aligned;
  uint64_t hash = seed ^ (len * kMul);
  for (const char* p = buf; p != end; p += 8) {
    hash = MurmurStep(hash, LittleEndian::Load64(p));
  }
  if ((len & 0x7) != 0) {
    const uint64_t data = LittleEndian::Load64VariableLength(end, len & 0x7);
    hash ^= data;
    hash *= kMul;
  }
  hash = ShiftMix(hash) * kMul;
  hash = ShiftMix(hash);
  return hash;
}

// MOE:begin_strip
// MurmurHash128 is a 128 bit hashing variant of the Murmur 2.0 algorithm.
// The low 64 bits are computed exactly the same way as in MurmurHash64.
// The higher 64 bits are computed parasitically as a sequence of xors
// through the intermediate hash values. The correlaction between the
// high and low bits with the last data values is reduced by using a separate
// mixture multiplier for the high bits.
absl::uint128 MurmurHash128WithSeed(absl::string_view sv, absl::uint128 seed) {
  const char* buf = sv.data();
  const size_t len = sv.size();

  // Initialize the hashing value.
  uint64_t hash = absl::Uint128High64(seed) ^ (len * kMul);

  // hash2 will be xored by hash during the hash computation iterations.
  // In the end we use an alternative mixture multiplier for mixing
  // the bits in hash2.
  uint64_t hash2 = absl::Uint128Low64(seed);

  // Let's remove the bytes not divisible by the sizeof(uint64).
  // This allows the inner loop to process the data as 64 bit integers.
  const size_t len_aligned = len & ~0x7;
  const char* const end = buf + len_aligned;

  for (const char* p = buf; p != end; p += 8) {
    // Manually unrolling this loop 2x did not help on Intel Core 2.
    hash = MurmurStep(hash, LittleEndian::Load64(p));
    hash2 ^= hash;
  }
  if ((len & 0x7) != 0) {
    const uint64_t data = LittleEndian::Load64VariableLength(end, len & 0x7);
    hash ^= data;
    hash *= kMul;
    hash2 ^= hash;
  }
  hash = ShiftMix(hash) * kMul;
  hash2 ^= hash;
  hash = ShiftMix(hash);

  // mul2 is a prime just above golden ratio. mul2 is used to ensure that the
  // impact of the last few bytes is different to the upper and lower 64 bits.
  static const uint64_t mul2 = 0x9e3779b97f4a7835ULL;
  hash2 = ShiftMix(hash2 * mul2) * mul2;

  return absl::MakeUint128(hash2, hash);
}

// MOE:end_strip
}  // namespace util_hash
