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

//
// An InlinedBitVector<NBITS> is a resizable bitvector that provides
// storage for bitvectors of length <= NBITS inline without requiring
// any heap allocation.  Typically NBITS is small (e.g., 128) so that
// bitvectors that are expected to be short do not incur extra
// malloc/free calls.
//
// This does not perfectly mimic the STL vector<bool> interface.
//
// Other operations may be added as needed to facilitate migrating
// code that uses vector<bool> to InlinedBitVector<>.

#ifndef THIRD_PARTY_GLOOP_UTIL_BITMAP_INLINED_BITVECTOR_H_
#define THIRD_PARTY_GLOOP_UTIL_BITMAP_INLINED_BITVECTOR_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "gloop/util/bitmap/bitmap.h"

namespace util {
namespace bitmap {

template <size_t NBITS>
class InlinedBitVector {
 public:
  InlinedBitVector() {}  // NOLINT see detailed rationale in cl/770370932.

  InlinedBitVector(const InlinedBitVector& other) noexcept;
  InlinedBitVector& operator=(const InlinedBitVector& other) noexcept;
  InlinedBitVector(InlinedBitVector&& other) noexcept;
  InlinedBitVector& operator=(InlinedBitVector&& other) noexcept;

  // Construct a new bit vector that is "num_bits" bits in size.  All
  // bits are initially set to 0.
  explicit InlinedBitVector(size_t num_bits);

  ~InlinedBitVector();

  // Remove all bits from the bit vector and set its length to 0 bits.
  inline void clear() {
    set_size(0);
    DCHECK(Invariants());
  }

  // Returns the size of the bit vector, in number of bits
  size_t size() const { return get_size(); }

  // Resize the bit vector so that it is "num_bits" in length.
  // If this requires growing the bit vector from its current length,
  // then '0' values are set for any bits added to the vector.
  void resize(size_t num_bits);

  // Compares the bit vector to another, returns true if they contain the same
  // bit values.
  template <size_t OtherN = NBITS>
  inline bool Equals(const InlinedBitVector<OtherN>& other) const;

  // Return the value of the bit at bit index "index"
  //
  // REQUIRES: index >= 0
  // REQUIRES: index < size()
  inline bool get_bit(size_t index) const;

  // Convenience method that is the same same as "get_bit"
  inline bool operator[](size_t index) const { return get_bit(index); }

  // Set the bit at bit index "index" to true
  //
  // REQUIRES: index >= 0
  // REQUIRES: index < size()
  inline void set_bit(size_t index);

  // Clear the bit at bit index "index" (i.e. set it to false)
  //
  // REQUIRES: index >= 0
  // REQUIRES: index < size()
  inline void clear_bit(size_t index);

  // Set the bit at bit index "index" to "val"
  //
  // REQUIRES: index >= 0
  // REQUIRES: index < size()
  inline void assign_bit(size_t index, bool val);

  // Set the bits at the [start, end) subsequence to true.
  //
  // REQUIRES: start >= 0
  // REQUIRES: end <= size()
  // REQUIRES: start <= end
  inline void SetBits(size_t start, size_t end);

  // Clear the bits at the [start, end) subsequence (i.e., set them to false).
  //
  // REQUIRES: start >= 0
  // REQUIRES: end <= size()
  // REQUIRES: start <= end
  inline void ClearBits(size_t start, size_t end);

  // Sets "this" to be the union of "this" and "other". The bitmaps do not
  // have to be the same size. If "other" is smaller than "this", all the
  // missing bits in "other" are assumed to be zero.  The size of "this" is
  // grown as necessary (if "other" is larger).
  template <size_t OtherN = NBITS>
  inline void Union(const InlinedBitVector<OtherN>& other);

  // Sets "this" to be the intersection of "this" and "other". If "other" is
  // smaller than "this", all the missing bits in "other" are assumed to be
  // zero.  The size of this is never changed by this operation (higher order
  // bits in other are ignored)
  template <size_t OtherN = NBITS>
  inline void Intersection(const InlinedBitVector<OtherN>& other);

  // Returns true if "this" and "other" have any bits set in common.
  template <size_t OtherN = NBITS>
  inline bool IsIntersectionNonEmpty(
      const InlinedBitVector<OtherN>& other) const;

  // Sets "this" to be the difference of "this" and "other". If "other" is
  // smaller than "this", all the missing bits in "other" are assumed to be
  // zero.  The size of this is never changed by this operation (higher order
  // bits in other are ignored)
  template <size_t OtherN = NBITS>
  inline void Difference(const InlinedBitVector<OtherN>& other);

  // Return the number of bits set in the bitvector.
  size_t count() const;

