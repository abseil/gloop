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

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_CODING_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_CODING_H_

// Efficient implementation of Elias-Fano encoded array, using succinct data
// structure to support fast select operations on a bitset.
//
// Provides an efficient way to store monotonic sequences of integers, while
// using very little amount of space and providing efficient forward and reverse
// lookups.
//
// Allows encoding up to 2^32-1 unsigned 64-bit values, in non-decreasing order.
//
// Supports 3 types of queries:
//   1) IteratorAt(i) - returns an iterator to the given index.
//   2) UpperBound(v) - for given value 'v' returns an iterator to the first
//                      value strictly greater than 'v'.
//   3) LowerBound(v) - for given value 'v' returns an iterator to the first
//                      value greater than or equal to 'v'.
//
// NOTATION:
//   M - max encoded value, N - number of values.
//
// HOW IT WORKS:
//   For each entry, we define low bits (floor(log(M/N)) bits) and high bits
//   (the rest). For each entry, high bits are stored in a bitmap, in a
//   following manner: if i-th entry's high bits equal to H, store one in bitmap
//   on position i + H. There will be no collisions, as H is weakly monotonic
//   and i is strictly monotonic. This way, i-th one in the bitset will
//   correspond to the i-th entry. Low bits on the other hand, are stored
//   explicitly in a flat array.
//
//   Therefore, to Decode i-th value we read low bits directly, and recover the
//   high bits of i-th entry by finding the index of the i-th one in the bitset.
//   Computed position - i gives us the high bits value (as i + H = position).
//
//   To get the UpperBound V, we find high(V)-th zero (each zero means an
//   increase in high value by one) and count number of ones by that point. We
//   do the same for high(V)-1 and run binary search for found range.
//
// MEMORY CONSUMPTION:
//   Note that unary stream has length at most N + N * 2^a, where 0 <= a < 1.
//   Specifically, the last position with one bit in unary stream is:
//
//     N + M / 2^floor(log(M/N)) = 2 * N if M/N is power of 2.
//     N + M / 2^floor(log(M/N)) = N + N * 2^a if M/N is not power of two,
//       where floor(log(M/N)) = log(M/N) - a, 0 <= a < 1.
//
//   Thus, we have N one bits and N * 2^a zero bits, where 0 <= a < 1.
//
//   For the version supporting random access and lower/upper bound, the
//   structure takes (at most):
//
//     N * (floor(log(M / N)) + 1 + 2^a + 0.875) bits =
//     N * (log(M / N) - a + 1 + 2^a + 0.875) bits <=
//     N * (log(M / N) + 2 + 0.875) bits,
//       since 2^a - a <= 1.
//
//   For the version supporting only random access or lower/upper bound, the
//   factor of 0.875 should roughly half, hitting a value between (0.33, 0.53).
//
// BENCHMARKS:
//
// Results are given as time in [ns] for a single query.
//
// 1) Uniformly distributed numbers.
//
//                    Number of entries (N)
//      Operation |  32  | 256  |  64k |  1M  |  16M |
//     -----------+------+------+------+------+------+
//     IteratorAt |   2  |   5  |   5  |   7  |  12  |
//     -----------+------+------+------+------+------+
//     UpperBound |  15  |  21  |  22  |  33  |  65  |
//     -----------+------+------+------+------+------+
//
// 2) Zipf distributed numbers.
//
//                    Number of entries (N)
//      Operation |  32  | 256  |  64k |  1M  |  16M |
//     -----------+------+------+------+------+------+
//     IteratorAt |   2  |   4  |   4  |   5  |  11  |
//     -----------+------+------+------+------+------+
//     UpperBound |   7  |  11  |  11  |  13  |  26  |
//     -----------+------+------+------+------+------+
//
// INDEX IMPLEMENTATION
//
// In order to efficiently find the position of the i-th 0 or 1 in the bitset,
// we implemented and improved upon the following index which was described in
// papers:
//   http://vigna.di.unimi.it/ftp/papers/Broadword.pdf
//   http://arxiv.org/pdf/cs/0610001v1.pdf
//
// For given bit stream (max 2^32 zeros + 2^32 ones), supports rank operations
// on 0's and 1's.
//
// Parameters work best for given bit streams with equal number of 0's and 1's.
//
// HOW IT WORKS:
//
// For the purpose of this discussion assume we are building an index on 1's.
// Building an index on 0's is the same as building an index over the negated
// bit stream.
//
// Definitions
//
// N: number of bits in bit stream
// k: number of 1's in bit stream
//
// Given the bitstream we want to encode the positions of each Nth 1 in it
// (rank). To do this we store 2 indexes:
//
// - dense index
// - sparse index
//
// The dense index is a 2-level index. For each D1 1's it stores the position,
// followed by D1 / D2 deltas for each subsequent D2 1's. For our purposes,
// D1 = 256 and D2 = 32, so an entry in the dense index will look like this:
//
//   uint32 base | uint16 delta1 | uint16 delta2 | ... | uint16 delta8
//
// The base is encoded as pos - i * D1. This always works because for any bit
// we know that rank <= pos. Since i * D1 <= rank, i * D1 <= pos as well. Thus
// we conclude pos - i * D1 >= 0 which can be encoded as an unsigned int.
//
// The max position we can encode is 2^32-1 + #(1's).
//
// The total memory used by the dense index is:
//
//   size(D1) + size(D2)
//
//   size(D1) = k / D1 * 32 bits
//
//   size(D2) = k / D2 * 16 bits
//
// This index cannot encode D1 positions that are further apart than R = 2^16
// bits (from D1). This is because the D2 deltas are encoded with 16 bits.
//
// When such positions are encountered, we use the sparse index. When we encode
// in the sparse index we know that for each R bits of the bit stream, we will
// have at most D1 1's.
//
// As such the sparse index, consists of D1 positions for each R bits of the
// bit stream. Since the size of unary stream is at most 3 * k, it's max memory
// usage is:
//
//   size(S) = N / R * D1 * 32 bits <= 3 * k / R * D1 * 32 bits
//
// We encode sparse index in a variable sized array. When the sparse index is
// used we store the offset into this array in the pos of the dense index which
// is otherwise unused.
//
// Performance
//
// It is important to realize why this index is split into dense and sparse
// indexes. After an index query, broadword search is performed over the bit
// stream. In places where the bit stream is dense, broadword search is fast
// so our dense index does not encode each 1's position. In places where the
// bit stream is sparse, the sparse index encodes all bit positions because
// broadword search is not going to be very fast since it will need to scan
// a lot of words before it finds the rank of the specified 1.
//
// 1. Dense Index Performance.
// Querying dense index can perform up to R/64 = 1024 = O(1) uint64 word reads
// in the worst case. Although 1024 is a constant, it is quite large and might
// affect performance in corner cases. Consider an example.
// Values 0, R-D2, R-D2, ....., where R-D2 is repeated R-D2 times.Then M = R-D2,
// and N = M + 1. Thus, we encode no low bits. All variables are encoded as high
// bits in unary stream. Here is the high bits of first D1 group:
// |--------------D1------------------------------------------------------------
// |------------------D2--------------------||-----------next D2----------------
// 10....................01.................11..................................
//  |----(R-D2 0 bits)---||---(D2 1 bits)---||---trailing N-(D2 + 1) 1 bits-----
// Thus, for queries that search positions in first D2 of first D1, we
// need to read at least (R-D2)/64 words.
//
//
// 2. Sparse Index Performance.
// Query of sparse index reads 2 (O(1)) words.
//
// Total memory consumption
//
// Denote k1 the number of 1's in the bitstream and k0 the number of 0's. Then
// we can derive the total amount of memory used by an index over both 0's and
// 1's given D1 = 256, D2 = 32, R = 2^16:
//
//   size(Index0) + size(Index1) = 0.875 * N bits
//
// Benchmarks
//
//                    Number of entries (N)
//   Distribution |  32  | 256  |  64k |  1M  |  16M |
//   -------------+------+------+------+------+------+
//        Uniform |   2  |   5  |   5  |   7  |  11  |
//   -------------+------+------+------+------+------+
//           Zipf |   1  |   4  |   4  |   5  |  10  |
//   -------------+------+------+------+------+------+
//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/base/prefetch.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/coding/coder.h"
#include "gloop/util/coding/elias_fano/nthbit.h"
#include "gloop/util/coding/elias_fano/options.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/gtl/unaligned.h"
#include "gloop/util/intops/strong_int.h"

// [[clang::musttail]] is only enabled for archs that support it and only in
// debug builds. The reason for debug builds only is because the tail calls are
// not per se required, they are nice to have from an optimization perspective.
// Enabling in debug builds means we can be sure the calls can be made into
// tailcalls which the compiler will happily do when the opportunity arizes (if
// say the function of the callsite is not inlined which will subsequently
// change the function signature of the caller).
#if __has_cpp_attribute(clang::musttail) && !defined(__wasm__) && \
    !defined(NDEBUG) && (defined(__aarch64__) || defined(__x86_64__))
#define ELIAS_FANO_MUSTTAIL [[clang::musttail]]
#else
#define ELIAS_FANO_MUSTTAIL
#endif

namespace elias_fano {

DEFINE_STRONG_INT_TYPE(Index, uint32_t);
DEFINE_STRONG_INT_TYPE(BitPos, uint64_t);

namespace internal {

struct DenseEntry {
  gtl::Unaligned<uint32_t> base;
  gtl::Unaligned<uint16_t> delta[Options::kD1 / Options::kD2];
};

using SparseEntry = gtl::Unaligned<uint32_t>;

}  // namespace internal

namespace testing {
struct CodecAccess;
}  // namespace testing

// Returns the encoded bytes of the first elias fano array in the span. If the
// span is not an encoded elias-fano array it returns 0.
size_t EncodedSize(absl::Span<const char> span);

struct Info {
  Options opts{0, 0};
  size_t max_bytes;
  size_t actual_bytes;
  size_t meta_bytes;
  size_t high_bytes;
  size_t low_bytes;
  size_t dense_index_bytes;
  size_t sparse_index_bytes;

  std::string DebugString() const;
};

// Returns info about the first elias fano array in the span.
absl::StatusOr<Info> GetInfo(absl::Span<const char> span);

class Encoder {
 public:
  // Returns the min/max possible encoding bytes for an elias-fano encoding
  // specified by the options.
  static size_t MinEncodingBytes(const Options& opts);
  static size_t MaxEncodingBytes(const Options& opts);

  // Creates encoder with specified options. Encoded elias-fano will be encoded
  // in the span.
  //
  // The memory pointed to by the span must outlive the encoder.
  Encoder(const Options& opts, absl::Span<char> span);

