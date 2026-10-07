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

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_OPTIONS_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_OPTIONS_H_

#include <stddef.h>

#include <cstdint>
#include <string>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace elias_fano {

class Options {
 public:
  // The min bit width for the numbers in the low bits array.
  static constexpr uint8_t kMinWidth = 0;
  // The max bit width for the numbers in the low bits array.
  static constexpr uint8_t kMaxWidth = 56;
  // # bits per D1 entry.
  static constexpr uint32_t kD1 = 256;
  // # bits per D2 entry.
  static constexpr uint32_t kD2 = 32;
  // dense/sparse cutoff.
  static constexpr uint32_t kR = 1 << 16;
  // The minimum number of bits required to build any index (irrespective of
  // flags).
  // Note: Anything above a single word makes performance worse.
  static constexpr uint32_t kMaxBitsNoIndex = 64;

  enum Index {
    NONE,
    // If set, a skip table of ones will exist and IteratorAt() will be O(1).
    ONES = 0x1,
    // If set, a skip table of zeros will exist and UpperBound() will be O(1).
    ZEROS = 0x2,
    ALL = 0x3,
  };

  enum Version {
    VERSION3 = 3,
  };

  // size: the number of elements in an elias fano array.
  // max_value: the max value in an elias fano array.
  // By default both ONES and ZEROS indexes are built.
  ABSL_DEPRECATED("Use Encoder2: no options and easier to use")
  Options(uint32_t size, uint64_t max_value);
  // Overrides the automatically computed width in options. Useful for testing.
  ABSL_DEPRECATED("Use Encoder2: no options and easier to use")
  Options& override_width(uint8_t width);
  ABSL_DEPRECATED("Use Encoder2: no options and easier to use")
  Options& indexes(size_t index);
  ABSL_DEPRECATED("Use Encoder2: no options and easier to use")
  Options& version(uint8_t version);
  // The version of the encoding.
  uint8_t version() const { return version_; }
  // The number of elements in the encoding.
  uint32_t size() const { return size_; }
  uint64_t max_value() const { return max_value_; }
  // The number of one bits that will be in the elias fano unary stream.
  uint32_t one_bits() const { return one_bits_; }
  // The number of zero bits that will be in the elias fano unary stream.
  uint32_t zero_bits() const { return zero_bits_; }
  // The total number of bits in the elias fano unary stream.
  uint64_t bits() const { return uint64_t{one_bits()} + zero_bits(); }
  // The width of the low bit array.
  uint8_t width() const { return width_; }
  // The disjunction of all the indexes in this encoding.
  size_t index() const { return index_; }
  // Returns true if all indexes in indexes are present.
  bool has_index(int index) const { return (index_ & index) == index; }
  // The number of dense entries for ones.
  uint32_t one_dense_size() const {
    return has_index(ONES) ? iceil(one_bits(), kD1) : 0;
  }
  // The number of dense entries for zeros.
  uint32_t zero_dense_size() const {
    return has_index(ZEROS) ? iceil(zero_bits(), kD1) : 0;
  }
  // The max number of sparse entries.
  size_t max_sparse_size() const;
  // The number of bytes for high bits, excluding padding.
  size_t high_bytes() const { return iceil(bits() + 1 /* sentinel */, 8); }
  // The number of bytes for low bits, excluding padding.
  size_t low_bytes() const { return iceil(uint64_t{size()} * width(), 8); }

  std::string DebugString() const;

  // The bytes needed to encode the options.
  static size_t EncodingLength(const Options& opts);
  // Encodes `opts` and returns the span of the encoded bytes.
  //
  // REQUIRES:
  //   `span` has length greater than or equal to `EncodingLength(opts)`.
  static absl::Span<const char> Encode(const Options& opts,
                                       absl::Span<char> span);

  // Decodes `span` and returns the options.
  //
  // REQUIRES:
  //   `span` was encoded with `Options::Encode`.
  static absl::StatusOr<Options> Decode(absl::Span<const char> span);

 private:
  // Returns ceil(q / d).
  static uint64_t iceil(uint64_t q, uint64_t d) { return q / d + (q % d > 0); }

  absl::Status CheckInvariants() const;
  static absl::Status CheckInvariants(uint8_t width, uint64_t max_value);

  uint64_t max_value_;
  uint32_t size_;
  uint8_t version_ = 3;
  uint8_t width_;
  uint8_t index_;
  uint32_t one_bits_;
  uint32_t zero_bits_;
};

}  // namespace elias_fano

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_OPTIONS_H_