  // TODO: add toggle?

  // Support for iterating over the bits in the vector efficiently
  //   InlinedBitVector<128> bv = ...;
  //   for (size_t index = 0; bv.FindNextSetBit(&index); index++) {
  //     fprintf(stderr, "Bit %d is set\n", index);
  //   }

  // On input, "*index" holds the index at which we start scanning forward
  // to find the next set bit in the vector.  If another set bit is found
  // before the end of the vector, sets "*index" to its index and returns
  // true.  Otherwise, returns false.
  bool FindNextSetBit(size_t* index) const;

  // FindNextSetBitBeforeLimit like FindNextSetBit, except stops at `limit`.
  // REQUIRES: `limit <= size()`.
  bool FindNextSetBitBeforeLimit(size_t* index, size_t limit) const;

  // Finds the first offset >= "*index" that does NOT have its bit set. If
  // found, sets "*index" to this offset and returns true. Otherwise, does not
  // modify "*index" and returns false.
  bool FindNextUnsetBit(size_t* index) const;

  // Like above, except stops at `limit`.
  // REQUIRES: `limit <= size()`.
  bool FindNextUnsetBitBeforeLimit(size_t* index, size_t limit) const;

  // Returns a string of the active bits in increasing order.
  std::string ToString() const;

  // For clients that want access to the underlying array of words
  // (e.g. for processing the bits 32 at a time).  The values are
  // arranged so that bits 0..31 are in word 0, bits 32..63 are in
  // word 1, etc., with the bits oriented so that bit 0 in each word
  // holds the lowest index in that word and bit 31 holding the
  // highest index in the word.  If "size()" is not a multiple of 32,
  // then the last word is only partially filled, holding "size() %
  // 32" meaningful bits, the remaining ones will be set to 0.
  //
  // Example:
  //   const uint32_t* bits = bv.underlying_array();
  //   const size_t nwords = (bv.size() + 31) / 32;
  //   for (size_t w = 0; w < nwords; w++) {
  //     fprintf(stderr, "Word %d: %u\n", (int)w, bits[w]);
  //   }
  const uint32_t* underlying_array() const { return words(); }

  template <typename H>
  friend H AbslHashValue(H h, const InlinedBitVector& vec) {
    h = H::combine_contiguous(std::move(h), vec.words(), vec.num_words());
    return H::combine(std::move(h), vec.size());
  }

 private:
  template <size_t OtherN>
  friend class InlinedBitVector;

  // N must be positive
  static_assert(NBITS > 0, "inlined_bitvector_with_negative_size");

  // Round-up NBITS to multiple of 64 since we need 64-bit alignment.
  static constexpr size_t kInlineBits = ((NBITS + 63) / 64) * 64;

  // Return number of words needed to store nbits.
  static constexpr size_t needed_words(size_t nbits) {
    return (nbits + 31) / 32;
  }

  // Get number of 32-bit words that will be inlined.
  static constexpr size_t kInlineWords = needed_words(kInlineBits);

  // We pack the following information into one 64-bit descriptor:
  // 1. Capacity info is stored as 8-bit log2 of number of allocated words,
  //    or 0 for inlined storage. To avoid ambiguity for zero, external storage
  //    has a capacity >= 2 (i.e., its log2 >= 1).
  // 2. Size

  // Accessors to different parts of the descriptor.
  bool external() const { return (descriptor_ & 0xff) != 0; }
  size_t get_size() const { return descriptor_ >> 8; }
  void set_size(size_t n) {
    descriptor_ = (descriptor_ & 0xff) | (static_cast<uint64_t>(n) << 8);
  }
  size_t lg_capacity_words() const { return descriptor_ & 0xff; }

  // Descriptor construction helpers.
  static constexpr uint64_t kEmptyDescriptor = 0;
  static uint64_t inlined_descriptor(size_t nbits) {
    return static_cast<uint64_t>(nbits) << 8;
  }
  static uint64_t external_descriptor(size_t nbits, size_t lg_cap) {
    DCHECK_LE(needed_words(nbits), 1ull << lg_cap);
    DCHECK_GE(lg_cap, 1);
    return lg_cap | (static_cast<uint64_t>(nbits) << 8);
  }

  // Return log2 of the smallest capacity needed to store num_words words.
  static size_t capacity_lg(size_t num_words) {
    // Return at least one to avoid ambiguity with inlined storage
    // that is indicated by lg_cap==0.
    return absl::bit_width(num_words | 1);
  }

  // Number of words that store the current contents of this vector.
  inline size_t num_words() const { return needed_words(size()); }