  // Appends value.
  //
  // REQUIRES:
  //   Values are given in non-decreasing order.
  //   v <= max_value
  //   No more than opts.size() values are given.
  void Append(uint64_t v);

  // Finalizes the encoding and returns the actual bytes of the encoding.
  //
  // REQUIRES:
  //   Last value fed through Append is equal to opts.max_value().
  //   Append was called exactly opts.size() times.
  absl::StatusOr<size_t> Finalize();

  const Options& options() const { return opts_; }

  // Returns the number of appended values.
  uint32_t size() const { return size_; }

 private:
  // Poisons high and low bits when running under ASAN.
  class HighLowBitsGuard;

  // Encodes up to 56 bit wide value into the low bits array.
  void Encode56(Index i, uint64_t value);
  // Returns the value of the bit at position pos.
  bool GetBit(BitPos pos) const;
  // Sets the bit at position pos to one.
  void SetBit(BitPos pos);
  // Sets the bit at position pos to zero.
  void UnsetBit(BitPos pos);
  // Builds an index on ones or zeros based on bit. Returns the new sparse_idx.
  template <bool One>
  absl::Status BuildIndex(internal::DenseEntry* dense, uint32_t* sparse_size);
  // Writes deltas in dense entry or sparse index. Returns the new sparse_idx.
  absl::Status WriteDeltas(BitPos base, const std::vector<uint64_t>& deltas,
                           internal::DenseEntry* dense, uint32_t dense_idx,
                           uint32_t* sparse_size);

  friend struct testing::CodecAccess;

  Options opts_;
  uint64_t low_mask_;
  uint64_t last_value_ = 0;
  size_t actual_bytes_ = 0;
  internal::DenseEntry* dense1_;
  internal::DenseEntry* dense0_;
  uint32_t size_ = 0;
  uint8_t* high_;
  uint8_t* low_;
  internal::SparseEntry* sparse_;
};

class Encoder2 {
 public:
  static constexpr size_t kDefaultMaxWastedSpace = 1024;

  explicit Encoder2(Options::Index index = Options::Index::ALL,
                    Options::Version version = Options::VERSION3);

  void Reset();

  // Returns false if values are not added in increasing order.
  ABSL_MUST_USE_RESULT bool Append(uint64_t value) {
    return Append({&value, 1});
  }
  ABSL_MUST_USE_RESULT bool Append(absl::Span<const uint64_t> values);

  ABSL_MUST_USE_RESULT absl::string_view Finalize(
      size_t max_wasted_space = kDefaultMaxWastedSpace) {
    auto encoded_or = FinalizeToStr(max_wasted_space);
    CHECK_OK(encoded_or);
    encoded_ = *std::move(encoded_or);
    return encoded_;
  }
  absl::StatusOr<std::string> FinalizeToStr(
      size_t max_wasted_space = kDefaultMaxWastedSpace);

  // Returns the number of appended values.
  uint32_t size() const { return num_vals_; }

  // Returns the maximum among the appended values.
  uint64_t max_value() const { return max_val_; }

  // Returns the max possible encoding for an elias-fano encoding specified by
  // the input properties.
  static size_t MaxEncodingBytes(uint32_t size, uint64_t max_value,
                                 Options::Index index = Options::Index::ALL,
                                 Options::Version version = Options::VERSION3);

  std::string DebugString() const;

 private:
  ::Encoder vals_enc_;
  Options::Version version_;
  Options::Index index_;
  uint32_t num_vals_ = 0;
  uint64_t min_delta_ = std::numeric_limits<uint64_t>::max();
  uint64_t max_val_ = 0;
  std::string encoded_;
};

class Decoder {
 public:
  class iterator {
   public:
    using difference_type = ptrdiff_t;
    using value_type = uint64_t;
    using pointer = void;
    using reference = uint64_t;
    using iterator_category = std::forward_iterator_tag;

    iterator() {}

    // Returns the index of the element this iterator points to. When the
    // iterator is the end iterator the index is the size of the array.
    uint32_t index() const { return index_.value(); }
    iterator& operator++() {
      // TODO: Read word from unary stream and store it in the iterator.
      // Then clear the LSB on every increment (w &= w - 1). If the word becomes
      // zero, read the next one, etc. To keep track of pos we can count
      // trailing zeros.
      ++index_;
      pos_ = decoder_->FindNextSetBit(pos_ + BitPos{1});
      return *this;
    }
    iterator operator++(int) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }
    value_type operator*() const {
      auto hi = pos_.value() - index_.value();
      auto width = decoder_->opts_.width();
      if (width == 0) return hi;
      return (hi << width) | decoder_->Decode56(index_);
    }
    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }
    bool operator!=(const iterator& other) const { return !(*this == other); }

   private:
    iterator(const Decoder* decoder, Index i, BitPos pos)
        : decoder_(decoder), index_(i), pos_(pos) {}

