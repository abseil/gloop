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

#ifndef THIRD_PARTY_GLOOP_STRINGS_CORD_BYTESTREAM_H_
#define THIRD_PARTY_GLOOP_STRINGS_CORD_BYTESTREAM_H_

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/numeric/bits.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"

namespace strings {

class CordByteSink final : public ByteSink {
 public:
  explicit CordByteSink(absl::Cord* absl_nonnull dest) : dest_(dest) {}
  CordByteSink(const CordByteSink&) = delete;
  CordByteSink& operator=(const CordByteSink&) = delete;

  void Append(const char* absl_nonnull data, size_t n) override {
    dest_->Append(absl::string_view(data, n));
  }

  void AppendExternalMemory(
      absl::string_view data, void* absl_nullable arg,
      void (*absl_nonnull memory_releaser)(void* absl_nullable)) override;

  size_t MinAppendExternalMemoryLength() const override;

  strings::TypeId GetTypeId() const override;

  absl::Cord* absl_nonnull cord() { return dest_; }

 private:
  absl::Cord* absl_nonnull dest_;
};

// A CordReader yields the sequence of bytes that make up the
// contents of a Cord.
//
// Examples:
//
//   absl::string_view s;
//   CordReader reader1(x);
//   while (reader1.ReadFragment(&s)) {
//     Process(s);
//   }
//
//   CordReader reader2(x);
//   while (reader2.PeekFragment(&s)) {
//     ProcessPartOf(s);
//     reader2.Skip(number_of_bytes_processed);
//   }
//
class CordReader final : public ByteSource {
 public:
  // Creates "empty" reader, which is expected to be Reset at a later time.
  CordReader();

  // Creates a reader. The given Cord must outlive the reader. That's why we
  // also delete overload with rvalue reference.
  explicit CordReader(const absl::Cord& src);
  explicit CordReader(absl::Cord&& src) = delete;

  ~CordReader() override;

  // Resets the CordReader to "empty" state. Any pointers returned before
  // this call are no longer valid.
  void Reset();

  // Resets the cord reader to read from the beginning of "cord". Any
  // pointers returned before this call are no longer valid. The given Cord must
  // outlive the reader. That's why we also delete overload with rvalue
  // reference.
  void Reset(const absl::Cord& cord);
  void Reset(absl::Cord&& cord) = delete;

  // Return true iff there are no more bytes to read.
  bool done() const;

  // Returns the number of bytes that have been read using this reader.
  size_t Position() const;

  // Return a reference to the underlying Cord.
  const absl::Cord& cord() const;

  // Returns the number of bytes that can be read using this reader.
  size_t Available() const override;

  // If there are more bytes left, sets "*result" to a non-zero number of bytes
  // read from the cord, and returns true. "*result" is guaranteed to reference
  // valid memory as long as the underlying Cord is not modified.
  // Otherwise, returns false and leaves the contents of *result" unchanged.
  bool ReadFragment(absl::string_view* absl_nonnull result);

  // REQUIRES: n <= Available()
  // Copies the first n bytes into dest and advances past them.
  void ReadN(size_t n, char* absl_nonnull dest);

  // Return the next fragment of available data, or an empty string_view value
  // if no more data is available. The returned value is guaranteed to reference
  // valid memory as long as the underlying Cord is not modified.
  // PeekFragment() does *NOT* skip past the returned fragment; a subsequent
  // call to PeekFragment() or ReadFragment() will return the same bytes.
  absl::string_view PeekFragment();

  // If there are more bytes left, sets "*result" to a non-zero number of bytes
  // read from the cord, and returns true. Otherwise, returns false and leaves
  // the contents of *result" unchanged. This function is equivalent to:
  //   if (PeekFragment().empty()) return false;
  //   *result = PeekFragment();
  //   return true;
  bool PeekFragment(absl::string_view* absl_nonnull result);

  // Variant of the above.  The returned region is empty iff
  // Available() == 0, and is valid until the next call to Skip().
  // The result is guaranteed to reference valid memory as long as
  // the underlying Cord is not modified.
  absl::string_view Peek() override;

  // Skip the next n bytes.  Caller is responsible for verifying that
  // "n" bytes are available before calling (i.e. Available() >= n).
  void Skip(size_t n) override;

  // Reads a little-endian uint32_t from the reader and stores it in "*result".
  // Returns true if successful, false if enough bytes are not available.
  bool Read32(uint32_t* absl_nonnull result);

  // Reads a little-endian uint64_t from the reader and stores it in "*result".
  // Returns true if successful, false if enough bytes are not available.
  bool Read64(uint64_t* absl_nonnull result);

  // Reads the next n bytes from the Cord and returns them as another Cord.
  absl::Cord ReadCord(size_t n);

  // Reads the next n bytes from the Cord and writes them to the given
  // ByteSink. The sink may share the memory with the Cord's internal
  // representation, e.g. CordByteSink.
  void CopyTo(strings::ByteSink* absl_nonnull sink, size_t n) override;

 private:
  // REQUIRES: n != 0
  void ReadNSlowPath(size_t n, char* absl_nonnull dst);

