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

#include "gloop/util/coding/huffcoding.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/log/check.h"
#include "gloop/util/coding/bitcoding.h"
#include "gloop/util/coding/coder.h"
#include "gloop/util/coding/tablecoding.h"
#include "gloop/util/hash/hash.h"

const uint32_t HuffmanCode::kMagicNumber;

HuffmanCode* HuffmanCode::Create(const int* count, int N, int max_length) {
  // Handling of less than two symbols is currently not supported. The
  // implementation will work in optimized builds, but the resulting Huffman
  // code will not be optimal.
  DCHECK(N > 1);
  CHECK(N >= 0);
  CHECK((unsigned int)N <= (1u << max_length));
  CHECK_LE(max_length, 27);  // Encoder table can only support upto 27

  absl::FixedArray<Symbol, 0> sym(N);

  // Try coding until we get a code that obeys the "max_length"
  // restriction.  If a code exceeds "max_length", we flatten the
  // counts and try again.
  int shift_amount = 0;
  while (true) {
    // printf("=== try with shift = %d\n", shift_amount);
    for (int i = 0; i < N; i++) {
      sym[i].id_ = i;
      sym[i].count_ = std::max(count[i] >> shift_amount, 1);
      sym[i].encoding_ = 0;
      sym[i].length_ = 0;
      sym[i].next_ = nullptr;
      sym[i].last_ = &sym[i];
    }

    if (TryToBuild(sym.data(), sym.size(), max_length)) {
      break;
    } else {
      shift_amount++;
      CHECK(shift_amount < 32);
    }
  }

  HuffmanCode* h = new HuffmanCode;
  h->num_ = N;
  h->length_ = new int[N];
  for (int i = 0; i < N; i++) {
    h->length_[i] = sym[i].length_;
  }

  return h;
}

HuffmanCode* HuffmanCode::CreateFromFreqCountPairs(const int* pairs, int N,
                                                   int num_symbols,
                                                   int max_len) {
  // Handling of less than two symbols is currently not supported. The
  // implementation will work in optimized builds, but the resulting Huffman
  // code will not be optimal.
  DCHECK(N > 1);
  absl::FixedArray<int, 0> counts(num_symbols);
  memset(counts.data(), 0, sizeof(counts[0]) * num_symbols);
  for (int i = 0; i < N; i++) {
    int symbol = pairs[2 * i];
    CHECK(symbol >= 0);
    CHECK(symbol < num_symbols);
    counts[symbol] = pairs[2 * i + 1];
  }
  for (int i = 0; i < num_symbols; i++) {
    counts[i] = std::max(counts[i], 1);
  }
  HuffmanCode* code = Create(counts.data(), counts.size(), max_len);

  return code;
}

HuffmanCode::~HuffmanCode() { delete[] length_; }

void HuffmanCode::Dump(const char* label) const {
  printf("Table %s:\n", label);
  for (int i = 0; i < num_; i++) {
    printf("  %3d: %2d bits\n", i, length_[i]);
  }
}

// For a heap that keeps smallest count elements at the top.
// Since heaps try to keep biggest elements at top, we reverse
// the sense of the comparison.
bool HuffmanCode::HeapByCount(Symbol* a, Symbol* b) {
  // Tie break by id_ when count_ is identical.
  if (a->count_ == b->count_) return a->id_ > b->id_;
  return a->count_ > b->count_;
}

bool HuffmanCode::TryToBuild(Symbol* sym, int N, int max_length) {
  if (N == 1) {
    // When only one symbol is encoded, set its code length to 1. Optimally,
    // the length should be 0, but it could break some existing decoders that
    // can't handle 0-zero length symbols.
    sym[0].length_ = 1;
    return true;
  }
  absl::FixedArray<Symbol*, 0> psym(N);
  for (int i = 0; i < N; i++) {
    psym[i] = &sym[i];
  }

  int heap_size = N;
  std::make_heap(psym.begin(), psym.end(), HeapByCount);
  while (heap_size >= 2) {
    // Get two smallest elements
    Symbol* a = psym[0];
    std::pop_heap(psym.begin(), std::next(psym.begin(), heap_size),
                  HeapByCount);
    Symbol* b = psym[0];
    std::pop_heap(psym.begin(), std::next(psym.begin(), heap_size - 1),
                  HeapByCount);

    // Merge the two together.
    b->last_->next_ = a;
    b->last_ = a->last_;
    b->count_ += a->count_ + 1;  // +1 breaks ties so as to make shallower trees

    // Increment lengths of all children of "a" and "b".  These children
    // are all accessible via the linked list headed by "b".
    for (Symbol* s = b; s != nullptr; s = s->next_) {
      s->length_++;
      if (s->length_ > max_length) {
        // printf("%3d: length exceeds %d\n", s->id_, max_length);

        return false;
      }
    }

    // Put the merged element back on the smaller heap
    heap_size--;
    std::push_heap(psym.begin(), std::next(psym.begin(), heap_size),
                   HeapByCount);
  }

  return true;
}