    const Decoder* decoder_;
    Index index_;  // index into low bit array (aka rank)
    BitPos pos_;   // bit index into high bit array
    friend class Decoder;
  };

  using value_type = uint64_t;
  using size_type = uint32_t;
  using const_iterator = iterator;
  using reference = typename iterator::reference;
  using const_reference = typename const_iterator::reference;
  using pointer = typename iterator::pointer;
  using const_pointer = typename const_iterator::pointer;

  // The memory block should contain data encoded with Encoder. The Decoder
  // class holds several pointers to that buffer. Hence, the buffer must
  // outlive the returned Decoder, and the pointers to it must not be
  // invalidated. Remember that moving a contiguous STL container
  // (e.g. std::string, but also std::vector) generally invalidates pointers
  // to that container.
  static absl::StatusOr<Decoder> Make(absl::Span<const char> span);

  // Returns an iterator to the element at position i.
  iterator IteratorAt(uint32_t i) const {
    DCHECK_LT(i, opts_.size());
    return iterator(this, Index(i), Select(1, i, false).first);
  }

  // Decodes the pair of adjacent elements `(at(i), at(i + 1))` using a single
  // high-bit select and (for width <= 28) a single 64-bit low-bit load.
  // REQUIRES: i + 1 < size().
  ABSL_ATTRIBUTE_ALWAYS_INLINE std::pair<uint64_t, uint64_t> DecodePair(
      uint32_t i) const {
    DCHECK_LT(i + 1, opts_.size());
    const uint8_t width = opts_.width();
    const auto [pos0, pos1] = Select1AdjacentPair(i);
    const uint64_t hi0 = pos0 - i;
    const uint64_t hi1 = pos1 - (i + 1);
    if (width == 0) return {hi0, hi1};
    if (ABSL_PREDICT_TRUE(width <= 28)) {
      const size_t low_bit_pos = size_t{i} * width;
      const uint64_t mask = (uint64_t{1} << width) - 1;
      const uint64_t raw =
          LittleEndian::Load64(&low_[low_bit_pos >> 3]) >> (low_bit_pos & 7u);
      return {(hi0 << width) | (raw & mask),
              (hi1 << width) | ((raw >> width) & mask)};
    }
    return {(hi0 << width) | Decode56(Index(i)),
            (hi1 << width) | Decode56(Index(i + 1))};
  }

  // Invokes `emit(idx, at(idx), at(idx + 1))` for each `idx` in `indices` with
  // `idx < limit_index` (out-of-range indices are skipped). Uses a 3-stage
  // software-pipelined prefetch ring (`dense1_`/`low_` -> `high_` -> decode)
  // combined with a forward bit-word cursor for sorted inputs.
  // REQUIRES: limit_index < size().
  template <typename EmitFn>
  void ForEachAdjacentPair(absl::Span<const uint32_t> indices,
                           uint32_t limit_index, EmitFn&& emit) const {
    if (indices.empty() || limit_index == 0) return;
    DCHECK_LT(limit_index, opts_.size());
    if (nthbit_internal::HasFastPdep()) {
      ForEachAdjacentPairPdep<true>(indices, limit_index, emit);
    } else {
      ForEachAdjacentPairPdep<false>(indices, limit_index, emit);
    }
  }

  struct UpperBoundResult {
    // First index with `at(index) > v`, or `size()` if `v >= max_value()`.
    uint32_t index;
    // Value `at(index)` when `index < size()`, or `max_value()` otherwise.
    uint64_t value_at_index;
  };

  // Computes `UpperBound(v)` together with `at(UpperBound(v))` using only the
  // ZEROS select pass (avoiding a second ONES `Select` call).
  UpperBoundResult UpperBoundWithValue(uint64_t v) const {
    if (ABSL_PREDICT_FALSE(opts_.max_value() <= v)) {
      return {opts_.size(), opts_.max_value()};
    }
    const uint8_t width = opts_.width();
    if (width == 0) {
      const uint32_t hi = static_cast<uint32_t>(v);
      const uint64_t z_curr = Select(0, hi, false).first.value();
      const uint32_t idx = static_cast<uint32_t>(z_curr - hi);
      const uint64_t next_one = NextOneAfterZero(z_curr, idx);
      return {idx, next_one - idx};
    }
    const uint32_t hi = static_cast<uint32_t>(v >> width);
    const uint64_t low_mask = (uint64_t{1} << width) - 1;
    const auto [start, limit, z_curr] = ResolveZeroBucket(hi);
    return FinishUpperBoundInBucket(hi, v & low_mask, width, low_mask, start,
                                    limit, z_curr);
  }

  // For sorted `values`, calls `emit(idx - 1)` for each distinct positive
  // `idx = UpperBound(v)` among elements with `v < domain_limit`
  // (`domain_limit <= max_value()`), using a 4-stage software-pipelined
  // `dense0_ -> high_ -> low_ -> search` loop plus interval/bucket skipping.
  template <typename EmitFn>
  void ForEachUniqueUpperBoundMinusOne(absl::Span<const uint32_t> values,
                                       uint64_t domain_limit,
                                       EmitFn&& emit) const {
    const uint64_t cap = std::min(domain_limit, opts_.max_value());
    if (values.empty() || cap == 0 || opts_.size() <= 1) return;
    if (nthbit_internal::HasFastPdep()) {
      UpperBoundBatchImpl<true, /*kUniqueOnly=*/true>(values, cap, 0, emit);
    } else {
      UpperBoundBatchImpl<false, /*kUniqueOnly=*/true>(values, cap, 0, emit);
    }
  }

  // Writes `(idx > 0 ? idx - 1 : invalid_value)` where `idx = UpperBound(v)`
  // for each `v` in `values` (writing `invalid_value` when `v >=
  // domain_limit`).
  void UpperBoundMinusOneBatch(absl::Span<const uint32_t> values,
                               uint64_t domain_limit, uint32_t invalid_value,
                               uint32_t* out) const {
    if (values.empty()) return;
    const uint64_t cap = std::min(domain_limit, opts_.max_value());
    if (ABSL_PREDICT_FALSE(cap == 0 || opts_.size() <= 1)) {
      for (size_t i = 0; i < values.size(); ++i) out[i] = invalid_value;
      return;
    }
    auto emit = [&out](uint32_t mapped) { *out++ = mapped; };
    if (nthbit_internal::HasFastPdep()) {
      UpperBoundBatchImpl<true, /*kUniqueOnly=*/false>(values, cap,
                                                       invalid_value, emit);
    } else {
      UpperBoundBatchImpl<false, /*kUniqueOnly=*/false>(values, cap,
                                                        invalid_value, emit);
    }
  }

  // Returns the index of the first element with value > v.
  uint32_t UpperBound(uint64_t v) const {
    if (ABSL_PREDICT_FALSE(opts_.max_value() <= v)) return opts_.size();
    uint8_t width = opts_.width();
    if (width == 0) return HighUpperBound(v).value();
    uint32_t hi = v >> width;
    Index start, limit;
    if (hi % Options::kD1 != 0) {
      // We hit the same index position, so we can fetch both values together.
      std::tie(start, limit) = HighUpperBoundRange(hi);
    } else {
      start = (hi == 0) ? Index(0) : HighUpperBound(hi - 1);
      limit = HighUpperBound(hi);
    }
    uint64_t lo = LowBits(v, width);
    // Note: branchless binary search. There is no guarantee std::upper_bound is
    // the same and performance is crucial.
    auto len = limit - start;
    if (len == Index{0}) return start.value();
    while (len > Index{1}) {
      auto half = len / 2;
      auto mid = start + half;
      start = lo < Decode56(mid) ? start : mid;
      len -= half;
    }
    return start.value() + (lo >= Decode56(start));
  }

  // Returns the index of the first element with value >= v.
  uint32_t LowerBound(uint64_t v) const {
    if (ABSL_PREDICT_FALSE(opts_.max_value() < v)) return opts_.size();
    uint8_t width = opts_.width();
    uint32_t hi = v >> width;
    Index start, limit;
    if (hi % Options::kD1 != 0) {
      // We hit the same index position, so we can fetch both values together.
      std::tie(start, limit) = HighUpperBoundRange(hi);
    } else {
      start = (hi == 0) ? Index(0) : HighUpperBound(hi - 1);
      limit = HighUpperBound(hi);
    }
    uint64_t lo = LowBits(v, width);
    // Note: branchless binary search. There is no guarantee std::lower_bound is
    // the same and performance is crucial.
    auto len = limit - start;
    if (len == Index{0}) return start.value();
    while (len > Index{1}) {
      auto half = len / 2;
      auto mid = start + half;
      start = lo <= Decode56(mid) ? start : mid;
      len -= half;
    }
    return start.value() + (lo > Decode56(start));
  }

  bool Exists(uint64_t v) const {
    if (ABSL_PREDICT_FALSE(opts_.max_value() < v)) return false;
    uint8_t width = opts_.width();
    uint32_t hi = v >> width;
    Index start, limit;
    if (hi % Options::kD1 != 0) {
      // We hit the same index position, so we can fetch both values together.
      std::tie(start, limit) = HighUpperBoundRange(hi);
    } else {
      start = (hi == 0) ? Index(0) : HighUpperBound(hi - 1);
      limit = HighUpperBound(hi);
    }
    uint64_t lo = LowBits(v, width);
    // Note: branchless binary search. There is no guarantee std::binary_search
    // is the same and performance is crucial.
    auto len = limit - start;
    if (len == Index{0}) return false;
    while (len > Index{1}) {
      auto half = len / 2;
      auto mid = start + half;
      start = lo < Decode56(mid) ? start : mid;
      len -= half;
    }
    return lo == Decode56(start);
  }

  iterator begin() const {
    return iterator(this, Index(0), FindNextSetBit(BitPos(0)));
  }
  iterator end() const {
    return iterator(this, Index(opts_.size()), BitPos(opts_.bits()));
  }

  // Returns the size of the Elias-Fano array.
  uint32_t size() const { return opts_.size(); }

  const Options& options() const { return opts_; }

  absl::Span<const char> RawBytes() const;

 private:
  Decoder(absl::Span<const char> span, Options opts);

  static uint64_t LowBits(uint64_t x, int width) {
#ifdef __BMI2__
    return _bzhi_u64(x, width);
#else
    return width == 0 ? 0 : (x & (~0ull >> (64 - width)));
#endif
  }

  // Searches the high bits array for the first set bit, starting at position
  // pos.
  //
  // REQUIRES: pos <= #bits.
  BitPos FindNextSetBit(BitPos pos) const {
    uint32_t i = pos.value() / 8;
    uint64_t x = LittleEndian::Load64(&high_[i]) >> (pos.value() % 8);
    if (x != 0) return BitPos(pos.value() + absl::countr_zero(x));
    return FindNextSetBitSlow(i, true);
  }

  // Starts the search from position `i + 8` in the high bits.
  ABSL_ATTRIBUTE_NOINLINE BitPos FindNextSetBitSlow(uint32_t i,
                                                    bool one) const {
    uint64_t x;
    do {
      i += 8;
      x = one ? LittleEndian::Load64(&high_[i])
              : ~LittleEndian::Load64(&high_[i]);
    } while (x == 0);
    return BitPos{uint64_t{i} * 8 + absl::countr_zero(x)};
  }

  // Decodes up to 56 bit wide value from the low bits array.
  uint64_t Decode56(Index i) const {
    if (opts_.width() == 0) return 0;
    size_t pos = size_t{i.value()} * opts_.width();
    const uint8_t* ptr = &low_[pos / 8];
    uint64_t word = LittleEndian::Load64(ptr);
    return LowBits(word >> (pos % 8), opts_.width());
  }

  Index HighUpperBound(uint32_t hi) const {
    if (ABSL_PREDICT_FALSE(hi >= opts_.zero_bits())) return Index(opts_.size());
    BitPos pos = Select(0, hi, false).first;
    return Index(pos.value() - hi);
  }

  std::pair<Index, Index> HighUpperBoundRange(uint32_t hi) const {
    DCHECK_NE(hi % Options::kD1, 0U)
        << "Can't find two consecutive positions at the end of the index";
    if (ABSL_PREDICT_FALSE(hi >= opts_.zero_bits())) {
      return {HighUpperBound(hi - 1), Index(opts_.size())};
    } else {
      auto [bit_pos, next_bit_pos] = Select(0, hi - 1, true);
      return {Index{bit_pos.value() - hi + 1},
              Index{next_bit_pos.value() - hi}};
    }
  }

  // Returns `[pos, npos]` the indexes of the rank-th and rank+1'th bits.
  // `npos` is valid iff next is `true`.
  // Searches ones if `one` is non-zero, zeros otherwise.
  //
  // REQUIRES: if `next` is true, the rank+1'th bit needs to be in the
  // same D1 index range.
  //
  // NOTE: The signature of this function is weird to facilitate tail calls.
  std::pair<BitPos, BitPos> Select(uint32_t one, uint32_t rank, bool next,
                                   BitPos hint_pos = BitPos{0}) const {
    if (!opts_.has_index(one ? Options::ONES : Options::ZEROS)) {
      ELIAS_FANO_MUSTTAIL return BroadWordSearch(one, rank, next, hint_pos);
    }
    const internal::DenseEntry* dense = one ? dense1_ : dense0_;
    DCHECK(!next || rank % Options::kD1 != Options::kD1 - 1)
        << "Can't find two consecutive positions at the end of the index";
    uint32_t d1_i = rank / Options::kD1;
    const auto& entry = dense[d1_i];
    // Load `base` (bytes 0..3) and `delta[0]` (bytes 4..5) together.
    const uint64_t header = LittleEndian::Load64(&entry);
    uint64_t base = static_cast<uint32_t>(header);
    // If the first delta is zero then the position is in the dense index.
    // Otherwise it is in the sparse.
    if (ABSL_PREDICT_TRUE(((header >> 32) & 0xffffu) == 0)) {
      base += d1_i * Options::kD1;
      uint32_t d2_i = (rank % Options::kD1) / Options::kD2;
      ELIAS_FANO_MUSTTAIL return BroadWordSearch(
          one, rank % Options::kD2, next,
          BitPos(base + entry.delta[d2_i].Load()));
    } else {
      DCHECK_LE(base, std::numeric_limits<uint32_t>::max());
      ELIAS_FANO_MUSTTAIL return SelectSparse(base, rank, next, hint_pos);
    }
  }

  // Returns `[pos, npos]` the indexes of the rank-th and rank+1'th bits.
  // `npos` is valid iff next is `true`.
  //
  // NOTE: The signature of this function is weird to facilitate tail calls.
  ABSL_ATTRIBUTE_NOINLINE std::pair<BitPos, BitPos> SelectSparse(
      uint32_t base, uint32_t rank, bool next, BitPos hint_pos) const;

  // Returns `[pos, npos]` the indexes of the rank-th and rank+1'th bits.
  // `npos` is valid iff next is `true`.
  // Searches ones if `one` is non-zero, zeros otherwise.
  std::pair<BitPos, BitPos> BroadWordSearch(uint32_t one, uint32_t rank,
                                            bool next, BitPos hint_pos) const {
    uint32_t i = static_cast<uint32_t>(hint_pos.value() >> 3);
    const uint64_t flip = one ? uint64_t{0} : ~uint64_t{0};
    uint64_t x = (LittleEndian::Load64(&high_[i]) ^ flip) &
                 (~uint64_t{0} << (hint_pos.value() & 7u));
    // Prefetch the next line when starting near the end of a cache line.
    if ((i & (ABSL_CACHELINE_SIZE - 1)) >= ABSL_CACHELINE_SIZE - 16) {
      absl::PrefetchToLocalCache(&high_[i] + ABSL_CACHELINE_SIZE);
    }
    uint32_t cnt = absl::popcount(x);
    while (cnt <= rank) {
      i += 8;
      rank -= cnt;
      x = LittleEndian::Load64(&high_[i]) ^ flip;
      cnt = absl::popcount(x);
    }
    const uint64_t word_base = uint64_t{i} * 8;
    if (!next) {
      return {BitPos{word_base + Nthbit(x, rank)}, BitPos{0}};
    }
    const auto [b0, after] = NthbitAndAfter(x, rank);
    const BitPos pos{word_base + b0};
    if (ABSL_PREDICT_TRUE(after != 0)) {
      return {pos, BitPos{word_base + absl::countr_zero(after)}};
    }
    return {pos, FindNextSetBitSlow(i, one)};
  }

  static constexpr uint64_t kSparseHintFlag = uint64_t{1} << 63;
  static constexpr uint64_t kZeroHiHintFlag = uint64_t{1} << 62;

  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t DecodeLowFast(uint32_t i, uint8_t width,
                                                      uint64_t low_mask) const {
    const size_t pos = size_t{i} * width;
    return (LittleEndian::Load64(&low_[pos >> 3]) >> (pos & 7u)) & low_mask;
  }

  // Returns bit positions of `i`-th and `(i+1)`-th one bits (`i + 1 < size()`),
  // seamlessly handling D1 boundaries and sparse blocks.
  ABSL_ATTRIBUTE_ALWAYS_INLINE std::pair<uint64_t, uint64_t>
  Select1AdjacentPair(uint32_t i) const {
    uint64_t hint_pos = 0;
    uint32_t r = i;
    if (ABSL_PREDICT_TRUE(opts_.has_index(Options::ONES))) {
      const uint32_t d1 = i >> 8;
      const auto& entry = dense1_[d1];
      const uint64_t hdr = LittleEndian::Load64(&entry);
      const uint32_t base = static_cast<uint32_t>(hdr);
      if (ABSL_PREDICT_FALSE(((hdr >> 32) & 0xffffu) != 0)) {
        return Select1AdjacentPairSparse(base, i);
      }
      hint_pos = uint64_t{base} + (uint64_t{d1} << 8) +
                 entry.delta[(i >> 5) & 7u].Load();
      r = i & 31u;
    }
    uint32_t byte_idx = static_cast<uint32_t>(hint_pos >> 3);
    uint64_t x = LittleEndian::Load64(&high_[byte_idx]) &
                 (~uint64_t{0} << (hint_pos & 7u));
    uint32_t cnt = absl::popcount(x);
    while (ABSL_PREDICT_FALSE(cnt <= r)) {
      byte_idx += 8;
      r -= cnt;
      x = LittleEndian::Load64(&high_[byte_idx]);
      cnt = absl::popcount(x);
    }
    const auto [b0, after] = NthbitAndAfter(x, r);
    const uint64_t pos0 = uint64_t{byte_idx} * 8 + b0;
    if (ABSL_PREDICT_TRUE(after != 0)) {
      return {pos0, uint64_t{byte_idx} * 8 + absl::countr_zero(after)};
    }
    return {pos0, NextOneAfterWordSlow(byte_idx, i + 1)};
  }

  ABSL_ATTRIBUTE_NOINLINE std::pair<uint64_t, uint64_t>
  Select1AdjacentPairSparse(uint32_t base, uint32_t i) const {
    if (ABSL_PREDICT_TRUE((i & 255u) != 255u)) {
      const auto [p0, p1] = SelectSparse(base, i, true, BitPos{0});
      return {p0.value(), p1.value()};
    }
    return {SelectSparse(base, i, false, BitPos{0}).first.value(),
            Select(1, i + 1, false).first.value()};
  }

  ABSL_ATTRIBUTE_NOINLINE uint64_t
  NextOneAfterWordSlow(uint32_t byte_idx, uint32_t next_rank) const {
    uint32_t nb = byte_idx + 8;
    uint64_t nx = LittleEndian::Load64(&high_[nb]);
    if (ABSL_PREDICT_TRUE(nx != 0)) {
      return uint64_t{nb} * 8 + absl::countr_zero(nx);
    }
    if ((next_rank & 255u) == 0 && opts_.has_index(Options::ONES)) {
      return Select(1, next_rank, false).first.value();
    }
    do {
      nb += 8;
      nx = LittleEndian::Load64(&high_[nb]);
    } while (nx == 0);
    return uint64_t{nb} * 8 + absl::countr_zero(nx);
  }

  // Advances from an exhausted word at `byte_idx` to the `next_rank`-th one
  // bit, returning `{pos1, word_byte_idx, word_masked_from_pos1}` with non-zero
  // masked word.
  ABSL_ATTRIBUTE_NOINLINE std::tuple<uint64_t, uint32_t, uint64_t>
  NextOneWordSlow(uint32_t byte_idx, uint32_t next_rank) const {
    uint32_t nb = byte_idx + 8;
    uint64_t nx = LittleEndian::Load64(&high_[nb]);
    if (ABSL_PREDICT_TRUE(nx != 0)) {
      return {uint64_t{nb} * 8 + absl::countr_zero(nx), nb, nx};
    }
    if ((next_rank & 255u) == 0 && opts_.has_index(Options::ONES)) {
      const uint64_t p1 = Select(1, next_rank, false).first.value();
      const uint32_t pb = static_cast<uint32_t>(p1 >> 3);
      const uint64_t px =
          LittleEndian::Load64(&high_[pb]) & (~uint64_t{0} << (p1 & 7u));
      return {p1, pb, px};
    }
    do {
      nb += 8;
      nx = LittleEndian::Load64(&high_[nb]);
    } while (nx == 0);
    return {uint64_t{nb} * 8 + absl::countr_zero(nx), nb, nx};
  }

  enum class WidthMode : uint8_t { kZero, kSmall, kLarge };

  template <bool kFastPdep, typename EmitFn>
  void ForEachAdjacentPairPdep(absl::Span<const uint32_t> indices,
                               uint32_t limit_index, EmitFn& emit) const {
    const uint8_t width = opts_.width();
    if (width == 0) {
      ForEachAdjacentPairImpl<kFastPdep, WidthMode::kZero>(indices, limit_index,
                                                           emit);
    } else if (ABSL_PREDICT_TRUE(width <= 28)) {
      ForEachAdjacentPairImpl<kFastPdep, WidthMode::kSmall>(indices,
                                                            limit_index, emit);
    } else {
      ForEachAdjacentPairImpl<kFastPdep, WidthMode::kLarge>(indices,
                                                            limit_index, emit);
    }
  }

  template <bool kFastPdep>
  ABSL_ATTRIBUTE_ALWAYS_INLINE std::pair<uint64_t, uint64_t>
  DecodeHighPairFromHint(const uint8_t* high, uint32_t i, uint64_t hint) const {
    if (ABSL_PREDICT_FALSE((hint & kSparseHintFlag) != 0)) {
      return Select1AdjacentPairSparse(static_cast<uint32_t>(hint), i);
    }
    uint32_t byte_idx = static_cast<uint32_t>(hint >> 3);
    uint32_t r = i & 31u;
    uint64_t x =
        LittleEndian::Load64(&high[byte_idx]) & (~uint64_t{0} << (hint & 7u));
    uint32_t cnt = static_cast<uint32_t>(absl::popcount(x));
    while (cnt <= r) {
      byte_idx += 8;
      r -= cnt;
      x = LittleEndian::Load64(&high[byte_idx]);
      cnt = static_cast<uint32_t>(absl::popcount(x));
    }
    const auto [b0, after] =
        nthbit_internal::NthbitAndAfterDispatch<kFastPdep>(x, r);
    const uint64_t pos0 = uint64_t{byte_idx} * 8 + b0;
    if (ABSL_PREDICT_TRUE(after != 0)) {
      return {pos0, uint64_t{byte_idx} * 8 + absl::countr_zero(after)};
    }
    const uint64_t nx = LittleEndian::Load64(&high[byte_idx + 8]);
    if (ABSL_PREDICT_TRUE(nx != 0)) {
      return {pos0, uint64_t{byte_idx + 8} * 8 + absl::countr_zero(nx)};
    }
    return {pos0, NextOneAfterWordSlow(byte_idx + 8, i + 1)};
  }

  template <WidthMode kWMode, typename EmitFn>
  ABSL_ATTRIBUTE_ALWAYS_INLINE void EmitFromHighPair(
      const uint8_t* low, uint8_t width, uint64_t low_mask, uint32_t i,
      uint64_t pos0, uint64_t pos1, EmitFn& emit) const {
    const uint64_t hi0 = pos0 - i;
    const uint64_t hi1 = pos1 - (i + 1);
    if constexpr (kWMode == WidthMode::kZero) {
      emit(i, hi0, hi1);
    } else if constexpr (kWMode == WidthMode::kSmall) {
      const size_t bp = size_t{i} * width;
      const uint64_t raw = LittleEndian::Load64(&low[bp >> 3]) >> (bp & 7u);
      emit(i, (hi0 << width) | (raw & low_mask),
           (hi1 << width) | ((raw >> width) & low_mask));
    } else {
      emit(i, (hi0 << width) | Decode56(Index(i)),
           (hi1 << width) | Decode56(Index(i + 1)));
    }
  }

  ABSL_ATTRIBUTE_ALWAYS_INLINE static uint64_t LoadDenseHint(
      const internal::DenseEntry* dense, uint32_t idx) {
    const uint32_t d1 = idx >> 8;
    const auto& entry = dense[d1];
    const uint64_t hdr = LittleEndian::Load64(&entry);
    const uint32_t base = static_cast<uint32_t>(hdr);
    if (ABSL_PREDICT_TRUE(((hdr >> 32) & 0xffffu) == 0)) {
      return uint64_t{base} + (uint64_t{d1} << 8) +
             entry.delta[(idx >> 5) & 7u].Load();
    }
    return kSparseHintFlag | uint64_t{base};
  }

  template <bool kFastPdep, WidthMode kWMode, typename EmitFn>
  void ForEachAdjacentPairImpl(absl::Span<const uint32_t> indices,
                               uint32_t limit_index, EmitFn& emit) const {
    const uint8_t width = opts_.width();
    const uint64_t low_mask =
        (kWMode == WidthMode::kSmall) ? ((uint64_t{1} << width) - 1) : 0;
    const uint8_t* const high = high_;
    const uint8_t* const low = low_;
    const internal::DenseEntry* const dense1 = dense1_;
    const size_t n = indices.size();
    const uint32_t* const in = indices.data();

    // No-index tiny array fallback (`bits() <= 64`).
    if (ABSL_PREDICT_FALSE(!opts_.has_index(Options::ONES))) {
      for (size_t k = 0; k < n; ++k) {
        const uint32_t i = in[k];
        if (ABSL_PREDICT_FALSE(i >= limit_index)) continue;
        uint32_t bi = 0;
        uint32_t r = i;
        uint64_t x = LittleEndian::Load64(&high[0]);
        uint32_t cnt = static_cast<uint32_t>(absl::popcount(x));
        while (cnt <= r) {
          bi += 8;
          r -= cnt;
          x = LittleEndian::Load64(&high[bi]);
          cnt = static_cast<uint32_t>(absl::popcount(x));
        }
        const auto [b0, after] =
            nthbit_internal::NthbitAndAfterDispatch<kFastPdep>(x, r);
        const uint64_t pos0 = uint64_t{bi} * 8 + b0;
        const uint64_t pos1 =
            after != 0 ? (uint64_t{bi} * 8 + absl::countr_zero(after))
                       : NextOneAfterWordSlow(bi, i + 1);
        EmitFromHighPair<kWMode>(low, width, low_mask, i, pos0, pos1, emit);
      }
      return;
    }

    // Regime 1 — Dense forward word-stream (`avg_step <= 24` on sorted input):
    // streams sequentially across 64-bit `high_` words with zero `dense1_`
    // reads between contiguous clusters.
    const bool looks_dense = (n >= 4) && (in[0] < limit_index) &&
                             (in[n - 1] >= in[0]) &&
                             (uint64_t{in[n - 1] - in[0]} <= uint64_t{n} * 24);
    if (looks_dense) {
      bool cur_valid = false;
      uint32_t prev_i = 0;
      uint32_t cur_byte = 0;
      uint64_t cur_after = 0;  // non-zero, starts at `(prev_i + 1)`-th 1-bit
      uint32_t cur_rem = 0;    // popcount(cur_after)

      for (size_t k = 0; k < n; ++k) {
        const uint32_t i = in[k];
        if (ABSL_PREDICT_FALSE(i >= limit_index)) continue;
        uint64_t pos0, pos1;
        if (ABSL_PREDICT_TRUE(cur_valid && i > prev_i && i - prev_i <= 64)) {
          uint32_t need = i - prev_i - 1;  // 0-indexed rank within `cur_after`
          while (cur_rem <= need) {
            need -= cur_rem;
            cur_byte += 8;
            cur_after = LittleEndian::Load64(&high[cur_byte]);
            cur_rem = static_cast<uint32_t>(absl::popcount(cur_after));
          }
          // Unconditional branchless `NthbitAndAfter` avoids 50/50 branch
          // mispredicts when `need` mixes 0 and >=1 (e.g. step 1..2).
          const auto [b0, after] =
              nthbit_internal::NthbitAndAfterDispatch<kFastPdep>(cur_after,
                                                                 need);
          pos0 = uint64_t{cur_byte} * 8 + b0;
          const uint32_t new_rem = cur_rem - need - 1;
          if (ABSL_PREDICT_TRUE(new_rem != 0)) {
            cur_after = after;
            cur_rem = new_rem;
            pos1 = uint64_t{cur_byte} * 8 + absl::countr_zero(after);
          } else {
            std::tie(pos1, cur_byte, cur_after) =
                NextOneWordSlow(cur_byte, i + 1);
            cur_rem = static_cast<uint32_t>(absl::popcount(cur_after));
          }
          prev_i = i;
          EmitFromHighPair<kWMode>(low, width, low_mask, i, pos0, pos1, emit);
          continue;
        }
        // Initial seek or jump > 64 (or unsorted/duplicate element).
        const uint64_t hint = LoadDenseHint(dense1, i);
        if (ABSL_PREDICT_FALSE((hint & kSparseHintFlag) != 0)) {
          std::tie(pos0, pos1) =
              Select1AdjacentPairSparse(static_cast<uint32_t>(hint), i);
          cur_valid = false;
        } else {
          uint32_t bi = static_cast<uint32_t>(hint >> 3);
          uint32_t r = i & 31u;
          uint64_t x =
              LittleEndian::Load64(&high[bi]) & (~uint64_t{0} << (hint & 7u));
          uint32_t cnt = static_cast<uint32_t>(absl::popcount(x));
          while (cnt <= r) {
            bi += 8;
            r -= cnt;
            x = LittleEndian::Load64(&high[bi]);
            cnt = static_cast<uint32_t>(absl::popcount(x));
          }
          const auto [b0, after] =
              nthbit_internal::NthbitAndAfterDispatch<kFastPdep>(x, r);
          pos0 = uint64_t{bi} * 8 + b0;
          const uint32_t new_rem = cnt - r - 1;
          if (ABSL_PREDICT_TRUE(new_rem != 0)) {
            cur_byte = bi;
            cur_after = after;
            cur_rem = new_rem;
            pos1 = uint64_t{bi} * 8 + absl::countr_zero(after);
          } else {
            std::tie(pos1, cur_byte, cur_after) = NextOneWordSlow(bi, i + 1);
            cur_rem = static_cast<uint32_t>(absl::popcount(cur_after));
          }
          cur_valid = true;
          prev_i = i;
        }
        EmitFromHighPair<kWMode>(low, width, low_mask, i, pos0, pos1, emit);
      }
      return;
    }

    // Regime 2 — L1-resident array, small batch, or moderate step (`<= 48`):
    // tight fully-inlined loop caching `d2` hints with zero ring-buffer
    // overhead.
    const bool use_3stage_pipe =
        (opts_.size() > 32768u) && (n >= 24) &&
        !(in[n - 1] >= in[0] &&
          uint64_t{in[n - 1] - in[0]} <= uint64_t{n} * 48);
    if (!use_3stage_pipe) {
      uint32_t last_d2 = std::numeric_limits<uint32_t>::max();
      uint64_t last_hint = 0;
      const bool do_pf = (opts_.size() > 32768u) && (n >= 8);
      for (size_t k = 0; k < n; ++k) {
        if (do_pf && k + 8 < n) {
          const uint32_t pf = in[k + 8];
          if (ABSL_PREDICT_TRUE(pf < limit_index)) {
            absl::PrefetchToLocalCache(&dense1[pf >> 8]);
            if constexpr (kWMode != WidthMode::kZero) {
              absl::PrefetchToLocalCache(&low[(size_t{pf} * width) >> 3]);
            }
          }
        }
        const uint32_t i = in[k];
        if (ABSL_PREDICT_FALSE(i >= limit_index)) continue;
        const uint32_t d2_key = i >> 5;
        if (d2_key != last_d2) {
          last_d2 = d2_key;
          last_hint = LoadDenseHint(dense1, i);
        }
        const auto [pos0, pos1] =
            DecodeHighPairFromHint<kFastPdep>(high, i, last_hint);
        EmitFromHighPair<kWMode>(low, width, low_mask, i, pos0, pos1, emit);
      }
      return;
    }

    // Regime 3 — Sparse large-array batch: branch-free, fully-inlined 3-stage
    // software pipeline overlapping independent `dense1_` + `low_` fetches
    // (2*kStage ahead) with dependent `high_` fetches (kStage ahead).
    constexpr size_t kStage = 16;
    constexpr size_t kMask = kStage - 1;
    struct Slot {
      uint32_t idx;
      uint64_t hint;
    };
    Slot ring[kStage];

    auto stage1 = [&](uint32_t raw_i) ABSL_ATTRIBUTE_ALWAYS_INLINE {
      if (ABSL_PREDICT_TRUE(raw_i < limit_index)) {
        absl::PrefetchToLocalCache(&dense1[raw_i >> 8]);
        if constexpr (kWMode != WidthMode::kZero) {
          absl::PrefetchToLocalCache(&low[(size_t{raw_i} * width) >> 3]);
        }
      }
    };

    auto stage2 = [&](uint32_t raw_i) ABSL_ATTRIBUTE_ALWAYS_INLINE -> Slot {
      if (ABSL_PREDICT_FALSE(raw_i >= limit_index)) {
        return {limit_index, 0};
      }
      const uint64_t hint = LoadDenseHint(dense1, raw_i);
      if (ABSL_PREDICT_TRUE((hint & kSparseHintFlag) == 0)) {
        const uint8_t* hp = &high[hint >> 3];
        absl::PrefetchToLocalCache(hp);
      } else {
        absl::PrefetchToLocalCache(
            &sparse_[static_cast<uint32_t>(hint) + (raw_i & 255u)]);
      }
      return {raw_i, hint};
    };

    for (size_t k = 0; k < kStage; ++k) stage1(in[k]);
    const size_t prime1_end = std::min(n, 2 * kStage);
    for (size_t k = 0; k < kStage; ++k) {
      if (k + kStage < prime1_end) stage1(in[k + kStage]);
      ring[k] = stage2(in[k]);
    }
    size_t k = 0;
    if (n >= 2 * kStage) {
      const size_t steady_end = n - 2 * kStage;
      for (; k < steady_end; ++k) {
        stage1(in[k + 2 * kStage]);
        const Slot cur = ring[k & kMask];
        ring[k & kMask] = stage2(in[k + kStage]);
        if (ABSL_PREDICT_TRUE(cur.idx < limit_index)) {
          const auto [pos0, pos1] =
              DecodeHighPairFromHint<kFastPdep>(high, cur.idx, cur.hint);
          EmitFromHighPair<kWMode>(low, width, low_mask, cur.idx, pos0, pos1,
                                   emit);
        }
      }
    }
    const size_t drain2_end = n - kStage;
    for (; k < drain2_end; ++k) {
      const Slot cur = ring[k & kMask];
      ring[k & kMask] = stage2(in[k + kStage]);
      if (ABSL_PREDICT_TRUE(cur.idx < limit_index)) {
        const auto [pos0, pos1] =
            DecodeHighPairFromHint<kFastPdep>(high, cur.idx, cur.hint);
        EmitFromHighPair<kWMode>(low, width, low_mask, cur.idx, pos0, pos1,
                                 emit);
      }
    }
    for (; k < n; ++k) {
      const Slot cur = ring[k & kMask];
      if (ABSL_PREDICT_TRUE(cur.idx < limit_index)) {
        const auto [pos0, pos1] =
            DecodeHighPairFromHint<kFastPdep>(high, cur.idx, cur.hint);
        EmitFromHighPair<kWMode>(low, width, low_mask, cur.idx, pos0, pos1,
                                 emit);
      }
    }
  }

  // Finds the first set bit at or after `z_curr + 1` (the `idx`-th one bit,
  // where `idx < size()`), staying in L1 cache for common buckets and falling
  // back to O(1) `Select(1, idx)` on long runs of zeros.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t NextOneAfterZero(uint64_t z_curr,
                                                         uint32_t idx) const {
    const uint64_t bp = z_curr + 1;
    const uint32_t bi = static_cast<uint32_t>(bp >> 3);
    const uint64_t w0 = LittleEndian::Load64(&high_[bi]) >> (bp & 7u);
    if (ABSL_PREDICT_TRUE(w0 != 0)) return bp + absl::countr_zero(w0);
    const uint64_t w1 = LittleEndian::Load64(&high_[bi + 8]);
    if (ABSL_PREDICT_TRUE(w1 != 0)) {
      return uint64_t{bi + 8} * 8 + absl::countr_zero(w1);
    }
    if (opts_.has_index(Options::ONES)) {
      return Select(1, idx, false).first.value();
    }
    return FindNextSetBitSlow(bi + 8, true).value();
  }

  struct ZeroBucket {
    uint32_t start;
    uint32_t limit;
    uint64_t z_curr;
  };

  // Resolves the index range `[start, limit)` of elements with high bits `hi`
  // (`hi <= zero_bits()`) and, when `hi < zero_bits()`, the position `z_curr`
  // of the `hi`-th zero bit. (When `hi == zero_bits()` and `v < max_value()`,
  // `at(size() - 1)` is in `[start, size())` with low bits `> lo`, so `idx <
  // limit` always holds and `z_curr` is unused.)
  ZeroBucket ResolveZeroBucket(uint32_t hi) const {
    if (ABSL_PREDICT_FALSE(hi >= opts_.zero_bits())) {
      const uint32_t s =
          (hi == 0) ? 0u
                    : static_cast<uint32_t>(
                          Select(0, hi - 1, false).first.value() - (hi - 1));
      return {s, opts_.size(), 0};
    }
    if (ABSL_PREDICT_FALSE(hi == 0)) {
      const uint64_t z0 = Select(0, 0, false).first.value();
      return {0, static_cast<uint32_t>(z0), z0};
    }
    if (ABSL_PREDICT_TRUE(hi % Options::kD1 != 0)) {
      const auto [zp, zc] = Select(0, hi - 1, true);
      return {static_cast<uint32_t>(zp.value() - (hi - 1)),
              static_cast<uint32_t>(zc.value() - hi), zc.value()};
    }
    const uint64_t zp = Select(0, hi - 1, false).first.value();
    const uint64_t zc = Select(0, hi, false).first.value();
    return {static_cast<uint32_t>(zp - (hi - 1)),
            static_cast<uint32_t>(zc - hi), zc};
  }

  // Completes `UpperBoundWithValue` inside a resolved zero bucket `[start,
  // limit)` (where `start` may already be narrowed to a monotone lower bound
  // `<= limit`).
  ABSL_ATTRIBUTE_ALWAYS_INLINE UpperBoundResult FinishUpperBoundInBucket(
      uint32_t hi, uint64_t lo, uint8_t width, uint64_t low_mask,
      uint32_t start, uint32_t limit, uint64_t z_curr) const {
    uint32_t len = limit - start;
    if (len == 0) {
      const uint64_t next_one = NextOneAfterZero(z_curr, limit);
      return {limit, ((next_one - limit) << width) |
                         DecodeLowFast(limit, width, low_mask)};
    }
    uint32_t s = start;
    while (len > 1) {
      const uint32_t half = len >> 1;
      const uint32_t mid = s + half;
      s = (lo < DecodeLowFast(mid, width, low_mask)) ? s : mid;
      len -= half;
    }
    const uint64_t lo_s = DecodeLowFast(s, width, low_mask);
    if (lo < lo_s) {
      return {s, (uint64_t{hi} << width) | lo_s};
    }
    const uint32_t idx = s + 1;
    if (idx < limit) {
      return {idx,
              (uint64_t{hi} << width) | DecodeLowFast(idx, width, low_mask)};
    }
    const uint64_t next_one = NextOneAfterZero(z_curr, idx);
    return {idx,
            ((next_one - idx) << width) | DecodeLowFast(idx, width, low_mask)};
  }

  // Index-only bucket resolution for the sparse 4-stage pipeline: skips
  // reconstructing `at(idx)` (`NextOneAfterZero` + extra `DecodeLowFast`),
  // cutting retired instructions by ~35% on sparse `UpperBound` batches.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint32_t
  FinishUpperBoundIndexOnly(uint64_t lo, uint8_t width, uint64_t low_mask,
                            uint32_t start, uint32_t limit) const {
    uint32_t len = limit - start;
    if (len == 0) return start;
    uint32_t s = start;
    while (len > 1) {
      const uint32_t half = len >> 1;
      const uint32_t mid = s + half;
      s = (lo < DecodeLowFast(mid, width, low_mask)) ? s : mid;
      len -= half;
    }
    return s + static_cast<uint32_t>(lo >= DecodeLowFast(s, width, low_mask));
  }

  template <bool kFastPdep>
  ABSL_ATTRIBUTE_ALWAYS_INLINE ZeroBucket
  ResolveZeroBucketFromHint(uint32_t hi, uint64_t hint) const {
    if (ABSL_PREDICT_FALSE(hi >= opts_.zero_bits())) {
      return ResolveZeroBucket(hi);
    }
    if (ABSL_PREDICT_FALSE((hint & kZeroHiHintFlag) != 0)) {
      const uint64_t raw_hint = hint & ~kZeroHiHintFlag;
      uint64_t z0;
      if (ABSL_PREDICT_FALSE((raw_hint & kSparseHintFlag) != 0)) {
        z0 = SelectSparse(static_cast<uint32_t>(raw_hint), 0, false, BitPos{0})
                 .first.value();
      } else {
        const uint32_t bi = static_cast<uint32_t>(raw_hint >> 3);
        const uint64_t x = (~LittleEndian::Load64(&high_[bi])) &
                           (~uint64_t{0} << (raw_hint & 7u));
        z0 = (x != 0) ? (uint64_t{bi} * 8 + absl::countr_zero(x))
                      : FindNextSetBitSlow(bi, false).value();
      }
      return {0, static_cast<uint32_t>(z0), z0};
    }

    const uint32_t r0 = hi - 1;
    if (ABSL_PREDICT_FALSE((hint & kSparseHintFlag) != 0)) {
      const uint32_t sbase = static_cast<uint32_t>(hint);
      uint64_t zp, zc;
      if (ABSL_PREDICT_TRUE((hi & 255u) != 0)) {
        const auto [p0, p1] = SelectSparse(sbase, r0, true, BitPos{0});
        zp = p0.value();
        zc = p1.value();
      } else {
        zp = SelectSparse(sbase, r0, false, BitPos{0}).first.value();
        zc = Select(0, hi, false).first.value();
      }
      return {static_cast<uint32_t>(zp - r0), static_cast<uint32_t>(zc - hi),
              zc};
    }

    uint32_t bi = static_cast<uint32_t>(hint >> 3);
    uint32_t r = r0 & 31u;
    uint64_t x =
        (~LittleEndian::Load64(&high_[bi])) & (~uint64_t{0} << (hint & 7u));
    uint32_t cnt = static_cast<uint32_t>(absl::popcount(x));
    while (cnt <= r) {
      bi += 8;
      r -= cnt;
      x = ~LittleEndian::Load64(&high_[bi]);
      cnt = static_cast<uint32_t>(absl::popcount(x));
    }
    const auto [b0, after] =
        nthbit_internal::NthbitAndAfterDispatch<kFastPdep>(x, r);
    const uint64_t zp = uint64_t{bi} * 8 + b0;
    uint64_t zc;
    if (ABSL_PREDICT_TRUE(after != 0)) {
      zc = uint64_t{bi} * 8 + absl::countr_zero(after);
    } else {
      const uint64_t nx = ~LittleEndian::Load64(&high_[bi + 8]);
      if (ABSL_PREDICT_TRUE(nx != 0)) {
        zc = uint64_t{bi + 8} * 8 + absl::countr_zero(nx);
      } else if ((hi & 255u) == 0) {
        zc = Select(0, hi, false).first.value();
      } else {
        zc = FindNextSetBitSlow(bi + 8, false).value();
      }
    }
    return {static_cast<uint32_t>(zp - r0), static_cast<uint32_t>(zc - hi), zc};
  }

  template <bool kFastPdep, bool kUniqueOnly, typename EmitFn>
  void UpperBoundBatchImpl(absl::Span<const uint32_t> values, uint64_t cap,
                           uint32_t invalid_value, EmitFn& emit) const {
    const uint8_t width = opts_.width();
    const size_t n = values.size();
    const uint32_t* const in = values.data();

    // Rare width==0 path or missing ZEROS index: interval-cached direct loop.
    if (ABSL_PREDICT_FALSE(width == 0 || !opts_.has_index(Options::ZEROS))) {
      bool has_cur = false;
      uint64_t prev_v = 0;
      uint64_t cur_limit = 0;
      uint32_t cur_idx = 0;
      uint32_t last_emitted = 0;
      for (size_t k = 0; k < n; ++k) {
        const uint64_t v = in[k];
        if (ABSL_PREDICT_FALSE(v >= cap)) {
          if constexpr (!kUniqueOnly) emit(invalid_value);
          continue;
        }
        if (has_cur && v >= prev_v && v < cur_limit) {
          prev_v = v;
          if constexpr (!kUniqueOnly) {
            emit(cur_idx == 0 ? invalid_value : cur_idx - 1);
          }
          continue;
        }
        const auto res = UpperBoundWithValue(v);
        has_cur = true;
        prev_v = v;
        cur_idx = res.index;
        cur_limit = res.value_at_index;
        if constexpr (kUniqueOnly) {
          if (res.index > 0 && res.index != last_emitted) {
            last_emitted = res.index;
            emit(res.index - 1);
          }
        } else {
          emit(res.index == 0 ? invalid_value : res.index - 1);
        }
      }
      return;
    }

    const uint64_t low_mask = (uint64_t{1} << width) - 1;
    const internal::DenseEntry* const dense0 = dense0_;
    const uint8_t* const high = high_;
    const uint8_t* const low = low_;

    const uint32_t zero_bits = opts_.zero_bits();
    auto compute_z_hint = [&](uint32_t hi, bool do_pf)
                              ABSL_ATTRIBUTE_ALWAYS_INLINE -> uint64_t {
      if (ABSL_PREDICT_FALSE(zero_bits == 0)) return 0;
      const uint32_t q = (hi == 0) ? 0u : (hi - 1);
      const uint32_t d1 = q >> 8;
      const auto& entry = dense0[d1];
      const uint64_t hdr = LittleEndian::Load64(&entry);
      const uint32_t base = static_cast<uint32_t>(hdr);
      uint64_t hint;
      if (ABSL_PREDICT_TRUE(((hdr >> 32) & 0xffffu) == 0)) {
        hint = uint64_t{base} + (uint64_t{d1} << 8) +
               entry.delta[(q >> 5) & 7u].Load();
        if (do_pf) {
          const uint8_t* hp = &high[hint >> 3];
          absl::PrefetchToLocalCache(hp);
          if ((reinterpret_cast<uintptr_t>(hp) & 63u) >= 48u) {
            absl::PrefetchToLocalCache(hp + 64);
          }
        }
      } else {
        hint = kSparseHintFlag | uint64_t{base};
        if (do_pf) absl::PrefetchToLocalCache(&sparse_[base + (q & 255u)]);
      }
      return (hi == 0) ? (hint | kZeroHiHintFlag) : hint;
    };

    // Detect dense regime (many consecutive hits per outer doc / zero bucket)
    // vs. sparse regime (cold L3/DRAM chases across a large array).
    const bool use_pipeline =
        (opts_.size() > 32768u) && (n >= 48) &&
        (in[n - 1] <= in[0] ||
         (uint64_t{in[n - 1] - in[0]} >= (uint64_t{n} << (width + 5))));

    if (!use_pipeline) {
      bool has_cur = false;
      uint64_t prev_v = 0;
      uint64_t cur_limit = 0;
      uint32_t cur_idx = 0;
      uint32_t last_emitted = 0;
      uint32_t cached_hi = std::numeric_limits<uint32_t>::max();
      ZeroBucket cached_zb{0, 0, 0};
      uint32_t cached_d2 = std::numeric_limits<uint32_t>::max();
      uint64_t cached_hint = 0;
      for (size_t k = 0; k < n; ++k) {
        const uint64_t v = in[k];
        if (ABSL_PREDICT_FALSE(v >= cap)) {
          if constexpr (!kUniqueOnly) emit(invalid_value);
          continue;
        }
        if (ABSL_PREDICT_TRUE(has_cur && v >= prev_v && v < cur_limit)) {
          prev_v = v;
          if constexpr (!kUniqueOnly) {
            emit(cur_idx == 0 ? invalid_value : cur_idx - 1);
          }
          continue;
        }
        // Lightweight lookahead prefetch on `dense0_` for medium-stride scans
        // (such as ExtractDocs over sparse inner terms).
        if (k + 12 < n) {
          const uint64_t v_pf = in[k + 12];
          if (ABSL_PREDICT_TRUE(v_pf < cap)) {
            const uint32_t hi_pf = static_cast<uint32_t>(v_pf >> width);
            absl::PrefetchToLocalCache(
                &dense0[(hi_pf == 0 ? 0u : hi_pf - 1) >> 8]);
          }
        }
        const uint32_t hi = static_cast<uint32_t>(v >> width);
        uint32_t s_start;
        if (hi == cached_hi) {
          s_start = (has_cur && v >= prev_v)
                        ? std::max(cached_zb.start, cur_idx)
                        : cached_zb.start;
        } else {
          const uint32_t d2_key =
              (hi == 0) ? (std::numeric_limits<uint32_t>::max() - 1)
                        : ((hi - 1) >> 5);
          if (d2_key != cached_d2) {
            cached_d2 = d2_key;
            cached_hint = compute_z_hint(hi, /*do_pf=*/false);
          }
          cached_zb = ResolveZeroBucketFromHint<kFastPdep>(hi, cached_hint);
          cached_hi = hi;
          s_start = cached_zb.start;
        }
        const auto res =
            FinishUpperBoundInBucket(hi, v & low_mask, width, low_mask, s_start,
                                     cached_zb.limit, cached_zb.z_curr);
        has_cur = true;
        prev_v = v;
        cur_idx = res.index;
        cur_limit = res.value_at_index;
        if constexpr (kUniqueOnly) {
          if (res.index > 0 && res.index != last_emitted) {
            last_emitted = res.index;
            emit(res.index - 1);
          }
        } else {
          emit(res.index == 0 ? invalid_value : res.index - 1);
        }
      }
      return;
    }

    // 4-stage fully-inlined software-pipelined lookup for sparse batches:
    //   Stage 1 (k + 3*kS): prefetch `dense0_[(hi-1) >> 8]`
    //   Stage 2 (k + 2*kS): decode `dense0_`, prefetch `high_[z_hint >> 3]`
    //   Stage 3 (k +   kS): resolve zero bucket `[start, limit)`, prefetch
    //   `low_` Stage 4 (k)       : branchless index-only search on L1-hot
    //   `low_`, emit
    constexpr size_t kS = 16;
    constexpr size_t kM = kS - 1;
    static constexpr uint32_t kInvalidBucket =
        std::numeric_limits<uint32_t>::max();
    struct HintSlot {
      uint32_t v;
      bool valid;
      uint64_t hint;
    };
    struct BucketSlot {
      uint32_t v;
      uint32_t start;
      uint32_t limit;  // `kInvalidBucket` encodes out-of-range `v >= cap`
    };
    HintSlot ring_a[kS];
    BucketSlot ring_b[kS];

    uint32_t last_s3_hi = std::numeric_limits<uint32_t>::max();
    uint32_t last_s3_start = 0;
    uint32_t last_s3_limit = 0;

    uint32_t prev_v = 0;
    uint32_t cur_idx = 0;
    uint32_t last_emitted = 0;
    uint32_t last_s4_hi = std::numeric_limits<uint32_t>::max();

    auto s1 = [&](uint32_t v) ABSL_ATTRIBUTE_ALWAYS_INLINE {
      if (ABSL_PREDICT_TRUE(uint64_t{v} < cap && zero_bits != 0)) {
        const uint32_t hi = static_cast<uint32_t>(uint64_t{v} >> width);
        const uint32_t d1 = (hi == 0) ? 0u : ((hi - 1) >> 8);
        absl::PrefetchToLocalCache(&dense0[d1]);
      }
    };

    auto s2 = [&](uint32_t v) ABSL_ATTRIBUTE_ALWAYS_INLINE -> HintSlot {
      if (ABSL_PREDICT_FALSE(uint64_t{v} >= cap)) return {v, false, 0};
      const uint32_t hi = static_cast<uint32_t>(uint64_t{v} >> width);
      return {v, true, compute_z_hint(hi, /*do_pf=*/true)};
    };

    auto s3 = [&](const HintSlot& hs)
                  ABSL_ATTRIBUTE_ALWAYS_INLINE -> BucketSlot {
      if (ABSL_PREDICT_FALSE(!hs.valid)) return {hs.v, 0, kInvalidBucket};
      const uint32_t hi = static_cast<uint32_t>(uint64_t{hs.v} >> width);
      if (ABSL_PREDICT_FALSE(hi == last_s3_hi)) {
        return {hs.v, last_s3_start, last_s3_limit};
      }
      const ZeroBucket zb = ResolveZeroBucketFromHint<kFastPdep>(hi, hs.hint);
      last_s3_hi = hi;
      last_s3_start = zb.start;
      last_s3_limit = zb.limit;
      if (zb.start < zb.limit) {
        absl::PrefetchToLocalCache(&low[(size_t{zb.start} * width) >> 3]);
      }
      return {hs.v, zb.start, zb.limit};
    };

    auto s4 = [&](const BucketSlot& bs) ABSL_ATTRIBUTE_ALWAYS_INLINE {
      if (ABSL_PREDICT_FALSE(bs.limit == kInvalidBucket)) {
        if constexpr (!kUniqueOnly) emit(invalid_value);
        return;
      }
      const uint32_t v = bs.v;
      const uint32_t hi = static_cast<uint32_t>(uint64_t{v} >> width);
      const uint32_t s_start =
          (ABSL_PREDICT_FALSE(hi == last_s4_hi) && v >= prev_v)
              ? std::max(bs.start, cur_idx)
              : bs.start;
      const uint32_t idx = FinishUpperBoundIndexOnly(
          uint64_t{v} & low_mask, width, low_mask, s_start, bs.limit);
      prev_v = v;
      cur_idx = idx;
      last_s4_hi = hi;
      if constexpr (kUniqueOnly) {
        if (ABSL_PREDICT_TRUE(idx > 0 && idx != last_emitted)) {
          last_emitted = idx;
          emit(idx - 1);
        }
      } else {
        emit(idx == 0 ? invalid_value : idx - 1);
      }
    };

    // Fill pipeline: n >= 32 == 2*kS.
    for (size_t i = 0; i < kS; ++i) s1(in[i]);
    for (size_t i = 0; i < kS; ++i) {
      s1(in[i + kS]);
      ring_a[i] = s2(in[i]);
    }
    for (size_t i = 0; i < kS; ++i) {
      if (i + 2 * kS < n) s1(in[i + 2 * kS]);
      const HintSlot prev_a = ring_a[i];
      ring_a[i] = s2(in[i + kS]);
      ring_b[i] = s3(prev_a);
    }
    size_t k = 0;
    if (n >= 3 * kS) {
      const size_t end3 = n - 3 * kS;
      for (; k < end3; ++k) {
        const size_t slot = k & kM;
        s1(in[k + 3 * kS]);
        const BucketSlot cur_b = ring_b[slot];
        const HintSlot cur_a = ring_a[slot];
        ring_a[slot] = s2(in[k + 2 * kS]);
        ring_b[slot] = s3(cur_a);
        s4(cur_b);
      }
    }
    const size_t end2 = n - 2 * kS;
    for (; k < end2; ++k) {
      const size_t slot = k & kM;
      const BucketSlot cur_b = ring_b[slot];
      const HintSlot cur_a = ring_a[slot];
      ring_a[slot] = s2(in[k + 2 * kS]);
      ring_b[slot] = s3(cur_a);
      s4(cur_b);
    }
    const size_t end1 = n - kS;
    for (; k < end1; ++k) {
      const size_t slot = k & kM;
      const BucketSlot cur_b = ring_b[slot];
      ring_b[slot] = s3(ring_a[slot]);
      s4(cur_b);
    }
    for (; k < n; ++k) {
      s4(ring_b[k & kM]);
    }
  }

  friend struct testing::CodecAccess;

  Options opts_;
  const internal::DenseEntry* dense1_;
  const internal::DenseEntry* dense0_;
  const uint8_t* high_;
  const uint8_t* low_;
  const internal::SparseEntry* sparse_;
};

