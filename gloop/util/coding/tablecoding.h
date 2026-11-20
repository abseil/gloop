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

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_TABLECODING_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_TABLECODING_H_

// TableEncoder       -- Generic table based encoder for a prefix-free code
// TableDecoder       -- Generic table based decoder for a prefix-free code

#include <string.h>

#include <cassert>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gloop/util/coding/bitcoding.h"
#include "gloop/util/coding/coder.h"

// Other classes can generate specific TableEncoder objects for
// various kinds of codes.  See huffcoding.cc for an example
// of a TableEncoder that encodes Huffman codes.
class TableEncoder {
 public:
  TableEncoder() : num_(0), table_(nullptr) {}

  // This type is neither copyable nor movable.
  TableEncoder(const TableEncoder&) = delete;
  TableEncoder& operator=(const TableEncoder&) = delete;
  ~TableEncoder() { delete[] table_; }

  inline void PutSymbol(BitEncoder* encoder, uint32_t sym) const {
    DCHECK_LT(static_cast<int64_t>(sym), num_);
    uint32_t v = table_[sym];
    encoder->PutBits(v, v >> 27);
  }

  inline void SetTable(int num, const uint32_t* t) {
    delete[] table_;
    table_ = t;
    num_ = num;
  }

  inline int MaxSymbol() const { return num_ - 1; }

  inline int EncodingLength(int symbol) const {
    DCHECK_GE(symbol, 0);
    DCHECK_LT(symbol, num_);
    return table_[symbol] >> 27;
  }

  inline uint32_t Encoding(int symbol) const {
    DCHECK_GE(symbol, 0);
    DCHECK_LT(symbol, num_);
    return table_[symbol] & ((1 << 27) - 1);
  }

 private:
  // Encoding table.  The code for "i" is the low "K" bits of
  // "table_[i]" where "K" is "table_[i] >> 27".
  int num_;
  const uint32_t* table_;
};

namespace util_coding_internal {

// Errors returned by TableDecoder::TryInitializeFromEncoder().  They are not
// inlined, to keep each TableDecoder instantiation small.
ABSL_ATTRIBUTE_NOINLINE inline absl::Status SymbolTooLargeError(
    unsigned int max_symbol, unsigned int elem_bits, int maxlen_bits) {
  return absl::InvalidArgumentError(
      absl::StrCat("symbol ", max_symbol, " does not fit in a ", elem_bits,
                   "-bit table entry with ", maxlen_bits, " length bits"));
}

ABSL_ATTRIBUTE_NOINLINE inline absl::Status InvalidEncodingError(
    unsigned int symbol, uint32_t encoding, uint32_t length,
    unsigned int maxlen) {
  if (length == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("symbol ", symbol, " has an empty encoding"));
  }
  if (length > maxlen) {
    return absl::InvalidArgumentError(absl::StrCat(
        "symbol ", symbol, " has a ", length,
        "-bit encoding, but the decoder supports at most ", maxlen, " bits"));
  }
  return absl::InvalidArgumentError(
      absl::StrCat("symbol ", symbol, " has encoding 0x", absl::Hex(encoding),
                   ", which does not fit in its ", length, " bits"));
}

}  // namespace util_coding_internal

// For decoding symbols from a bit buffer.  This class is made
// a template so that fast versions for different lengths can
// be generated via just a typedef.
//
// Template parameters:
//
// 1. maxlen: should be >= maximum bit-length encoding over all valid
//    symbols.  The decoding table has 2^maxlen entries.
//
// 2. maxlen_bits: should be >= number of bits required to represent
//    maxlen.  The high "maxlen_bits" bits of each table entry contains
//    the length of encoding matched by that table entry.
//
// 3. Elem: type of each table entry.  This type must be big enough
//    to hold both the "maxlen_bits" long length field, and the
//    largest possible symbol.
//
// Example: suppose we are encoding symbols in the range [0,255] with
// a maximum possible encoding of 10 bits.  "maxlen" should therefore
// be 10.  "maxlen_bits" has to be at least 4.  The largest symbol
// (255) takes 8 bits to encode.  Therefore each table entry needs to
// be at least 12 bits long (8 bits for the symbol, and 4 bits for the
// encoding length).  Therefore a uint16 is sufficient as the table
// element type:
//
//      TableDecoder<10,4,uint16> decoder;
//      absl::Status status = decoder.TryInitializeFromEncoder(...);
//      ...

template <unsigned int maxlen_arg, int maxlen_bits = 5, class Elem = uint32_t>
class TableDecoder {
 public:
  TableDecoder() { memset(table_, 0, sizeof(table_)); }

