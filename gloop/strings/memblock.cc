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

#include "gloop/strings/memblock.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/layout.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/config.h"

#if ABSL_HAVE_MMAP
#include <sys/mman.h>
#endif  // ABSL_HAVE_MMAP

#if GOOGLE_HAVE_MLOCK || ABSL_HAVE_MMAP
#include <unistd.h>
#endif  // GOOGLE_HAVE_MLOCK || ABSL_HAVE_MMAP

#include <ios>  // for std::dec and std::hex
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"

#if ABSL_HAVE_MMAP

// munmap grabs a lock in the kernel for write, so if there's an ongoing munmap,
// other mmaps and munmaps will block until it finishes.
// For some services, big munmaps may take hundreds of milliseconds, which may
// disrupt serving. This flag allows splitting big munmaps into smaller ones to
// prevent that.
ABSL_FLAG(uint64_t, mmappedmemblock_munmap_pages_per_call, 0,
          "Number of pages to pass to each call of munmap when destroying an "
          "MMappedMemBlock. Set to 0 to pass the whole region to munmap in "
          "one call (default behaviour).");

// madvise(MADV_DONTNEED) might take very long time to complete on a big memory
// region, so for some services it makes sense to chunk it into smaller page to
// reduce the contention for mmap semaphore inside kernel.
ABSL_FLAG(uint64_t, mmappedmemblock_madvdontneed_pages_per_call, 0,
          "Number of pages to pass to each call of madvise when releasing the "
          "memory held by MMappedMemBlock. Set to 0 to pass the whole region to"
          "madvise in one call (default behaviour).");

#endif  // ABSL_HAVE_MMAP

namespace strings {

#if ABSL_HAVE_MMAP

ABSL_CONST_INIT static absl::Mutex total_lock(absl::kConstInit);
// Total amount of mmapped memory.
static int64_t total_mmapped ABSL_GUARDED_BY(total_lock) = 0;

#endif  // ABSL_HAVE_MMAP

/*virtual*/ MemBlock::~MemBlock() {}

void MemBlock::DiscardPrefix(size_t n) {
  CHECK_LE(n, length());
  data_ = reinterpret_cast<char*>(data_) + n;
  length_ -= n;
}

void MemBlock::DiscardSuffix(size_t n) {
  CHECK_LE(n, length());
  length_ -= n;
}

namespace {
class SwissMemblock final : public strings::MemBlock {
#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
  using L = absl::container_internal::Layout<
      SwissMemblock, absl::container_internal::Aligned<
                         char, __STDCPP_DEFAULT_NEW_ALIGNMENT__>>;
#else
  using L = absl::container_internal::Layout<
      SwissMemblock,
      absl::container_internal::Aligned<char, alignof(max_align_t)>>;
#endif

 public:
  ~SwissMemblock() override;

#if defined(__cpp_lib_destroying_delete)
  static void operator delete(SwissMemblock* p, std::destroying_delete_t);
#else
  static void operator delete(void* p);
#endif
  static void* operator new(size_t) = delete;
  static void* operator new(size_t, void* absl_nonnull ptr) { return ptr; }
  Stats GetStats() const override {
    Stats stats = MemBlock::GetStats();
    // Memblock and data share the same chunk.
    stats.memory_usage = alloc_size(orig_length());
    return stats;
  }

 private:
  SwissMemblock(char* absl_nonnull block, size_t length)
      : MemBlock(block, length) {}

  // Creates a memblock of size `length` using a single allocation.
  // data() on the resulting memblock is aligned to
  // __STDCPP_DEFAULT_NEW_ALIGNMENT__.
  static std::unique_ptr<MemBlock> Create(size_t length);

  static size_t alloc_size(size_t length) {
    constexpr size_t kHeaderSize = L::Partial(1).Offset<1>();
    CHECK_LE(length, std::numeric_limits<size_t>::max() - kHeaderSize);
    // alloc_size has to be at least size_t because we use the space to store
    // one size_t value in destructor to allow sized delete.
    return L(1, std::max<size_t>(length, sizeof(size_t))).AllocSize();
  }