// Stateful decoder optimized for forward access to the sequence. Allows for
// advancing to both increasing values (LowerBound) and indices (IteratorAt). In
// addition, Reset() is provided that repositions the decoder at the beginning
// of the sequence.
//
// Provides three methods to access the state of the decoder.
// 1) index() - returns the index of current value,
// 2) value() - returns the currently pointed value,
// 3) done() - returns true if the decoder reached the end of data.
//
// Provides three methods to advance the state.
// 1) AdvanceToIndex(i) - advances to index i (i in subsequent calls must be
//                        in an increasing order).
// 2) AdvanceToValue(v) - advances to the first value greater or equal to v
//                        (v in subsequent call must be in an increasing order)
// 3) Next() - advances to the next value. Performs about twice as fast as its
//             logical equivalent: AdvanceToIndex(index() + 1).
//
// If AdvanceToIndex and AdvanceToValue calls are mixed:
// a) v in AdvanceToValue must be >= value() if AdvanceToIndex was called last.
// b) i in AdvanceToIndex must be >= index() if AdvanceToValue was called last.
//
// Performs about two times faster than the regular decoder in case of iteration
// (either through index or values) and slowly drops the advantage as the
// iteration jumps grows (i.e. query every n-th value), reaching the performance
// of the regular decoder starting from jumps of about 128.
class ForwardDecoder {
 public:
  // The memory block should contain data encoded with Encoder.
  static absl::StatusOr<ForwardDecoder> Make(absl::Span<const char> span);