  // Initializes this decoder to decode the code in "encoder".  Returns an
  // error, and leaves this decoder unusable, if the encoder has no symbols,
  // its largest symbol does not fit in Elem alongside "maxlen_bits" length
  // bits, an encoding is empty, longer than "maxlen", or has bits set beyond
  // its length, or the encodings of two symbols overlap.
  //
  // The initialization is intentionally separate from the constructor.
  // One advantage is that the same stack allocated TableDecoder object
  // can be used to decode multiple encodings over time.
  absl::Status TryInitializeFromEncoder(const TableEncoder* encoder) {
    if (encoder->MaxSymbol() < 0) {
      return absl::InvalidArgumentError("TableEncoder has no symbols");
    }
    // Check that Elem is big enough for a decoding entry: It needs to
    // be able to store the largest possible symbol, as well as "maxlen_bits".
    const unsigned int max_symbol = encoder->MaxSymbol();
    if (elem_bits < BitEncoder::BitsRequired(max_symbol) + maxlen_bits) {
      return util_coding_internal::SymbolTooLargeError(max_symbol, elem_bits,
                                                       maxlen_bits);
    }

    // Create decoding table
    memset(table_, 0, sizeof(table_));
    uint64_t num_patterns = 0;
    for (unsigned int sym = 0; sym <= max_symbol; sym++) {
      // Find all possible "maxlen" length bit patterns that start
      // with an encoding of "sym".
      uint32_t enc_length = encoder->EncodingLength(sym);
      uint32_t enc = encoder->Encoding(sym);
      if (enc_length == 0 || enc_length > maxlen || (enc >> enc_length) != 0) {
        return util_coding_internal::InvalidEncodingError(sym, enc, enc_length,
                                                          maxlen);
      }
      unsigned int slop = maxlen - enc_length;
      num_patterns += uint64_t{1} << slop;
      for (uint32_t extra = 0; extra < (1 << slop); extra++) {
        uint32_t pattern = (extra << enc_length) | enc;
        table_[pattern] = (enc_length << len_shift) | sym;
      }
    }

    // Should be prefix-free codes.  Otherwise two symbols share a pattern, and
    // fewer table entries are filled than there are patterns.
    uint32_t num_filled = 0;
    for (const Elem entry : table_) num_filled += (entry != 0);
    if (num_filled != num_patterns) {
      return absl::InvalidArgumentError("the encodings of two symbols overlap");
    }

#if 0
    // Remove the `#if 0` and `#endif` to print out the contents of the table
    // for debugging.
    printf("Decoder:\n");
    for (uint32 i = 0; i < (1 << maxlen); i++) {
      char buf1[33];
      printf("  %s: [ %2d ] %3d\n",
             BitEncoder::UnparseLSBFirst(buf1, i, maxlen),
             table_[i] >> len_shift,
             table_[i] & sym_mask);
    }
#endif
    return absl::OkStatus();
  }

  // Like TryInitializeFromEncoder(), but CHECK-fails if an encoding is longer
  // than "maxlen", and DCHECK-fails on any other error.
  ABSL_DEPRECATED("Use TryInitializeFromEncoder instead")
  void InitializeFromEncoder(const TableEncoder* encoder) {
    for (int sym = 0; sym <= encoder->MaxSymbol(); sym++) {
      const uint32_t enc_length = encoder->EncodingLength(sym);
      CHECK_LE(enc_length, maxlen);
    }
    const absl::Status status = TryInitializeFromEncoder(encoder);
    DCHECK_OK(status);
  }

  // Read the next symbol from "bd" and return it.  If "bd" does
  // not contain any more symbols, returns -1.
  int32_t GetSymbol(BitDecoder* bd) const {
    uint64_t avail = bd->AvailBits();
    int min_req = maxlen;
    if (avail < min_req) min_req = avail;
    uint32_t bits = bd->EnsureBits(min_req);
    Elem v = table_[bits & index_mask];
    unsigned int nbits = v >> len_shift;
    if (nbits <= avail) {
      uint32_t sym = v & sym_mask;
      bd->ConsumeBits(nbits);
      return sym;
    } else {
      // Do not have as many bits as are needed for decoding next symbol
      return -1;
    }
  }

  // Return number of bits to read from specified number to get
  // the next symbol out.
  unsigned int EncodingLength(uint32_t bits) const {
    return (table_[bits & index_mask] >> len_shift);
  }

  // Return next symbol in specified bits
  unsigned int EncodedSymbol(uint32_t bits) const {
    return (table_[bits & index_mask] & sym_mask);
  }

  ~TableDecoder() {}

  // Returns a pointer to the beginning of the decoding table.  This
  // enables better performance when decoding a large number of
  // symbols consecutively, but should only be used by clients who
  // know what they are doing.
  const Elem* decoding_table() const { return table_; }

 private:
  friend class IndexBlockDecoder;

  // Some useful constants
  static constexpr unsigned int maxlen = maxlen_arg;
  static constexpr unsigned int index_mask = (1 << maxlen) - 1;
  static constexpr unsigned int elem_bits = sizeof(Elem) * 8;
  static constexpr unsigned int len_shift = elem_bits - maxlen_bits;
  static constexpr Elem sym_mask = (1u << len_shift) - 1;

  static_assert(maxlen_bits >= BitEncoder::BitsRequired(maxlen),
                "maxlen_bits is too small for maxlen");

  Elem table_[1 << maxlen];  // The decoding table
};

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_TABLECODING_H_
