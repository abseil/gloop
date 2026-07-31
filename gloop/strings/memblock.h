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

// This module implements a variable-sized memory block abstraction.
// Subclasses can be defined to allocate/deallocate the blocks in
// different ways.

#ifndef THIRD_PARTY_GLOOP_STRINGS_MEMBLOCK_H_
#define THIRD_PARTY_GLOOP_STRINGS_MEMBLOCK_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cstdint>
#include <memory>
#include <new>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gloop/base/config.h"

#if ABSL_HAVE_MMAP

#include "absl/flags/declare.h"

ABSL_DECLARE_FLAG(uint64_t, mmappedmemblock_munmap_pages_per_call);
ABSL_DECLARE_FLAG(uint64_t, mmappedmemblock_madvdontneed_pages_per_call);
#endif

namespace strings {

#if GOOGLE_HAVE_MLOCK
// Use these as the mlock_bytes parameter to MLockGeneral().
enum { MLOCK_ALL = -1, MLOCK_NONE = 0 };
#endif  // GOOGLE_HAVE_MLOCK

class MemBlock {
 public:
  MemBlock(const MemBlock&) = delete;
  MemBlock& operator=(const MemBlock&) = delete;
  virtual ~MemBlock();

  void* absl_nullable data() { return data_; }
  const void* absl_nullable data() const { return data_; }
  size_t length() const { return length_; }

  // Original pointer/size passed to constructor (before adjusts)
  void* absl_nullable orig_data() { return orig_data_; }
  const void* absl_nullable orig_data() const { return orig_data_; }
  size_t orig_length() const { return orig_length_; }

  // Helper routines to reduce the extent of the visible block.  These
  // do not affect orig_data/orig_length, so those values can be used
  // for appropriate cleanup in the destructors.
  //
  // It is an error to call these routines with "n > length()".
  // Also note, multiple discard operations are cumulative.
  // E.g., two calls to "DiscardPrefix(10)" are equivalent to
  // one call to "DiscardPrefix(20)".
  void DiscardPrefix(size_t n);
  void DiscardSuffix(size_t n);

  // Allocates a new memblock object and storage on heap with a single memory
  // allocation.
  //
  // data() is aligned to __STDCPP_DEFAULT_NEW_ALIGNMENT__. This is the same
  // alignment you will get if you allocate a char[] using operator new (new
  // char[size]).
  //
  // Prefer to use this MemBlock instead of MallocedMemBlock.
  static std::unique_ptr<MemBlock> New(size_t size);

#if GOOGLE_HAVE_MLOCK
  // Try to mlock the contents of the block.  Returns true iff
  // some portion of the block was mlocked.
  //
  // If "mlock_stride" is non-zero, split up individual mlock calls
  // into smaller mlock calls of size "mlock_stride" bytes each.  This
  // is useful to prevent the process from hanging for a long time on
  // a large mlock call.
  //
  // If "allow_partial" is true, allow the block to be partially
  // locked if we start running into memory limits.  If
  // "allow_partial" is false, either the entire block is locked, or
  // none of it is locked.  Note that the all-or-nothing property
  // holds even if "mlock_stride" is specified.  The amount of
  // memory locked can be found by calling "mlocked_length()".
  //
  // "max_bytes" specifies the maximum number of bytes to mlock.  If
  // the block is larger than "max_bytes", then MLockGeneral will
  // attempt to lock the initial "max_bytes" bytes of the block.
  // Fewer bytes may be mlocked if "allow_partial" is true and memory
  // is tight.  Specifying "max_bytes" of MLOCK_ALL indicates no
  // maximum; MLOCK_NONE indicates that nothing should be mlocked.
  virtual bool MLockGeneral(bool allow_partial, size_t mlock_stride,
                            int64_t max_bytes);

  // Simpler interface for mlocking.  Equivalent to:
  //    MLockGeneral(true, 0, MLOCK_ALL);
  // I.e. partial mlocking is allowed, and memory is locked in one
  // fell swoop.
  bool MLock() {
    return MLockGeneral(/*allow_partial=*/true, /*mlock_stride=*/0, MLOCK_ALL);
  }

  // Unlock any locked region of memory.  It is okay to call this
  // routine even if none of this block is current mlocked.
  virtual void MUnlock();
#endif  // GOOGLE_HAVE_MLOCK