  // Returns the current index.
  uint32_t index() const { return index_.value(); }

  // Returns true if reached the end of data.
  bool done() const { return index() >= opts_.size(); }

  // Returns the current value.
  // REQUIRES: done() == false
  uint64_t value() const {
    return (pos_.value() - index()) << opts_.width() | Decode56(index_);
  }

  // Repositions the decoder to the first element.
  void Reset() {
    index_ = Index{0};
    pos_ = BitPos{0};
    block_ = LittleEndian::Load64(high_);
    ScanForNextOne(0);
  }

  // Positions the decoder to the next index.
  // REQUIRES: done() == false
  void Next() {
    DCHECK(block_);
    ++index_;
    pos_ -= BitPos{PosInBlock()};
    block_ = block_ & (block_ - 1);
    ScanForNextOne(pos_.value() / 8);
  }

  // Positions the decoder to the specified index.
  // REQUIRES: index() <= i < opts_.size()
  void AdvanceToIndex(uint32_t i) {
    DCHECK_GE(i, index());
    DCHECK_LT(i, opts_.size());
    pos_ = SelectOne(i);
    index_ = Index(i);
  }

  // Positions the decoder to the first element with value >= v.
  // REQUIRES: value() <= v
  void AdvanceToValue(uint64_t v) {
    if (ABSL_PREDICT_FALSE(opts_.max_value() < v)) {
      index_ = Index{opts_.size()};
      return;
    }
    // There are 3 possible options here:
    // Note: (pos_ - index_) is number of zeros before current one bit at pos_.
    // 1. hi < pos_ - index_
    //    In that case, current one position already has more zeroes before it,
    //    than the index of zero we're looking for -- it means we point to the
    //    right number.
    // 2. hi == pos_ - index_
    //    We point to the right block of one bits - we start searching from the
    //    current one bit and scan the low bits array until we reach low bits
    //    value greater or equal to low bits of v, or until we reach the end of
    //    the ones block (therefore increasing number of preceding zeroes =>
    //    increasing high bits value).
    // 3. hi > pos_ - index_
    //    We skip until we reach the one with at least hi zeros preceding it and
    //    then apply 1 or 2.
    uint32_t hi = v >> opts_.width();
    if (hi > pos_.value() - index_.value()) {
      // Note: hi != 0 is verified above.
      index_ = Index(SelectZero(hi - 1).value() - (hi - 1));
    }
    uint64_t lo = v & low_mask_;
    for (; hi == pos_.value() - index_.value(); Next())
      if (lo <= Decode56(index_)) break;
  }

