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

// A "shrunk array" is simply a read-only array of uint64 that
// represents a larger array of uint64, or several of them.  It's
// Plain Old Data, not a class, so it can be copied freely.  (Note on
// byte-order:  If you write it to a file or network, it's the integer
// values you need to preserve, not the byte values.)  The class
// ShrunkArray is a collection of static functions and a nested class
// for creating, copying, and interpreting shrunk arrays.
//
// Shrunk arrays are designed to be quick to create and quick to read,
// both serially and randomly.  They have low per-array overhead,
// so that many small shrunk arrays can be created, and random read
// patterns can jump from one to another.

// Example usage:
//
//     #include "gloop/util/coding/shrunk-array.h"
//
//     int raw_size = 17;
//     uint32 *raw_array = new uint32[raw_size];
//     GenerateMyRawData(raw_array);
//
//     vector<uint64> shrunk_vector;
//     uint64 decode_key[2];
//     ShrunkArray::Write(raw_array, raw_size, 3, decode_key, &shrunk_vector);
//     const uint64 *shrunk_array = ShrunkArray::Copy(shrunk_vector);
//
//     ShrunkArray::Reader *reader = ShrunkArray::Reader::New();
//     reader->Bind(shrunk_array, decode_key);
//     uint32 first = reader->Get(0);
//     uint32 last = reader->Get(raw_size - 1);
//     delete reader;

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_SHRUNK_ARRAY_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_SHRUNK_ARRAY_H_

#include <stddef.h>

#include <cstdint>
#include <vector>

#include "absl/types/span.h"

class ShrunkArray {
 public:
  // Write(raw_array, raw_size, complexity, decode_key, &shrunk_vector)
  // reads raw_size elements from raw_array, encodes them, appends the
  // result to shrunk_vector, and writes a decode key that will be
  // needed to recover the raw values from the shrunk array (which will
  // be a copy of shrunk_vector).  The input can be an array of uint64,
  // uint32, uint16, or uint8, but Reader::Get() will return the raw
  // values as uint64 regardless.  The decode key(s) can be stored near
  // the array pointer, for cache locality.
  //
  // You are welcome to call Write() more than once with the
  // same shrunk_vector, and to write other data into the vector
  // before/between/after calls to Write(), but you must not change the
  // value or array index of any elements written by Write().  Putting
  // multiple input arrays in the same shrunk array versus separate
  // shrunk arrays has no performance implication; do whatever is
  // convenient.
  //
  // Extremely small input arrays may not compress well, because there
  // won't be much redundancy to exploit, and each incurs the 128-bit
  // overhead of the decode key.  A single large input array may not be
  // optimal either, because the encoder is denied the opportunity to
  // tailor various decode keys for various portions of the data.  Try
  // to keep data of different kinds in separate input arrays.
  //
  // The complexity parameter must be 1, 2, or 3.  It is the maximum
  // number of memory fetches that Reader::Get() will perform, where
  // each fetch reads one or two array elements from a span of at
  // most three (which will sometimes straddle two cache lines).  The
  // following kinds of redundancy in the input can be exploited,
  // depending on the complexity level:
  //
  // Complexity level 1 can exploit:
  //
  //     All input values have zeros in their most significant bits.
  //
  // Complexity level 2 can additionally exploit:
  //
  //     The distribution of input values is skewed toward zero.
  //
  //     Some input values occur multiple times.
  //
  //  Complexity level 3 can additionally exploit:
  //
  //     Some input values occur more often than others.
  //
  // The following kinds of redundancy are never exploited:
  //
  //     The distribution of input values is concentrated in some
  //     ranges.
  //
  //     Correlation between values and their positions.
  //
  //     Correlation between values and neighboring values (like runs
  //     and repeated sequences).
  //
  // When complexity is 1, decode_key[1] is guaranteed to be zero,
  // so the caller can use Reader::Bind1() and need not preserve
  // decode_key[1].

  static void Write(const uint64_t raw_array[], size_t raw_size, int complexity,
                    uint64_t decode_key[2],
                    std::vector<uint64_t>* shrunk_vector);

  static void Write(const uint32_t raw_array[], size_t raw_size, int complexity,
                    uint64_t decode_key[2],
                    std::vector<uint64_t>* shrunk_vector);