  // Return the size of the prefix of this block which is mlocked
  // in memory.  If the block is completely locked, this size will
  // be the same as "orig_length()".
  size_t mlocked_length() const { return mlock_length_; }

  // The return type for 'GetStats' below. As more fields are added, they
  // should be initialized here to a value that callers can interpret as
  // not having been set.
  struct Stats {
    // Number of cache hits for this MemBlock. -1 means not known.
    int cache_hits;

    // Number of memory bytes owned by this MemBlock. In other words, deleting
    // this MemBlock will return such amount of memory to the system. -1 means
    // not known.
    // N.B. orig_length() is not always a good approximation for the defined
    // value (see IOBufferMemBlock for an example).
    int64_t memory_usage;

    Stats() : cache_hits(-1), memory_usage(-1) {}
  };

  // Returns a Stats struct containing statistics for this MemBlock.  Derived
  // classes that implement GetStats() should return a modified copy of their
  // parent's statistics to make it easier to add statistics to the parent
  // classes in the future.
  virtual Stats GetStats() const;

  // For use with APIs that prefer a string_view.
  absl::string_view ToStringPiece() const {
    return absl::string_view(static_cast<const char*>(data()), length());
  }

 protected:
  // For use by subclasses
  MemBlock(void* absl_nullable data, size_t length) {
    data_ = orig_data_ = data;
    length_ = orig_length_ = length;
    mlock_length_ = 0;
  }

 private:
  void* data_;
  size_t length_;
  void* orig_data_;
  size_t orig_length_;
  size_t mlock_length_;  // How much of orig_data_ was mlocked
};

// Data block is malloced at start, and freed at end
class ABSL_DEPRECATED(
    "Use MemBlock::New() or AlignedMemBlock instead, depending on alignment "
    "requirements") MallocedMemBlock : public MemBlock {
 public:
  explicit MallocedMemBlock(size_t length);
  MallocedMemBlock(const MallocedMemBlock&) = delete;
  MallocedMemBlock& operator=(const MallocedMemBlock&) = delete;
  // A constructor for clients who want to malloc their own memory.
  // Ownership of the block of data transfers to the newly allocated
  // MallocedMemBlock, which will free it when its destructor is called.
  MallocedMemBlock(void* absl_nonnull space, size_t length);
  ~MallocedMemBlock() override;
};

class ABSL_DEPRECATED("Use MemBlock::New() instead") NewedMemBlock final
    : public MemBlock {
 public:
  // Constructs (via new char[length]) a block of size length.
  explicit NewedMemBlock(size_t length);

  // Data block 'space' of specified 'length' must have been created by
  // 'new char[length]', and it will be delete[]'ed when this object is
  // destroyed.
  ABSL_DEPRECATED("Use the single argument constructor instead")
  NewedMemBlock(char* absl_nonnull space, size_t length);
  NewedMemBlock(const NewedMemBlock&) = delete;
  NewedMemBlock& operator=(const NewedMemBlock&) = delete;
  ~NewedMemBlock() override;
};

// Memory block from string data.
// This class takes ownership of the string, and deletes it in its destructor.
class StringDataMemBlock : public MemBlock {
 public:
  explicit StringDataMemBlock(absl_nonnull std::unique_ptr<std::string> s);

  ABSL_DEPRECATE_AND_INLINE()
  explicit StringDataMemBlock(std::string* absl_nonnull s)
      : StringDataMemBlock(absl::WrapUnique(s)) {}
  StringDataMemBlock(const StringDataMemBlock&) = delete;
  StringDataMemBlock& operator=(const StringDataMemBlock&) = delete;
  ~StringDataMemBlock() override;

 private:
  std::unique_ptr<std::string> s_;
};

#if ABSL_HAVE_MMAP

// Memory block obtained from an mmap call.
class MMappedMemBlock : public MemBlock {
 public:
  // REQUIRES: 'data' is page-aligned.
  MMappedMemBlock(void* absl_nonnull data, size_t length);
  MMappedMemBlock(const MMappedMemBlock&) = delete;
  MMappedMemBlock& operator=(const MMappedMemBlock&) = delete;

  // Release all resources: munlock any locked region, and also
  // munmap the mapped memory (if munmap_ is true).
  ~MMappedMemBlock() override;

  // Should we munmap on delete?
  void munmap_on_delete(bool delete_it) { munmap_ = delete_it; }