  // Word array that holds the bits.
  const uint32_t* words() const { return external() ? d_.ptr : &d_.words[0]; }
  uint32_t* mutable_words() { return const_cast<uint32_t*>(words()); }

  // Span that covers the words containing size() bits.
  inline absl::Span<const uint32_t> word_span() const {
    return absl::Span<const uint32_t>(words(), num_words());
  }

  // Clear any unused bits in the last used word to enforce the invariant that
  // those bits are always zero.
  void ClearUnusedBits() {
    const size_t n = size();
    const size_t num_trailing_bits = n % 32;
    if (num_trailing_bits == 0) return;
    const uint32_t last_word_mask = (uint32_t{1} << num_trailing_bits) - 1;
    mutable_words()[n / 32] &= last_word_mask;
  }

  // Crashes if an invariant is violated; returns true otherwise.
  // Intended usage: DCHECK(Invariants()) or CHECK(Invariants()).
  bool Invariants() const;

  // Zero the specified number of words.
  static void ZeroWords(uint32_t* words, size_t num_words) {
    std::fill_n(words, num_words, 0);
  }

  // Copy the specified number of words from src to dst.
  static void CopyWords(uint32_t* dst, const uint32_t* src, size_t num_words) {
    std::copy_n(src, num_words, dst);
  }

  // lg_capacity_words | (size << 8)
  uint64_t descriptor_ = kEmptyDescriptor;

