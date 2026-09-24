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

#include "gloop/util/coding/elias_fano/options.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "gloop/util/coding/coder.h"
#include "gloop/util/status/ret_check.h"
#include "gloop/util/status/status_macros.h"

namespace elias_fano {
namespace {

uint8_t OptimalLowBitWidth(uint32_t size, uint64_t max_value) {
  if (size == 0 || size > max_value) return Options::kMinWidth;
  uint8_t lg2 = absl::bit_width(max_value / size) - 1;
  // If we are going to have more than kuint32max zeros, up the width.
  lg2 += ((max_value >> lg2) > std::numeric_limits<uint32_t>::max());
  return std::clamp(lg2, Options::kMinWidth, Options::kMaxWidth);
}

}  // namespace

const uint8_t Options::kMinWidth;
const uint8_t Options::kMaxWidth;
const uint32_t Options::kD1;
const uint32_t Options::kD2;
const uint32_t Options::kR;
const uint32_t Options::kMaxBitsNoIndex;

Options::Options(uint32_t size, uint64_t max_value) {
  max_value_ = max_value;
  size_ = size;
  width_ = OptimalLowBitWidth(size, max_value);
  one_bits_ = size;
  zero_bits_ = max_value >> width_;
  index_ = bits() > kMaxBitsNoIndex ? ONES | ZEROS : 0;
  CHECK_OK(CheckInvariants(width(), this->max_value()));  // Crash OK
}

Options& Options::override_width(uint8_t width) {
  width_ = width;
  zero_bits_ = max_value_ >> width;
  if (bits() <= kMaxBitsNoIndex) index_ = 0;
  return *this;
}

Options& Options::indexes(size_t index) {
  if (bits() > kMaxBitsNoIndex) index_ = index;
  return *this;
}

Options& Options::version(uint8_t version) {
  CHECK_GE(version, 3);  // Crash OK
  CHECK_LE(version, 3);  // Crash OK
  version_ = version;
  return *this;
}

size_t Options::max_sparse_size() const {
  // These bounds are computed based on the constructing an unary stream that
  // results in the maximum number of sparse indexes. Define a block as a series
  // of kD1 entries of a sparse index. Then compute two bounds:
  // - the max total blocks if we have both indexes
  // - the max blocks per index
  // If we have both indexes the latter bound will be larger than the first, so
  // we take the minimum of the two.

  // Proof of the bound: bits() / (kR - kD1)
  // Let's assume we can start blocks at any offset, not at an exact position
  // every kD1 bits. This relaxes the bound, so the bound calculated this way is
  // greater or equal than the actual bound.
  // We also assume that kR >= 3 * kD1 (with that assumption we can overlap
  // maximum theoretical number of bits on both sides of the sparse block).
  //
  // Lemma 1: The total number of bits in overlapped fragments cannot exceed
  // B * kD1, where B denotes the number of sparse blocks.
  // Proof: Each sparse block consists of few (at most kD1) precious bits, and
  // many (at least kR - kD1) common bits (block can contain less than kD1
  // precious bits iff it's the last block of given type, but in such case it
  // still needs to reach total span of kR bits).
  // When we overlap two blocks, each bit in the shared fragment is a precious
  // bit in exactly one of the blocks. This directly means that each overlapped
  // bit has to be "paid for" by exactly one block, by using one of its precious
  // bits. Total number of precious bits in all blocks is at most B * kD1 which
  // proves the lemma.
  //
  // Conclusion: If the total number of bits in overlapping fragments is at most
  // B * kD1, then B blocks cannot span over less than B * kR - B * kD1 bits,
  // which leads to: max_total_blocks <= bits() / (kR - kD1)

  // Example construction showing a possibly tighter bound:
  //
  // (bits() - kD1 - 1) / (kR - kD1)
  //
  // The max total blocks when we have both indexes is when we put as many
  // alternating sparse indexes back to back (in bit position order). In order
  // to maximize the number of sparse blocks we construct the following unary
  // stream. Sparse zero blocks are at the top, sparse one blocks to the bottom
  // and the unary stream in the middle. Assume kD1 = 4 and kR = 16:
  //
  //  |--------------|        |--------------|        |--------------|
  // 10111111111111000000000000111111111111000000000000111111111111000
  //              |--------------|        |--------------|
  //
  // In the example we can have:
  //   16 bits, 4 zeros, 12 ones, 1 sparse block
  //   28 bits, 13 zeros, 15 ones, 2 sparse blocks
  //   40 bits, 15 zeros, 25 ones, 3 sparse blocks
  //   52 bits, 25 zeros, 27 ones, 4 sparse blocks
  //   64 bits, 36 zeros, 28 ones, 5 sparse blocks
  size_t max_total_blocks = bits() / (kR - kD1);

  // When only a zero index is present the max number of zero sparse blocks
  // will result from a construction where all zero sparse blocks are next to
  // each other:
  //
  // |--------------||--------------||--------------||--------------|
  // 0111111111111000011111111111100001111111111110000111111111111000
  //
  // This is bound by the number of ones, and generalizes to:
  //
  //   #ones / (kR - kD1)
  //
  // Similarly for a single one index.
  auto max_per_index_blocks =
      (has_index(ONES) ? size_t{zero_bits()} / (kR - kD1) : 0) +
      (has_index(ZEROS) ? size_t{one_bits()} / (kR - kD1) : 0);
  return std::min(max_total_blocks, max_per_index_blocks) * kD1;
}

std::string Options::DebugString() const {
  return absl::StrFormat(
      "V=%u N=%u M=%u #ones=%u #zeros=%u width=%u indexes=%x", version_, size_,
      max_value_, one_bits_, zero_bits_, width_, index_);
}

absl::Status Options::CheckInvariants(uint8_t width, uint64_t max_value) {
  RET_CHECK_LE(width, kMaxWidth);
  RET_CHECK_LE(max_value >> width, std::numeric_limits<uint32_t>::max());
  return absl::OkStatus();
}

// Encoding format:
// varint32: version
// varint32: indexes
// varint32: size
// varint64: max_value
// varint32: width
size_t Options::EncodingLength(const Options& opts) {
  auto l32 = Encoder::varint32_length;
  auto l64 = Encoder::varint64_length;
  return l32(opts.version()) + l32(opts.index()) + l32(opts.size()) +
         l64(opts.max_value()) + l32(opts.width());
}

absl::Span<const char> Options::Encode(const Options& opts,
                                       absl::Span<char> span) {
  CHECK_GE(span.size(), EncodingLength(opts));  // Crash OK
  ::Encoder enc(span.data(), span.size());
  enc.put_varint32(opts.version());
  enc.put_varint32(opts.index());
  enc.put_varint32(opts.size());
  enc.put_varint64(opts.max_value());
  enc.put_varint32(opts.width());
  return span.subspan(0, enc.length());
}

absl::StatusOr<Options> Options::Decode(absl::Span<const char> span) {
  ::Decoder dec(span.data(), span.size());
  uint32_t version;
  RET_CHECK(dec.get_varint32(&version));
  RET_CHECK(version >= 1 && version <= 3) << "Unsupported version: " << version;
  uint32_t indexes;
  RET_CHECK(dec.get_varint32(&indexes));
  uint32_t size;
  RET_CHECK(dec.get_varint32(&size));
  uint64_t max_value;
  RET_CHECK(dec.get_varint64(&max_value));
  uint32_t width;
  RET_CHECK(dec.get_varint32(&width));
  RET_CHECK_LE(width, kMaxWidth);
  RETURN_IF_ERROR(
      Options::CheckInvariants(OptimalLowBitWidth(size, max_value), max_value));
  Options opts(size, max_value);
  opts.override_width(width).indexes(indexes);
  opts.version_ = version;
  RETURN_IF_ERROR(Options::CheckInvariants(opts.width(), opts.max_value()));
  VLOG(1) << opts.DebugString();
  return opts;
}

}  // namespace elias_fano