  // Returns the size of the Elias-Fano array.
  uint32_t size() const { return opts_.size(); }
  // Returns the options used to encode the Elias-Fano array.
  const Options& options() const { return opts_; }

  absl::Span<const char> RawBytes() const;

 private:
  ForwardDecoder(absl::Span<const char> span, Options opts);

  // Decodes up to 56 bit wide value from the low bits array.
  // TODO Consider keeping low bits block in the state as well. In that
  // case we could use Next56 equivalent of Decode56.
  uint64_t Decode56(Index i) const {
    if (opts_.width() == 0) return 0;
    size_t pos = size_t{i.value()} * opts_.width();
    const uint8_t* ptr = &low_[pos / 8];
    uint64_t word = LittleEndian::Load64(ptr);
    return (word >> (pos % 8)) & low_mask_;
  }

  // TODO These heuristics can most likely be improved by taking zero/one
  // ratio under consideration.
  // TODO Consider manually prefetching high_ array.
  bool ShouldScanForOne(Index rank_distance) {
    if (!opts_.has_index(Options::ONES)) return true;
    return rank_distance < Index{128};
  }

  bool ShouldScanForZero(Index rank_distance) {
    if (!opts_.has_index(Options::ZEROS)) return true;
    return rank_distance < Index{128};
  }