  // Return total number of bytes currently mapped in the process.
  static int64_t TotalMappedBytes();

 protected:
  bool munmap_;  // munmap on delete?
};
#endif  // ABSL_HAVE_MMAP

// A MemBlock whose destructor does nothing.  Useful when some higher
// level is responsible for allocation/deletion of the actual data
// blocks.
class NoCleanupMemBlock : public MemBlock {
 public:
  NoCleanupMemBlock(void* absl_nullable data, size_t length)
      : MemBlock(data, length) {}
  NoCleanupMemBlock(const NoCleanupMemBlock&) = delete;
  NoCleanupMemBlock& operator=(const NoCleanupMemBlock&) = delete;
  ~NoCleanupMemBlock() override;
};

// A MemBlock that allocates aligned data. This class automatically
// allocates and frees the data, and makes sure the data is aligned on
// a specified block size boundary, which must be a power of two.
class AlignedMemBlock : public MemBlock {
 public:
  // Initializes the MemBlock by allocating 'length' bytes of data aligned on
  // the specified 'alignment' size boundary.
  //
  // For proper operation, alignment must be a power of 2 and length +
  // alignment <= SIZE_MAX.
  //
  // It is valid to create a AlignedMemBlock with length 0. The value for data()
  // in that case will be nullptr.
  //
  // Sample usage 1, read data into an int64 aligned buffer:
  //
  //   // Read data requiring 64 bits alignment.
  //   AlignedMemBlock memblock(data_size, sizeof(int64));
  //   auto status = some_object.ReadData(memblock.data(), data_size);
  //   if (status.ok()) {
  //     // Etc.
  //   }
  //
  // Sample usage 2, read memblock data into Cord:
  //
  //   // Read from some block device requiring block aligned memory.
  //   auto memblock = absl::make_unique<AlignedMemBlock>(
  //                       data_size, block_device.block_size()));
  //   auto status = block_device.ReadBlockData(memblock->data(), data_size);
  //   if (status.ok()) {
  //     Cord cord = CordFromMemBlock(std::move(memblock));
  //     // Etc.
  //   }
  AlignedMemBlock(size_t length, size_t alignment);
  AlignedMemBlock(const AlignedMemBlock&) = delete;
  AlignedMemBlock& operator=(const AlignedMemBlock&) = delete;
  // Releases the allocated memory contained in this memory block.
  ~AlignedMemBlock() override;

 private:
  // Hold the alignment so that it can be passed to operator delete.
  const std::align_val_t alignment_;
};

// Create a cord with the same contents as "*block".
//
// Beware when using this with MemBlock implementations that do not own the
// underlying memory (e.g. NoCleanupMemBlock). In such cases, all copies of
// the Cord must be destroyed before the memory can be freed.
absl::Cord CordFromMemBlock(absl_nonnull std::unique_ptr<const MemBlock> block);

// Releaser functions to delete external memory appended to Cord.

// This releaser does nothing.
// WARNING: It's likely a bug if Cord::AppendExternalMemory() is called with
// this releaser. For example, consider the following:
// void Foo(const char* buffer, int len) {
//   Cord c;
//   c.AppendExternalMemory(string_view(buffer, len), nullptr,
//                          strings::NoopReleaser);
//
//   // BUG: If Bar() copies its cord for any reason, including keeping a
//   // substring of it, the lifetime of buffer might be extended beyond
//   // when Foo() returns.
//   Bar(c);
// }
void NoopReleaser(void* absl_nullable);
// This releaser calls delete on MemBlock*.
void DeleteMemBlock(void* absl_nullable);
// This releaser calls delete on string*.
void DeleteString(void* absl_nullable);
// This releaser calls delete[] on char*.
void DeleteCharArray(void* absl_nullable);

}  // namespace strings

// Old names for <link>:
using ::strings::MallocedMemBlock;    // NOLINT
using ::strings::MemBlock;            // NOLINT
using ::strings::NewedMemBlock;       // NOLINT
using ::strings::NoCleanupMemBlock;   // NOLINT
using ::strings::StringDataMemBlock;  // NOLINT
#if GOOGLE_HAVE_MLOCK
using ::strings::MLOCK_ALL;  // NOLINT
#endif                       // GOOGLE_HAVE_MLOCK

#endif  // THIRD_PARTY_GLOOP_STRINGS_MEMBLOCK_H_
