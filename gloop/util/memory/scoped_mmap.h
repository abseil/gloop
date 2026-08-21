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

#ifndef THIRD_PARTY_GLOOP_UTIL_MEMORY_SCOPED_MMAP_H_
#define THIRD_PARTY_GLOOP_UTIL_MEMORY_SCOPED_MMAP_H_

#include <stddef.h>

// A wrapper around mmap and munmap which unmaps it's mapped memory
// when it's destructor is called.
// <link>
//
// Recommended pattern of usage:
//   /* map and copy 4KB from the physical address 1MB */
//   const int    file        = open("/dev/mem", O_RDWR);
//   const size_t copyAddress = 0x00100000;
//   const size_t copySize    = 0x00001000;
//   uint8*       localcopy   = new uint8[copySize];
//   {
//     platform::ScopedMmap scoped_mmap;
//     void *address = scoped_mmap.Map(file, 0x10000, 0x10, getpagesize(),
//                                     PROT_READ, MAP_SHARED);
//     memcpy(localcopy, address, copySize);
//   } /* unmaps mapped memory here */
class ScopedMmap {
 public:
  // initializes the memory mapper to unmapped
  ScopedMmap();

  // This type is neither copyable nor movable.
  ScopedMmap(const ScopedMmap&) = delete;
  ScopedMmap& operator=(const ScopedMmap&) = delete;

  // if memory has been mapped by this object, unmap it
  ~ScopedMmap();

  // Map the data specified by the byte offset and size from the
  // file_descriptor into the calling process' address space ensuring the
  // mapped region is aligned to the specified byte alignment
  // returning the address of the mapped region in the process' address space
  // if successful.  If mapping fails this function returns NULL.
  // file_descriptor must be a valid file descriptor and
  // size should be greater than 0.
  // alignment should be the page size returned by getpagesize().
  // mmap_prot specifies the memory protection mode (prot) passed to mmap().
  // mmap_flags specifies the type of mapped object flags (flags) passed
  // to mmap().
  // See mmap() documentation (info mmap) for more information.
  void* Map(const int file_descriptor, const size_t offset, const size_t size,
            const size_t alignment, const int mmap_prot, const int mmap_flags);

  // unmap the mapped file descriptor
  void Unmap();

  // determine whether this object has mapped a region of a file into the
  // calling process' address space
  bool IsMapped() const;

 protected:
  void* process_address_;
  size_t aligned_file_map_size_;

  // reset the internal object's state to unmapped
  void Reset();
};

#endif  // THIRD_PARTY_GLOOP_UTIL_MEMORY_SCOPED_MMAP_H_