  uint32_t PosInBlock() { return absl::countr_zero(block_); }

  // REQUIRES: State must be valid.
  // State changes: Updates block_ and pos_ to the position of rank'th bit.
  //                If distance == 1 also updates the index_.
  BitPos SelectOne(uint32_t rank) {
    DCHECK_GE(rank, index_.value());
    Index distance(rank - index_.value());
    // TODO Test how many bits is it worth to iterate like that.
    // TODO Consider writing a multiple-Next method that would save us
    // a few calls to ctz.
    if (distance == Index{1}) {
      Next();
      return pos_;
    }
    return ShouldScanForOne(distance)
               ? BroadWordSearch(1, distance.value(), PosInBlock())
               : SelectWithIndex(1, rank, dense1_);
  }

  // REQUIRES: State must be valid.
  // State changes: Updates block_ and pos_ to the position of the one right
  // after the rank'th zero.
  BitPos SelectZero(uint32_t rank) {
    Index current_zeroes(pos_.value() - index_.value());
    DCHECK_GE(rank, current_zeroes.value());
    Index distance(rank - current_zeroes.value());

    return ShouldScanForZero(distance)
               ? BroadWordSearch(0, distance.value(), PosInBlock())
               : SelectWithIndex(0, rank, dense0_);
  }

  // Returns the index of the rank'th bit from the index.
  //   a) returns value directly from sparse index or
  //   b) performs broadword search starting from hinted rank.
  //
  // State changes: Updates block_ and pos_ to the position of:
  //   a) the found bit, if One == true
  //   b) the next one after rank'th zero, if One == false
  BitPos SelectWithIndex(bool one, uint32_t rank,
                         const internal::DenseEntry* dense) {
    uint32_t d1_i = rank / Options::kD1;
    uint32_t offset = rank % Options::kD1;
    const auto& entry = dense[d1_i];
    uint64_t base = entry.base.Load();
    // If the first delta is zero then the position is in the dense index.
    // Otherwise it is in the sparse.
    if (entry.delta[0].Load() == 0) {
      base += d1_i * Options::kD1;
      uint32_t d2_i = offset / Options::kD2;
      pos_ = BitPos{base + entry.delta[d2_i].Load()};
      ReloadBlock(pos_);
      return BroadWordSearch(one, rank % Options::kD2, pos_.value() % 8);
    } else {
      BitPos pos;
      if (opts_.version() <= 2) {
        pos = BitPos{sparse_[base + offset].Load()};
      } else {
        pos = BitPos{uint64_t{sparse_[base + offset].Load()} + rank};
        DCHECK_EQ(d1_i * Options::kD1 + offset, rank);
      }
      ReloadBlock(pos);
      if (one) {
        pos_ = pos;
      } else {
        ScanForNextOne(pos.value() / 8);
      }
      return pos;
    }
  }