  bool Read32SlowPath(uint32_t* absl_nonnull result);

  // Resets the CordReader to an empty state.
  void ResetInternal();
  // Resets the CordReader from the provided cord.
  void ResetInternal(const absl::Cord* absl_nonnull cord);

  // Btree specific navigation functions.
  bool BtreeAdvance();
  void BtreeSkipSlowPath(size_t n);
  absl::Cord BtreeReadCord(size_t n);

  bool Advance();

  // Underlying Cord
  const absl::Cord* absl_nonnull cord_;

  // Total length of Cord being read. Immutable after construction or Reset.
  size_t length_;

  // Number of bytes read. It is the position of the char @ data_ + limit_.
  //
  // INVARIANT: used_ <= length_
  size_t used_;

  using CordRep = absl::cord_internal::CordRep;
  using CordRepBtree = absl::cord_internal::CordRepBtree;
  using CordRepBtreeReader = absl::cord_internal::CordRepBtreeReader;

  // Current range
  absl::string_view current_chunk_;

  // The current data edge if we are reading from a single data edge.
  CordRep* absl_nullable current_edge_;

  // Cord reader for cord btrees. Empty if not traversing a btree.
  CordRepBtreeReader btree_reader_;

  // Customized implementations of CopyTo() to allow sharing.
  void CopyToWithSharing(strings::ByteSink* absl_nonnull, size_t n);
  void CopyToCord(CordByteSink* absl_nonnull sink, size_t n);
};

inline void CordReader::ResetInternal() {
  current_chunk_ = {};
  used_ = length_ = 0;
  current_edge_ = nullptr;
  btree_reader_.Reset();
}

inline void CordReader::ResetInternal(const absl::Cord* absl_nonnull cord) {
  assert(cord != nullptr);

  cord_ = cord;
  length_ = cord->size();
  btree_reader_.Reset();
  current_edge_ = nullptr;

  if (CordRep* tree = cord->contents_.tree()) {
    if (ABSL_PREDICT_TRUE(tree->length != 0)) {
      tree = absl::cord_internal::SkipCrcNode(tree);
      if (tree->IsBtree()) {
        current_chunk_ = btree_reader_.Init(tree->btree());
      } else {
        current_chunk_ = absl::cord_internal::EdgeData(tree);
        current_edge_ = tree;
      }
    } else {
      current_chunk_ = {};
    }
  } else {
    current_chunk_ = {cord->contents_.as_chars(), length_};
  }
  used_ = current_chunk_.size();
}

inline CordReader::CordReader() { ResetInternal(); }

inline CordReader::CordReader(const absl::Cord& src) { ResetInternal(&src); }

inline void CordReader::Reset() { ResetInternal(); }

inline void CordReader::Reset(const absl::Cord& cord) { ResetInternal(&cord); }

inline size_t CordReader::Position() const {
  return used_ - current_chunk_.size();
}

inline const absl::Cord& CordReader::cord() const { return *cord_; }

inline size_t CordReader::Available() const {
  return (length_ - used_) + current_chunk_.size();
}

inline bool CordReader::done() const { return Available() == 0; }

inline bool CordReader::Advance() {
  return btree_reader_ ? BtreeAdvance() : false;
}

inline absl::string_view CordReader::PeekFragment() {
  return (!current_chunk_.empty() || Advance()) ? current_chunk_
                                                : absl::string_view{};
}

inline bool CordReader::PeekFragment(absl::string_view* absl_nonnull result) {
  if (current_chunk_.empty() && !Advance()) return false;
  *result = current_chunk_;
  return true;
}

inline absl::string_view CordReader::Peek() {
  absl::string_view result;
  PeekFragment(&result);
  return result;
}

inline void CordReader::Skip(const size_t n) {
  assert(n <= Available());

  // Fast path: can we just advance data_?
  if (n <= current_chunk_.size()) {
    current_chunk_.remove_prefix(n);
    return;
  }
  if (ABSL_PREDICT_FALSE(n >= Available())) {
    used_ = length_;
    current_chunk_ = {};
    return;
  }
  if (btree_reader_) BtreeSkipSlowPath(n);
}

inline void CordReader::ReadN(const size_t n, char* absl_nonnull const dest) {
  // Fast path: can we read from [data_, limit_)?
  if (n <= current_chunk_.size()) {
    memcpy(dest, current_chunk_.data(), n);
    current_chunk_.remove_prefix(n);
    return;
  }

  ReadNSlowPath(n, dest);
}

inline bool CordReader::Read32(uint32_t* absl_nonnull const result) {
  // Fast path: can we read from [current_chunk_)?
  if (sizeof(*result) <= current_chunk_.size()) {
    memcpy(reinterpret_cast<char*>(result), current_chunk_.data(),
           sizeof(*result));
    if constexpr (absl::endian::native != absl::endian::little) {
      *result = absl::byteswap(*result);
    }
    current_chunk_.remove_prefix(sizeof(*result));
    return true;
  }

  return Read32SlowPath(result);
}

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_CORD_BYTESTREAM_H_
