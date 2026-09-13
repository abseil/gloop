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
// An instance of the class "HuffmanCode" represents a huffman code:
// i.e., a map from symbols to bit patterns.  Operations are provided to:
//
// 1. Construct such a map given a frequency distribution of symbols
// 2. Save the map into a buffer
// 3. Restore the map from a buffer
// 4. Create an encoder from a map (see TableEncoder in tablecoding.h)
//
// To create a decoder from a map (see TableDecoder in tablecoding.h) first
// create an encoder and then use TableDecoder::InitializeFromEncoder().

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_HUFFCODING_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_HUFFCODING_H_

#include <assert.h>

#include <cstdint>
#include <vector>

#include "absl/base/attributes.h"

// Representation of a given Huffman code
class HuffmanCode {
 public:
  // This type is neither copyable nor movable.
  HuffmanCode(const HuffmanCode&) = delete;
  HuffmanCode& operator=(const HuffmanCode&) = delete;

  // Build Huffman code given a set of symbol frequencies, and
  // a maximum code length.
  //
  // We assume that the symbols are 0..N-1, and the frequency
  // is given as "count[i]" for symbol "i". N must be > 1.
  static HuffmanCode* Create(const int* count, int N, int max_length);

  // Helper method to call Create if what you have is an array
  // containing "N" pairs of <symbol, frequency> values in consecutive
  // array elements.  (i.e. the length of the "pairs" array is "N*2",
  // and pairs[2*i] is a symbol value and pairs[2*i+1] is the
  // frequency for this symbol).  "num_symbols" specifies the total
  // range of allowed symbols, as opposed to "N" which specifies the
  // number of observed symbols in the pairs array.  Based on the
  // pairs array, a count array will be constructed, filling in a
  // value of 1 for any symbol that is missing or that has a frequency
  // of less than 1 in the pairs array.
  static HuffmanCode* CreateFromFreqCountPairs(const int* pairs, int N,
                                               int num_symbols, int max_len);

  // Release all resources.
  ~HuffmanCode();

  // Append huffman code to supplied buffer
  void Save(std::vector<char>* buffer) const;

  // Load huffman code from supplied buffer.  Stores number of bytes
  // consumed in "*length".  Returns nullptr on malformed input.
  static HuffmanCode* SafeRestore(const char* buffer, int size, int* length);

  // Load huffman code from supplied buffer.  Stores number of bytes
  // consumed in "*length".  Dies on malformed input.
  ABSL_DEPRECATED("Use SafeRestore instead")
  static HuffmanCode* Restore(const char* buffer, int size, int* length);

  // Debugging routine
  void Dump(const char* label) const;

  // Initialize table encoder to encode using this Huffman code
  void InitializeEncoder(class TableEncoder* encoder) const;

  // Return the length of the encoding assigned to symbol "symbol" in bits
  int EncodingLength(int symbol) const {
    assert(symbol >= 0);
    assert(symbol < num_);
    return length_[symbol];
  }

 private:
  // We just need to keep track of the length assigned to each symbol
  int num_;      // Number of symbols
  int* length_;  // Length assigned to each symbol

  static constexpr uint32_t kMagicNumber = 0x7af663fbu;

  // Info kept per symbol
  struct Symbol {
    uint32_t id_;        // The symbol itself
    int count_;          // Frequency for symbol
    int length_;         // Encoding length
    uint32_t encoding_;  // Assigned encoding
    Symbol* next_;       // Next symbol in this set
    Symbol* last_;       // Last symbol in this set
  };

  // Helper functions
  static bool TryToBuild(Symbol* sym, int N, int max_length);
  static bool HeapByCount(Symbol* a, Symbol* b);
  static void CountingSortByLength(const int* counts, Symbol* sym,
                                   Symbol** sorted_sym, int N, int max_length);

  HuffmanCode() {}
};

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_HUFFCODING_H_
