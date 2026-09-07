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

// A simple class that efficiently allocates strings on an arena, storing size
// as either a single byte or 4 bytes. Useful for storing large numbers of small
// strings with a very small memory overhead: 0 bytes for len == 0, 1 byte for
// len < 128, and 4 bytes for everything else. Storing in an arena means there
// is no padding anywhere. The drawbacks are:
// - unaligned loads for sizes >= 128 which might hurt in some CPUs
// - no support for deallocation of individual strings
// - it supports strings up to size 2^31 - 1 bytes
//
// Advantages over c-strings:
// --data may contain NUL bytes
// --size() is O(1)
//
// Compared to string_view:
// --lower memory overhead for short strings
// --slight performance penalty for encoding and decoding the size
//
// This class could be further extended if there's demand, for example by adding
// comparators, support for STL hashing, a Shrink() method, etc.

#ifndef THIRD_PARTY_GLOOP_STRINGS_ARENA_STRING_H__
#define THIRD_PARTY_GLOOP_STRINGS_ARENA_STRING_H__

#include <assert.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "gloop/base/arena.h"
#include "gloop/util/endian/endian.h"

namespace strings {

class ArenaString {
 public:
  // Default constructor creates an empty string.
  ArenaString() : enc_(nullptr) {}

  template <class ArenaPtrOrAlloc>
  ArenaString(absl::string_view str, ArenaPtrOrAlloc a) {
    assign(str, a);
  }

  ArenaString(const ArenaString& other) = default;
  ArenaString& operator=(const ArenaString& other) = default;

  // No cleanup, since memory management is handled by the arena.
  ~ArenaString() = default;

  // Allocates the given string on a BaseArena* or C++ Allocator concept (see
  // https://en.cppreference.com/w/cpp/named_req/Allocator). Note that the
  // ArenaString will not release the memory allocated by the allocator, so the
  // allocator must be backed by an arena.
  template <class ArenaPtrOrAlloc>
  void assign(absl::string_view s, ArenaPtrOrAlloc a) {
    enc_ = s.empty() ? nullptr
                     : EncodeNonEmpty(s, this->Allocate(a, EncSize(s.size())));
  }

  void clear() { enc_ = nullptr; }

  bool empty() const { return !enc_; }

  // Accessors.  Other traditional string methods are available via string_view.
  absl::string_view str() const { return Decode(enc_); }
  int size() const { return Decode(enc_).size(); }
  const char* data() const { return enc_; }

  //////// Static methods for manually encoding and decoding.

  // Computes the encoded size for a string of the given length.  Returns 1 for
  // the empty string, although empty strings may also be encoded as NULL.
  static size_t EncSize(size_t size) {
    assert(size < 0x80000000u);
    return size + (size < 128 ? 1 : 4);
  }

  // Encodes a string into the given buffer, as 1 or 4 bytes of size followed by
  // string data.  Returns pointer to string data. Encode() produces a 1-byte
  // encoding of the empty string, but clients may encode as NULL instead.
  //
  // REQUIRES: buf is non-NULL and has at least EncSize(str) bytes available
  static char* Encode(absl::string_view str, char* buf) {
    assert(buf);
    if (str.empty()) {
      *buf = 0;
      return buf + 1;
    }
    return EncodeNonEmpty(str, buf);
  }

  // Decodes a string.  Returns the empty string if data is NULL.
  static absl::string_view Decode(const char* data) {
    if (!data) return absl::string_view();

    uint32_t len = DecodeLen(data);
    return absl::string_view(data, len);
  }

 private:
  friend class ArenaStringAccess;

  static char* EncodeNonEmpty(absl::string_view str, char* buf) {
    assert(str.size() < 0x80000000u);
    uint32_t size = static_cast<uint32_t>(str.size());
    char* data = EncodeLen(buf, size);
    memcpy(data, str.data(), size);
    return data;
  }

  static char* EncodeLen(char* buf, uint32_t len) {
    if (ABSL_PREDICT_TRUE(len < 128)) {
      *buf = len;
      return buf + 1;
    }
    LittleEndian::Store32(buf, ~len);
    return buf + 4;
  }
  static uint32_t DecodeLen(const char* data) {
    uint32_t len = 0xFF & data[-1];
    if (ABSL_PREDICT_TRUE(len < 128)) {
      return len;
    }
    return ~LittleEndian::Load32(data - 4);
  }

  static char* Allocate(UnsafeArena* a, size_t s) { return a->Alloc(s); }
  static char* Allocate(BaseArena* a, size_t s) { return a->SlowAlloc(s); }
  // Note that by making the return type dependent on a.allocate, this override
  // is only considered for allocators that have an allocate() member function.
  // Thanks SFINAE!
  template <class Alloc>
  static auto Allocate(Alloc a, size_t s)
      -> decltype(typename Alloc::template rebind<char>::other(a).allocate(s)) {
    return typename Alloc::template rebind<char>::other(a).allocate(s);
  }

  const char* enc_;
};

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_ARENA_STRING_H__