void HuffmanCode::Save(std::vector<char>* buffer) const {
  // Format:    FIELD                   BYTES
  //            magic                   4
  //            num symbols: N          4
  //            bit length:             1 * N
  //            checksum                4
  size_t max_bytes = 12 + num_;
  absl::FixedArray<char, 0> buf(max_bytes);
  Encoder e(buf.data(), buf.size());
  e.put32(kMagicNumber);
  e.put32(num_);
  for (int i = 0; i < num_; i++) {
    e.put8(length_[i]);
  }
  uint32_t checksum = HashTo32(absl::string_view(buf.data(), e.length()));
  e.put32(checksum);
  CHECK(e.length() == max_bytes);
  buffer->insert(buffer->end(), buf.begin(), buf.end());
}

HuffmanCode* HuffmanCode::Restore(const char* buffer, int size, int* consumed) {
  HuffmanCode* h = HuffmanCode::SafeRestore(buffer, size, consumed);
  CHECK(h != nullptr) << "invalid serialized huffman code";
  return h;
}

HuffmanCode* HuffmanCode::SafeRestore(const char* buffer, int size,
                                      int* length) {
  Decoder d(buffer, size);
  if (d.avail() < 12) {
    return nullptr;
  }
  if (d.get32() != kMagicNumber) {
    return nullptr;
  }
  uint32_t num = d.get32();
  if (num < 2) {
    // We require at least 2 symbols:
    // - If num == 0, TableDecoder::InitializeFromEncoder will calculate
    //   max_symbol = num - 1 which underflows to UINT_MAX, leading to an
    //   infinite loop and out-of-bounds memory accesses.
    // - If num == 1, the Huffman code is incomplete (codespace sum < 1.0),
    //   leaving "holes" in the TableDecoder table. Probing a hole returns a 0
    //   entry, causing the decoder to decode symbol 0 while consuming 0 bits,
    //   resulting in an infinite loop on malformed streams.
    return nullptr;
  }
  // Since input may be untrusted, avoid overflow by not using num+4.
  if (d.avail() < num || d.avail() - num < 4) {  // lengths and checksum
    return nullptr;
  }
  int* len = new int[num];
  for (uint32_t i = 0; i < num; i++) {
    len[i] = d.get8();
    if (len[i] < 1 || len[i] > 27) {
      delete[] len;
      return nullptr;
    }
  }
  uint32_t checksum = d.get32();
  if (HashTo32(absl::string_view(buffer, 8 + num)) != checksum) {
    delete[] len;
    return nullptr;
  }

  HuffmanCode* h = new HuffmanCode;
  h->num_ = num;
  h->length_ = len;
  *length = static_cast<int>(d.pos());
  return h;
}

// Note that sym is already sorted by id, so because counting sort is stable
// sorted_sym will be sorted by length then by id.
void HuffmanCode::CountingSortByLength(const int* counts, Symbol* sym,
                                       Symbol** sorted_sym, int N,
                                       int max_length) {
  std::vector<int> offsets(max_length + 1);
  // Note that counts[0] is always 0 as we have no 0-length encodings.
  for (int i = 2; i <= max_length; ++i) {
    offsets[i] = counts[i - 1] + offsets[i - 1];
  }
  for (int i = 0; i < N; ++i) {
    int* cur_offset = &offsets[sym[i].length_];
    sorted_sym[*cur_offset] = sym + i;
    ++(*cur_offset);
  }
}

void HuffmanCode::InitializeEncoder(TableEncoder* encoder) const {
  const int N = num_;
  uint32_t* t = new uint32_t[N];

  if (N == 1) {
    // Initialize the table encoder for a special case when only one symbol is
    // being encoded.

    // Expected code length for one symbol is currently 1 bit.
    DCHECK(length_[0] == 1);

    // Set the table encoder entry. The code is set to 0 and the code length is
    // stored in the 5 most significant bits of the entry.
    t[0] = (length_[0] << 27);
  } else {
    // Get list of symbols as well as distribution of symbol lengths
    int count[32];                       // Number of elements of each length
    absl::FixedArray<Symbol, 0> sym(N);  // List of symbols
    int max_length = 0;
    memset(count, 0, sizeof(count));
    for (int i = 0; i < N; i++) {
      sym[i].id_ = i;
      sym[i].length_ = length_[i];
      count[sym[i].length_]++;
      max_length = std::max(max_length, sym[i].length_);
    }

    // March in increasing length/increasing id order
    absl::FixedArray<Symbol*, 0> sorted_sym(N);
    CountingSortByLength(count, sym.data(), sorted_sym.data(), N, max_length);

    uint32_t last = 0;
    int index = 0;
    for (int L = 1; L <= max_length; L++) {
      last = 2 * last + count[L];
      for (int i = 0; i < count[L]; i++) {
        CHECK(sorted_sym[index + i]->length_ == L);
        uint32_t encoding = last - 1 - i;
        t[sorted_sym[index + i]->id_] =
            (L << 27) | BitEncoder::ReverseBits(L, encoding);
      }
      index += count[L];
    }
  }

  encoder->SetTable(N, const_cast<const uint32_t*>(t));

#if 0
  printf("Encoding table %d:\n", N);
  for (int i = 0; i < N; i++) {
    uint32 c = t[i];
    uint32 l = c >> 27;
    char str[33];
    printf("  %2d: %s\n", i, BitEncoder::UnparseLSBFirst(str, c, l));
  }
#endif
}