  // The remainder is either an array of words for inlined storage, or a pointer
  // to an array for external storage.
  union Data {
    uint32_t words[kInlineWords];
    uint32_t* ptr;
  };
  Data d_;
};

template <size_t NBITS>
inline InlinedBitVector<NBITS>::~InlinedBitVector() {
  DCHECK(Invariants());
  if (external()) {
    delete[] d_.ptr;
  }
}

template <size_t NBITS>
inline InlinedBitVector<NBITS>::InlinedBitVector(
    const InlinedBitVector& other) noexcept
    : InlinedBitVector() {
  // Potential improvement: resize() zeroes new words, which is unnecessary
  // here. We could avoid that cost if necessary at the cost of some extra code.
  resize(other.size());
  CopyWords(mutable_words(), other.words(), num_words());
}

template <size_t NBITS>
inline InlinedBitVector<NBITS>&
InlinedBitVector<NBITS>::InlinedBitVector::operator=(
    const InlinedBitVector& other) noexcept {
  if (this != &other) {
    // Potential improvement: resize() zeroes new words, which is unnecessary
    // here. We could avoid that cost if necessary at the cost of some extra
    // code.
    resize(other.size());
    CopyWords(mutable_words(), other.words(), num_words());
  }
  return *this;
}

template <size_t NBITS>
inline InlinedBitVector<NBITS>::InlinedBitVector(
    InlinedBitVector&& other) noexcept {
  descriptor_ = other.descriptor_;
  d_ = other.d_;
  other.descriptor_ = kEmptyDescriptor;
  DCHECK(Invariants());
  DCHECK(other.Invariants());
}

template <size_t NBITS>
inline InlinedBitVector<NBITS>& InlinedBitVector<NBITS>::operator=(
    InlinedBitVector&& other) noexcept {
  if (this != &other) {
    if (external()) {
      delete[] d_.ptr;
    }
    descriptor_ = other.descriptor_;
    d_ = other.d_;
    other.descriptor_ = kEmptyDescriptor;
  }
  DCHECK(Invariants());
  DCHECK(other.Invariants());
  return *this;
}

template <size_t NBITS>
InlinedBitVector<NBITS>::InlinedBitVector(size_t num_bits) {
  if (num_bits <= kInlineBits) {
    descriptor_ = inlined_descriptor(num_bits);
    // It is faster to zero the entire array.
    ZeroWords(d_.words, kInlineWords);
  } else {
    const size_t num_words = needed_words(num_bits);
    const size_t new_cap_lg = capacity_lg(num_words);
    DCHECK_GE(1ull << new_cap_lg, num_words);
    d_.ptr = new uint32_t[1ull << new_cap_lg];
    descriptor_ = external_descriptor(num_bits, new_cap_lg);
    ZeroWords(mutable_words(), num_words);
  }

  DCHECK(Invariants());
}

template <size_t NBITS>
inline size_t InlinedBitVector<NBITS>::count() const {
  size_t r = 0;
  for (uint32_t w : word_span()) r += absl::popcount(w);
  return r;
}

template <size_t NBITS>
inline void InlinedBitVector<NBITS>::resize(size_t num_bits) {
  if (num_bits < size()) {
    set_size(num_bits);  // Shrink in place.
    ClearUnusedBits();   // Fix up any partial word at end of num_bits
    DCHECK(Invariants());
    return;
  }

  uint32_t* old_array = mutable_words();
  size_t current_words = num_words();
  const size_t num_words = needed_words(num_bits);
  const size_t capacity_in_words =
      external() ? (1ull << lg_capacity_words()) : kInlineWords;
  if (num_words <= capacity_in_words) {
    // Can resize in place.
    ZeroWords(old_array + current_words, num_words - current_words);
    set_size(num_bits);
    DCHECK(Invariants());
    return;
  }

  // We need to enlarge.
  const size_t new_cap_lg = capacity_lg(num_words);
  DCHECK_GE(1ull << new_cap_lg, num_words);
  uint32_t* new_array = new uint32_t[1ull << new_cap_lg];
  CopyWords(new_array, old_array, current_words);
  ZeroWords(new_array + current_words, num_words - current_words);

  // Drop any old external storage.
  if (external()) {
    delete[] old_array;
  }

  // Install new array.
  d_.ptr = new_array;
  descriptor_ = external_descriptor(num_bits, new_cap_lg);

  DCHECK(Invariants());
}

template <size_t NBITS>
template <size_t OtherN>
inline bool InlinedBitVector<NBITS>::Equals(
    const InlinedBitVector<OtherN>& other) const {
  return (size() == other.size()) && (word_span() == other.word_span());
}

template <size_t NBITS>
inline bool InlinedBitVector<NBITS>::get_bit(size_t index) const {
  DCHECK_LT(index, size());
  const size_t word = index / 32;
  const size_t bit_index = index & 31;
  const uint32_t* a = words();
  return (a[word] & (1u << bit_index)) ? true : false;
}

template <size_t NBITS>
inline void InlinedBitVector<NBITS>::set_bit(size_t index) {
  DCHECK_LT(index, size());
  const size_t word = index / 32;
  const size_t bit_index = index & 31;
  uint32_t* a = mutable_words();
  a[word] |= (1u << bit_index);
  DCHECK(Invariants());
}

template <size_t NBITS>
inline void InlinedBitVector<NBITS>::clear_bit(size_t index) {
  DCHECK_LT(index, size());
  const size_t word = index / 32;
  const size_t bit_index = index & 31;
  uint32_t* a = mutable_words();
  a[word] &= ~(1u << bit_index);
  DCHECK(Invariants());
}

template <size_t NBITS>
inline void InlinedBitVector<NBITS>::assign_bit(size_t index, bool val) {
  if (val) {
    set_bit(index);
  } else {
    clear_bit(index);
  }
}

template <size_t NBITS>
inline void InlinedBitVector<NBITS>::SetBits(size_t start, size_t end) {
  DCHECK_GE(start, 0);
  DCHECK_LE(start, end);
  DCHECK_LE(end, size());
  if (start >= end) {
    // Avoid potential out-of-range unnecessary read of start_word below
    return;
  }

  const size_t start_word = start / 32;
  const size_t start_word_bit_index = start % 32;
  const size_t end_word = end / 32;
  const size_t end_word_bit_index = end % 32;
  const uint32_t start_word_mask = ~((1u << start_word_bit_index) - 1);
  const uint32_t end_word_mask = (1u << end_word_bit_index) - 1;

  uint32_t* a = mutable_words();
  if (start_word == end_word) {
    a[start_word] |= (start_word_mask & end_word_mask);
  } else {
    a[start_word] |= start_word_mask;
    for (int i = start_word + 1; i < end_word; ++i) {
      a[i] = ~(0u);
    }
    if (end_word_bit_index != 0) {
      a[end_word] |= end_word_mask;
    }
  }
  DCHECK(Invariants());
}

template <size_t NBITS>
inline void InlinedBitVector<NBITS>::ClearBits(size_t start, size_t end) {
  DCHECK_GE(start, 0);
  DCHECK_LE(start, end);
  DCHECK_LE(end, size());
  if (start >= end) {
    // Avoid potential out-of-range unnecessary read of start_word below
    return;
  }

  const size_t start_word = start / 32;
  const size_t start_word_bit_index = start % 32;
  const size_t end_word = end / 32;
  const size_t end_word_bit_index = end % 32;
  const uint32_t start_word_mask = (1u << start_word_bit_index) - 1;
  const uint32_t end_word_mask = ~((1u << end_word_bit_index) - 1);

  uint32_t* a = mutable_words();
  if (start_word == end_word) {
    a[start_word] &= (start_word_mask | end_word_mask);
  } else {
    a[start_word] &= start_word_mask;
    for (int i = start_word + 1; i < end_word; ++i) {
      a[i] = 0;
    }
    if (end_word_bit_index != 0) {
      a[end_word] &= end_word_mask;
    }
  }
  DCHECK(Invariants());
}

template <size_t NBITS>
bool InlinedBitVector<NBITS>::FindNextSetBit(size_t* index) const {
  return Bitmap32::FindNextSetBitInVector(words(), index, size());
}

template <size_t NBITS>
bool InlinedBitVector<NBITS>::FindNextSetBitBeforeLimit(size_t* index,
                                                        size_t limit) const {
  return Bitmap32::FindNextSetBitInVector(words(), index, limit);
}

template <size_t NBITS>
bool InlinedBitVector<NBITS>::FindNextUnsetBit(size_t* index) const {
  return Bitmap32::FindNextUnsetBitInVector(words(), index, size());
}

template <size_t NBITS>
bool InlinedBitVector<NBITS>::FindNextUnsetBitBeforeLimit(size_t* index,
                                                          size_t limit) const {
  return Bitmap32::FindNextUnsetBitInVector(words(), index, limit);
}

template <size_t NBITS>
std::string InlinedBitVector<NBITS>::ToString() const {
  std::string r = "{";
  for (size_t i = 0; FindNextSetBit(&i); i++) {
    CHECK(get_bit(i));
    if (r.size() > 1) {
      r.push_back(',');
    }
    absl::StrAppend(&r, i);
  }
  r.push_back('}');
  return r;
}

template <size_t NBITS>
template <size_t OtherN>
void InlinedBitVector<NBITS>::Union(const InlinedBitVector<OtherN>& other) {
  if (ABSL_PREDICT_FALSE(other.size() > size())) resize(other.size());
  uint32_t* my_words = mutable_words();
  const uint32_t* other_words = other.words();
  const int n_words = other.num_words();
  for (int i = 0; i < n_words; ++i) {
    my_words[i] |= other_words[i];
  }
  DCHECK(Invariants());
}

template <size_t NBITS>
template <size_t OtherN>
void InlinedBitVector<NBITS>::Intersection(
    const InlinedBitVector<OtherN>& other) {
  const size_t my_num_words = num_words();
  uint32_t* my_words = mutable_words();
  const uint32_t* other_words = other.words();
  int n_words = std::min(my_num_words, other.num_words());
  for (int i = 0; i < n_words; ++i) {
    my_words[i] &= other_words[i];
  }
  for (int i = n_words; i < my_num_words; ++i) {
    my_words[i] = 0;
  }
  DCHECK(Invariants());
}

template <size_t NBITS>
template <size_t OtherN>
bool InlinedBitVector<NBITS>::IsIntersectionNonEmpty(
    const InlinedBitVector<OtherN>& other) const {
  const uint32_t* my_words = words();
  const uint32_t* other_words = other.words();
  int n_words = std::min(num_words(), other.num_words());
  for (int i = 0; i < n_words; ++i) {
    if ((my_words[i] & other_words[i]) != 0) {
      return true;
    }
  }
  return false;
}

template <size_t NBITS>
template <size_t OtherN>
void InlinedBitVector<NBITS>::Difference(
    const InlinedBitVector<OtherN>& other) {
  uint32_t* my_words = mutable_words();
  const uint32_t* other_words = other.words();
  int n_words = std::min(num_words(), other.num_words());
  for (int i = 0; i < n_words; ++i) {
    my_words[i] &= ~other_words[i];
  }
  DCHECK(Invariants());
}

template <size_t LhsNBITS, size_t RhsNBITS>
bool operator==(const InlinedBitVector<LhsNBITS>& lhs,
                const InlinedBitVector<RhsNBITS>& rhs) {
  return lhs.Equals(rhs);
}

template <size_t LhsNBITS, size_t RhsNBITS>
bool operator!=(const InlinedBitVector<LhsNBITS>& lhs,
                const InlinedBitVector<RhsNBITS>& rhs) {
  return !lhs.Equals(rhs);
}

template <size_t NBITS>
inline bool InlinedBitVector<NBITS>::Invariants() const {
  if (external()) {
    CHECK_LE(num_words(), (1ull << lg_capacity_words()));
  } else {
    CHECK_LE(size(), kInlineBits);
    CHECK_EQ(lg_capacity_words(), 0);
  }

  // Bits at index >= size() in last word must be zero.
  const size_t num_trailing_bits = size() % 32;
  if (num_trailing_bits > 0) {
    const uint32_t last_word = words()[num_words() - 1];
    DCHECK_EQ(last_word >> num_trailing_bits, 0);
  }

  return true;
}

}  // namespace bitmap
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_BITMAP_INLINED_BITVECTOR_H_
