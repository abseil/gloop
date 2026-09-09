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

#include "gloop/util/memory/memory_mapped_io.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gloop/util/memory/scoped_mmap.h"

MemoryMappedIO::MemoryMappedIO() { Reset(); }

MemoryMappedIO::~MemoryMappedIO() {
  if (IsInitialized()) Terminate();
}

bool MemoryMappedIO::Initialize(absl::string_view memory_device,
                                const OpenMode open_mode) {
  DCHECK(!IsInitialized());

  int open_flag = 0;
  switch (open_mode) {
    case OPEN_MODE_READ_ONLY:
      open_flag = O_RDONLY;
      break;
    case OPEN_MODE_READ_WRITE:
      open_flag = O_RDWR;
      break;
    default:
      LOG(ERROR) << "Invalid open mode " << open_mode;
      return false;
  }

  const int memory_device_file_descriptor =
      open(std::string(memory_device).c_str(), open_flag);
  if (memory_device_file_descriptor >= 0) {
    InitializeUsingFileDescriptor(memory_device_file_descriptor);
    return true;
  } else {
    VLOG(1) << "Unable to open memory device " << memory_device;
  }
  return false;
}

void MemoryMappedIO::InitializeUsingFileDescriptor(const int file_descriptor) {
  CHECK_GE(file_descriptor, 0);
  memory_device_file_descriptor_ = file_descriptor;
  memory_page_size_ = getpagesize();
}

bool MemoryMappedIO::IsInitialized() const {
  return memory_device_file_descriptor_ >= 0 && memory_page_size_;
}

void MemoryMappedIO::Terminate() {
  DCHECK(IsInitialized());
  close(memory_device_file_descriptor_);
  Reset();
}

static void ByteByByteCopy(volatile void* destination_address,
                           volatile const void* const source_address,
                           const size_t size) {
  for (size_t i = 0; i < size; i += sizeof(uint8_t)) {
    reinterpret_cast<volatile uint8_t*>(destination_address)[i] =
        reinterpret_cast<volatile const uint8_t* const>(source_address)[i];
  }
}

static void OptimizedCopy(const size_t memory_device_offset,
                          volatile void* destination_address,
                          volatile const void* const source_address,
                          const size_t size) {
  if (memory_device_offset % size == 0) {
    switch (size) {
      // Special case for native word size reads.  Some MMIO devices are not
      // byte-addressable.
      case sizeof(uint64_t):
        *reinterpret_cast<volatile uint64_t*>(destination_address) =
            *reinterpret_cast<volatile const uint64_t*>(source_address);
        break;
      case sizeof(uint32_t):
        *reinterpret_cast<volatile uint32_t*>(destination_address) =
            *reinterpret_cast<volatile const uint32_t*>(source_address);
        break;
      case sizeof(uint16_t):
        *reinterpret_cast<volatile uint16_t*>(destination_address) =
            *reinterpret_cast<volatile const uint16_t*>(source_address);
        break;
      default:
        ByteByByteCopy(destination_address, source_address, size);
    }
  } else {
    // For unaligned accesses always use byte by byte copies.
    ByteByByteCopy(destination_address, source_address, size);
  }
}

bool MemoryMappedIO::Read(const size_t memory_device_offset, const size_t size,
                          void* const destination_address) const {
  DCHECK(IsInitialized());
  ScopedMmap scoped_mmap;
  const void* const process_address =
      Map(memory_device_offset, size, PROT_READ, MAP_SHARED, &scoped_mmap);
  if (process_address) {
    OptimizedCopy(memory_device_offset, destination_address, process_address,
                  size);
    return true;
  }
  return false;
}

bool MemoryMappedIO::Write(const WriteMode write_mode,
                           const size_t memory_device_offset, const size_t size,
                           const void* const source_address) const {
  DCHECK(IsInitialized());
  // Translate write mode from WriteMode to flags required by mmap().
  int flags = 0;
  switch (write_mode) {
    case WRITE_MODE_SHARED:
      flags = MAP_SHARED;
      break;
    case WRITE_MODE_PRIVATE:
      flags = MAP_PRIVATE;
      break;
    default:
      LOG(ERROR) << "Invalid write mode" << write_mode;
      return false;
  }

  ScopedMmap scoped_mmap;
  void* const process_address =
      Map(memory_device_offset, size, PROT_WRITE, flags, &scoped_mmap);
  if (process_address) {
    OptimizedCopy(memory_device_offset, process_address, source_address, size);
    return true;
  }
  return false;
}

void* MemoryMappedIO::Map(const size_t memory_device_offset, const size_t size,
                          const int mmap_prot, const int mmap_flags,
                          ScopedMmap* const scoped_mmap) const {
  DCHECK(scoped_mmap);
  DCHECK(IsInitialized());
  return scoped_mmap->Map(memory_device_file_descriptor(), memory_device_offset,
                          size, memory_page_size(), mmap_prot, mmap_flags);
}

void MemoryMappedIO::Reset() {
  memory_device_file_descriptor_ = -1;
  memory_page_size_ = 0;
}
