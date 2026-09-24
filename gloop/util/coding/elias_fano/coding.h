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
//   Sebastiano Vigna, "Broadword Implementation of Rank/Select Queries"
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
class CodecAccess;
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

  friend class testing::CodecAccess;

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
    uint64_t base = entry.base.Load();
    // If the first delta is zero then the position is in the dense index.
    // Otherwise it is in the sparse.
    if (ABSL_PREDICT_TRUE(entry.delta[0].Load() == 0)) {
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
    uint32_t i = hint_pos.value() / 8;
    // Negate the word if we are counting zeros.
    uint64_t x = one ? LittleEndian::Load64(&high_[i])
                     : ~LittleEndian::Load64(&high_[i]);
    x = x >> (hint_pos.value() % 8);
    // Prefetch the next line, which we're likely to reuse.
    absl::PrefetchToLocalCache(&high_[i] + ABSL_CACHELINE_SIZE);
    uint32_t cnt = absl::popcount(x);
    if (cnt > rank) {
      BitPos pos{hint_pos.value() + Nthbit(x, rank)};
      if (!next) return {pos, BitPos{0}};
      ++rank;
      if (cnt > rank) return {pos, BitPos(hint_pos.value() + Nthbit(x, rank))};
      return {pos, FindNextSetBitSlow(i, one)};
    }
    do {
      i += 8;
      rank -= cnt;
      x = one ? LittleEndian::Load64(&high_[i])
              : ~LittleEndian::Load64(&high_[i]);
      cnt = absl::popcount(x);
    } while (cnt <= rank);
    BitPos pos{uint64_t{i} * 8 + Nthbit(x, rank)};
    if (!next) return {pos, BitPos{0}};
    ++rank;
    if (cnt > rank) return {pos, BitPos(uint64_t{i} * 8 + Nthbit(x, rank))};
    return {pos, FindNextSetBitSlow(i, one)};
  }

  friend class testing::CodecAccess;

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

  friend class testing::CodecAccess;

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