  friend class MemBlock;
};

SwissMemblock::~SwissMemblock() {
#if GOOGLE_HAVE_MLOCK
  MUnlock();  // Be safe - free() in libc may not call munmap() right away
#endif        // GOOGLE_HAVE_MLOCK
#if !defined(__cpp_lib_destroying_delete)
  // To enable sized delete we store alloc_size in the data to retrieve it from
  // there in operator delete. Note that we can't access orig_length after this
  // destructor returns.
  size_t bytes = alloc_size(orig_length());
  memcpy(L::Partial(1).template Pointer<char>(reinterpret_cast<char*>(this)),
         &bytes, sizeof(size_t));
#endif  // !__cpp_lib_destroying_delete
}

#if defined(__cpp_lib_destroying_delete)
void SwissMemblock::operator delete(SwissMemblock* p,
                                    std::destroying_delete_t) {
  size_t bytes = alloc_size(p->orig_length());
  p->~SwissMemblock();
  std::allocator<char> allocator;
  absl::container_internal::Deallocate<L::Alignment()>(&allocator, p, bytes);
}
#else
void SwissMemblock::operator delete(void* p) {
  char* ptr = reinterpret_cast<char*>(p);
  size_t bytes;
  // retrieve original alloc_size from data (stored there in destructor).
  memcpy(&bytes, L::Partial(1).template Pointer<char>(ptr), sizeof(size_t));
  std::allocator<char> allocator;
  absl::container_internal::Deallocate<L::Alignment()>(&allocator, ptr, bytes);
}
#endif

std::unique_ptr<MemBlock> SwissMemblock::Create(size_t length) {
  std::allocator<char> allocator;
  char* block =
      static_cast<char*>(absl::container_internal::Allocate<L::Alignment()>(
          &allocator, alloc_size(length)));
  return std::unique_ptr<MemBlock>(new (block) SwissMemblock(
      L::Partial(1).template Pointer<char>(block), length));
}

}  // namespace

std::unique_ptr<MemBlock> MemBlock::New(size_t size) {
  if (size == 0) {
    return std::make_unique<NoCleanupMemBlock>(nullptr, 0);
  }
  if ((size & 1023) == 0 || (size & 1023) + sizeof(SwissMemblock) > 1024) {
    // If allocating SwissMemblock with data would cause us to use a higher
    // allocator alloc class then we fallback to NewedMemBlock.
    return std::make_unique<NewedMemBlock>(size);
  }
  return SwissMemblock::Create(size);
}

#if GOOGLE_HAVE_MLOCK
bool MemBlock::MLockGeneral(bool allow_partial, size_t mlock_stride,
                            int64_t max_bytes) {
  // sysconf(_SC_PAGESIZE) is cheap compared to mlock().
  // do not bother caching.
  const size_t pagesize = sysconf(_SC_PAGESIZE);

  // Round to pagesize
  if (mlock_stride > 0) {
    mlock_stride = ((mlock_stride + pagesize - 1) / pagesize) * pagesize;
  }

  size_t target_length;  // We want to mlock this much if possible
  if (max_bytes != MLOCK_ALL && orig_length_ > static_cast<size_t>(max_bytes))
    target_length = max_bytes;
  else
    target_length = orig_length_;

  int failures = 0;
  while (mlock_length_ + pagesize <= target_length) {
    // Try to mlock some more data
    char* ptr = reinterpret_cast<char*>(orig_data_) + mlock_length_;
    size_t leftover = target_length - mlock_length_;
    size_t mlock_bytes = leftover;
    if (mlock_stride != 0 && mlock_stride < leftover) {
      // Try to lock just "mlock_stride" bytes here
      mlock_bytes = mlock_stride;
    }

    int r = mlock(ptr, mlock_bytes);
    VLOG(1) << "mlock: " << mlock_bytes << ": " << r;
    if (r == 0) {
      mlock_length_ += mlock_bytes;
    } else if (allow_partial && (errno == ENOMEM)) {
      failures++;
      if ((failures < 10) && (mlock_bytes >= 10 * pagesize)) {
        // Reduce request size to 90% and retry
        mlock_stride = (mlock_bytes / 10) * 9;
        mlock_stride = ((mlock_stride + pagesize - 1) / pagesize) * pagesize;
        VLOG(3) << "Reducing mlock amount to " << mlock_stride;
      } else {
        // Too many ENOMEM errors: just live with what we have so far
        return (mlock_length_ > 0);
      }
    } else {
      // Error!
      VLOG(1) << "mlock error: " << strerror(errno);
      MUnlock();
      return false;
    }
  }
  VLOG(3) << "Mlocked " << mlock_length_;
  return true;
}

void MemBlock::MUnlock() {
  if (mlock_length_ > 0) {
    // solaris wants orig_data to be a char*.  Nobody else cares.
    int r = munlock(reinterpret_cast<char*>(orig_data_), mlock_length_);
    VLOG(1) << "munlock: " << mlock_length_ << ": " << r;
    CHECK_EQ(r, 0) << " Could not unlock memory " << std::hex << orig_data_
                   << " " << std::dec << mlock_length_ << " "
                   << strerror(errno);
    VLOG(3) << "Unlocked memory at " << std::hex << orig_data_ << " "
            << std::dec << mlock_length_;
    mlock_length_ = 0;
  }
}
#endif  // GOOGLE_HAVE_MLOCK

MemBlock::Stats MemBlock::GetStats() const {
  Stats stats;
  stats.memory_usage = sizeof(*this) + orig_length_;
  return stats;
}

MallocedMemBlock::MallocedMemBlock(size_t length)
    : MemBlock(malloc(length), length) {
  assert(orig_data() != nullptr);  // Out of memory
}

MallocedMemBlock::MallocedMemBlock(void* absl_nonnull space, size_t length)
    : MemBlock(space, length) {
  assert(orig_data() != nullptr);
}

/*virtual*/ MallocedMemBlock::~MallocedMemBlock() {
#if GOOGLE_HAVE_MLOCK
  MUnlock();  // Be safe - free() in libc may not call munmap() right away
#endif        // GOOGLE_HAVE_MLOCK
  free(orig_data());
}

NewedMemBlock::NewedMemBlock(size_t length)
    : MemBlock(new char[length], length) {}

NewedMemBlock::NewedMemBlock(char* absl_nonnull space, size_t length)
    : MemBlock(space, length) {
  assert(orig_data() != nullptr);
}

/*virtual*/ NewedMemBlock::~NewedMemBlock() {
#if GOOGLE_HAVE_MLOCK
  MUnlock();  // Be safe - free() in libc may not call munmap() right away
#endif        // GOOGLE_HAVE_MLOCK
  ::operator delete[](static_cast<char*>(orig_data()), orig_length());
}

StringDataMemBlock::StringDataMemBlock(
    absl_nonnull std::unique_ptr<std::string> s)
    : MemBlock(const_cast<char*>(s->data()), s->size()), s_(std::move(s)) {
  assert(orig_data() != nullptr);
}

/*virtual*/ StringDataMemBlock::~StringDataMemBlock() {
#if GOOGLE_HAVE_MLOCK
  MUnlock();  // Be safe - string destructor may not call munmap() right away
#endif        // GOOGLE_HAVE_MLOCK
}

#if ABSL_HAVE_MMAP
MMappedMemBlock::MMappedMemBlock(void* data, size_t length)
    : MemBlock(data, length), munmap_(true) {
  VLOG(3) << "Created MMappedMemBlock: address=" << std::hex << data
          << ", length=" << std::dec << length;
  absl::MutexLock l(total_lock);
  total_mmapped += length;
}

namespace {

// Calls madvise(MADV_DONTNEED) on the chunk of RAM, in small chunks. This leads
// to RAM being returned to the operating system without actually unmapping that
// memory. Doing this in chunks is necessary to reduce the time the lock inside
// kernel is held at once.
//
// The reason to do madvise with MADV_DONTNEED before doing munmap is,
// madvise only grabs mmap semaphore inside kernel for reading, and so does
// not block minor page faults that are happening in parallel. MADV_DONTNEED
// on Linux cleans the stuff in the page table, so the munmap that follows,
// which locks the mmap semaphore for write, executes quickly.
// Context and history in http://b/37752123#comment296 and
// <internal thread>
//
// Returns false if madvise failed or was not done, true otherwise
bool ReleaseMemoryInChunks(char* absl_nonnull start, size_t size) {
// Not all platforms support MADV_DONTNEED. For example, Mac OS X doesn't.
// So disable this code for the platforms that do.
#ifdef MADV_DONTNEED
  const size_t pages_per_chunk =
      absl::GetFlag(FLAGS_mmappedmemblock_madvdontneed_pages_per_call);
  size_t page_size = sysconf(_SC_PAGESIZE);
  bool all_chunks_succeeded = true;
  if (pages_per_chunk > 0) {
    while (size >= page_size * 2) {
      // Determine the actual page size, by trying to madvise the second page
      // with different sizes until it succeeds.
      // Release second page and not the first because madvise only cares about
      // the alignment of offset and not size.
      if (madvise(start + page_size, page_size, MADV_DONTNEED) < 0) {
        page_size *= 2;
        continue;
      }

      // Right page size found. Release the first page.
      // It's possible for a user to mlock arbitrary subregion, so don't
      // CHECK-fail on error.
      if (madvise(start, page_size, MADV_DONTNEED) < 0) {
        PLOG(ERROR) << "madvise(MADV_DONT_NEED)";
        all_chunks_succeeded = false;
      }
      start += page_size * 2;
      size -= page_size * 2;
      const size_t chunk_size = page_size * pages_per_chunk;
      // Release the remaining chunks.
      while (size >= chunk_size) {
        // It's possible for a user to mlock arbitrary subregion, so don't
        // CHECK-fail on error.
        if (madvise(start, chunk_size, MADV_DONTNEED) < 0) {
          PLOG(ERROR) << "madvise()";
          all_chunks_succeeded = false;
        }
        if (sched_yield() < 0) {
          PLOG(ERROR) << "sched_yield()";
        }
        start += chunk_size;
        size -= chunk_size;
      }
      break;
    }
  }
  if (size != 0) {
    if (madvise(start, size, MADV_DONTNEED) < 0) {
      // Allow EINVAL because it might happen due to the reasons that don't mean
      // a bug, such as the memory block coming from hugetlbfs
      if (errno != EINVAL) PLOG(ERROR) << "madvise(MADV_DONTNEED)";
      return false;
    }
  }

  return all_chunks_succeeded;
#else
  // MADV_DONTNEED is unsupported on the platform, just return
  return false;
#endif  // MADV_DONTNEED
}

}  // namespace

MMappedMemBlock::~MMappedMemBlock() {
  if (munmap_) {
    char* start = reinterpret_cast<char*>(orig_data());
    size_t size = orig_length();
    bool memory_released = ReleaseMemoryInChunks(start, size);
    const size_t pages_per_chunk =
        absl::GetFlag(FLAGS_mmappedmemblock_munmap_pages_per_call);
    // Chunked munmap is only still potentially useful if ReleaseMemoryInChunks
    // failed, such as mlocked memory. This functionality might be removed in
    // the future.
    if (pages_per_chunk > 0 && !memory_released) {
      size_t page_size = sysconf(_SC_PAGESIZE);
      while (size >= page_size * 2) {
        // Determine the actual page size, by trying to munmap the second page
        // with different sizes until it succeeds.
        // Unmap second page and not the first because munmap only cares about
        // the alignment of offset and not size.
        //
        // There's a similar code in ReleaseMemoryInChunks, but we can't rely on
        // it here because we only run this code if ReleaseMemoryInChunks failed
        if (munmap(start + page_size, page_size) == 0) {
          // Right page size found. Unmap the first page.
          PCHECK(munmap(start, page_size) == 0);
          start += page_size * 2;
          size -= page_size * 2;
          const size_t chunk_size = page_size * pages_per_chunk;
          // Unmap the remaining chunks
          while (size >= chunk_size) {
            PCHECK(munmap(start, chunk_size) == 0);
            start += chunk_size;
            size -= chunk_size;
          }
          break;
        }
        PCHECK(errno == EINVAL);
        page_size *= 2;
      }
    }
    if (size != 0) {
      PCHECK(munmap(start, size) == 0);
    }
  }
  absl::MutexLock l(total_lock);
  total_mmapped -= orig_length();
}

int64_t MMappedMemBlock::TotalMappedBytes() {
  absl::MutexLock l(total_lock);
  return total_mmapped;
}
#endif  // ABSL_HAVE_MMAP

NoCleanupMemBlock::~NoCleanupMemBlock() {}

AlignedMemBlock::AlignedMemBlock(size_t length, size_t alignment)
    : MemBlock(length == 0 ? nullptr
                           : operator new(length, std::align_val_t{alignment}),
               length),
      alignment_(std::align_val_t{alignment}) {
  CHECK_EQ(0u, alignment & (alignment - 1))
      << " alignment (" << alignment << ") is not a power of 2.";
  if (length == 0) {
    CHECK(orig_data() == nullptr);
    CHECK(data() == nullptr);
  } else {
    CHECK(orig_data() != nullptr) << "out of memory";
    CHECK_EQ(0u, reinterpret_cast<uintptr_t>(data()) & (alignment - 1));
  }
  CHECK_EQ(length, this->length());
}

AlignedMemBlock::~AlignedMemBlock() {
#if GOOGLE_HAVE_MLOCK
  MUnlock();  // Be safe - aligned_free() may not call munmap() right away
#endif        // GOOGLE_HAVE_MLOCK
  operator delete(orig_data(), orig_length(), alignment_);
}

absl::Cord CordFromMemBlock(
    absl_nonnull std::unique_ptr<const MemBlock> block) {
  absl::string_view block_view = block->ToStringPiece();
  auto releaser = [b = std::move(block)] {};
  return absl::MakeCordFromExternal(block_view, std::move(releaser));
}

void NoopReleaser(void* absl_nullable) {}
void DeleteMemBlock(void* absl_nullable arg) {
  delete static_cast<MemBlock*>(arg);
}
void DeleteString(void* absl_nullable arg) {
  delete static_cast<std::string*>(arg);
}
void DeleteCharArray(void* absl_nullable arg) {
  delete[] static_cast<char*>(arg);
}
}  // namespace strings