  // Returns the index of rank-th bit. pos_in_block is the bit position in the
  // block to start the search.
  //
  // State changes: Updates block_ and pos_ to the position of:
  //   a) the found bit, if One == true
  //   b) the next one after rank'th zero, if One == false
  // REQUIRES: pos_ must be positioned to the bit we start the search and
  // pos_in_block must be its position in the block_. block_ must be already
  // masked and have at least pos_in_block leading zeroes.
  BitPos BroadWordSearch(bool one, uint32_t rank, uint32_t pos_in_block) {
    uint64_t i = (pos_.value() - pos_in_block) / 8;
    if (!one) {
      rank += pos_in_block;
      block_ = ~block_;
    }
    uint32_t cnt = absl::popcount(block_);
    while (cnt <= rank) {
      i += 8;
      rank -= cnt;
      block_ = one ? LittleEndian::Load64(&high_[i])
                   : ~LittleEndian::Load64(&high_[i]);
      cnt = absl::popcount(block_);
    }
    pos_in_block = Nthbit(block_, rank);
    if (!one) block_ = ~block_;
    block_ = (block_ >> pos_in_block) << pos_in_block;
    if (!one) ScanForNextOne(i);
    return BitPos(uint64_t{i} * 8 + pos_in_block);
  }

  // State changes: Updates block_ to the position of pos and masks all bits
  // before this position.
  // Note: block_ after this method might equal 0 and therefore be invalid.
  void ReloadBlock(BitPos pos) {
    block_ = LittleEndian::Load64(&high_[pos.value() / 8]);
    uint32_t pos_in_block = pos.value() % 8;
    block_ = (block_ >> pos_in_block) << pos_in_block;
  }

  // Finds the first one in the block_ or following bytes.
  // block_it - index of the byte that starts the current block_.
  //
  // REQUIRES: block_ must be set (and masked on initial bits if necessary).
  // State changes: Updates block_ and pos_ to the position of found bit.
  // TODO Perform block_ reads aligned to 64 bits and calculate block_it
  //           from pos_. This will simplify the SelectFromIndex and
  //           BroadWordSearch code as the call to this method will be extracted
  //           to SelectZero.
  void ScanForNextOne(uint64_t block_it) {
    while (!block_) {
      block_it += 8;
      block_ = LittleEndian::Load64(&high_[block_it]);
    }
    // We can use ctz here instead of tzcnt since we know word != 0.
    pos_ = BitPos{block_it * 8 + PosInBlock()};
  }

  friend struct testing::CodecAccess;

  Options opts_;
  uint64_t low_mask_;
  const internal::DenseEntry* dense1_;
  const internal::DenseEntry* dense0_;
  const uint8_t* high_;
  const uint8_t* low_;
  const internal::SparseEntry* sparse_;

  // State:
  // Valid state consists of pos_ and index_ pointing to the same one, and
  // block_ that has zeroes on all positions that correspond to pos < pos_, and
  // one on the position that corresponds to pos_.
  //
  // Note: block_ does not have to be aligned to 64 bits, therefore the
  // position of the first bit of the block (given valid state) is:
  // first_bit_of_block = pos_ - absl::countr_zero(block_);
  uint64_t
      block_;    // Block from the unary stream, with set bit pointed by pos_.
  Index index_;  // Index of the currently pointed one.
  BitPos pos_;   // Position in the unary stream of the currently pointed one.
};

}  // namespace elias_fano

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_CODING_H_