  static void Write(const uint16_t raw_array[], size_t raw_size, int complexity,
                    uint64_t decode_key[2],
                    std::vector<uint64_t>* shrunk_vector);

  static void Write(const uint8_t raw_array[], size_t raw_size, int complexity,
                    uint64_t decode_key[2],
                    std::vector<uint64_t>* shrunk_vector);

  // Why does Write() use method overloading rather than a template?
  // To enforce the limited selection of types it is able to handle,
  // and to enable this .h file to specify an interface free of any
  // implementation details.

  // Copy(shrunk_vector) returns a copy of shrunk_vector as an array
  // allocated by new uint64[shrunk_vector.size()].  The caller is
  // responsible for deleting it.  There is no requirement to use this
  // function; you are welcome to copy the vector by other means.

  static const uint64_t* Copy(absl::Span<const uint64_t> shrunk_vector);

  // A Reader binds to a shrunk array and recovers the original raw
  // values that were passed to Write().
  // Reader is thread-compatible (<link>).

  class Reader {
   public:
    // This type is neither copyable nor movable.
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    virtual ~Reader();

    // New() returns a new reader, which the caller is responsible for
    // deleting.

    static Reader* New();

    // Bind(shrunk_array, decode_key) sets up the reader to recover
    // raw values from the given shrunk_array.  The decode_key must
    // be (or be a copy of) a decode_key written by Write(), and the
    // shrunk_array must have been copied from the shrunk_vector after
    // that call to Write().  Bind() may be called more than once.  It
    // is cheap--it parses the decode_key but does not access the shrunk
    // array itself.

    void Bind(const uint64_t* shrunk_array, const uint64_t decode_key[2]);

    // Bind1 is like Bind except that decode_key[1] is not accessed and
    // need not exist.  It is assumed to be zero, which is always true
    // for arrays written with complexity == 1.

    void Bind1(const uint64_t* shrunk_array, const uint64_t decode_key[1]);

    // Get(position) returns the value of raw_array[position] that was
    // passed to Write().  Requires that Bind() has already been called.
    // This method is non-const because it cannot be called concurrently,
    // even though its side effects are invisible to the caller.
    // Warning:  Just like a regular array, a shrunk array does not know
    // its own size, and out-of-bounds reads can be fatal.

    uint64_t Get(size_t position);

    // There is no GetRange() method because it would not be any faster
    // than simply calling Get() in a forward loop.  A forward loop can
    // be somewhat faster than a backward loop (but either is an order
    // of magnitude faster than random access over arrays that exceed
    // the processor cache).

    // Shorthand:
    uint64_t Get(const uint64_t* shrunk_array, const uint64_t decode_key[2],
                 size_t position) {
      Bind(shrunk_array, decode_key);
      return Get(position);
    }

    // SharedGet is like Get except that SharedGet can be called
    // concurrently, but is slower for sequential access.

    uint64_t SharedGet(size_t position) const;

   protected:
    enum { kHostOrder = true };
    typedef uint64_t ShrunkData;

    Reader();  // Non-public, use New() above.
  };

  // UnalignedLittleEndianReader is like Reader but reads unaligned data
  // encoded as little-endian uint64s.  This is used for decoding
  // shrunk-arrays generated on little-endian machines without buffers
  // for byte-swapping.  The interface is similar to Reader's but all
  // pointers have type void* instead of uint64*.  The decode keys
  // are also expected to be in the same byte-order as the data.

  class UnalignedLittleEndianReader {
   public:
    // This type is neither copyable nor movable.
    UnalignedLittleEndianReader(const UnalignedLittleEndianReader&) = delete;
    UnalignedLittleEndianReader& operator=(const UnalignedLittleEndianReader&) =
        delete;

    virtual ~UnalignedLittleEndianReader();

    static UnalignedLittleEndianReader* New();

    void Bind(const void* shrunk_array, const void* decode_key);
    void Bind1(const void* shrunk_array, const void* decode_key);

    uint64_t Get(size_t position);

    uint64_t Get(const void* shrunk_array, const void* decode_key,
                 size_t position) {
      Bind(shrunk_array, decode_key);
      return Get(position);
    }

    uint64_t SharedGet(size_t position) const;

   protected:
    enum { kHostOrder = false };
    typedef char ShrunkData;

    UnalignedLittleEndianReader();
  };
};

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_SHRUNK_ARRAY_H_
