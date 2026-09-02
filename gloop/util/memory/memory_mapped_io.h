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

#ifndef THIRD_PARTY_GLOOP_UTIL_MEMORY_MEMORY_MAPPED_IO_H_
#define THIRD_PARTY_GLOOP_UTIL_MEMORY_MEMORY_MAPPED_IO_H_

#include <stddef.h>

#include "absl/strings/string_view.h"

class ScopedMmap;

// Memory Mapped Input/Output, reads and writes a memory device by mapping
// the device into the process' address space.
// <link>
//
// Recommended pattern of usage:
//     MemoryMappedIO mmio;
//     uint8 data[32 << 10];
//     // Open the memory mapped device.
//     mmio.Initialize(MemoryMappedIO::kPhysicalMemoryDevice,
//                     MemoryMappedIO::OPEN_MODE_READ_WRITE);
//     // Read 32kb from above the 1MB physical address.
//     mmio.Read(0x00100000, sizeof(data), data);
//     /* modify the memory (in a real test this would probably be very bad) */
//     for (int i = 0; i < sizeof(data); i += 2) {
//         swap(data[i], data[i + 1]);
//     }
//     // Write 32kb back to physical address 1MB.
//     mmio.Write(MemoryMappedIO::WRITE_MODE_SHARED,
//                0x00100000, sizeof(data), data);
//     // Close the device.
//     mmio.Terminate();
//
class MemoryMappedIO {
 public:
  enum OpenMode {
    // Data can only be read from the memory device.
    OPEN_MODE_READ_ONLY,
    // Data can be read from and written to the memory device.
    OPEN_MODE_READ_WRITE,
  };

  enum WriteMode {
    // Data written to the memory device is shared with other processes.
    WRITE_MODE_SHARED,
    // Changes to the memory device are only visible to the calling process.
    // When mapping another area of main RAM (e.g /dev/mem) this flag
    // results in the mapped area being copied to the process' address space.
    WRITE_MODE_PRIVATE,
  };

  // The constructor initializes the member variables of this object but
  // doesn't allocate resource required for reading / writing a memory device.
  // It's necessary to allocate resources prior to reading / writing a memory
  // device, using Initialize().
  MemoryMappedIO();

  // This type is neither copyable nor movable.
  MemoryMappedIO(const MemoryMappedIO&) = delete;
  MemoryMappedIO& operator=(const MemoryMappedIO&) = delete;

  // Deallocates resources owned by this object if the object is initialized.
  virtual ~MemoryMappedIO();

  // Allocate resources required for this object to access the
  // memory_device with the specified open_mode.
  // To access physical memory specify kMemoryDevice as the memory_device
  // argument.
  virtual bool Initialize(absl::string_view memory_device,
                          const OpenMode open_mode);

  // Initialize access to the memory device referenced by file_descriptor.
  // This function will CHECK if file_descriptor is lesser than zero.
  virtual void InitializeUsingFileDescriptor(const int file_descriptor);

  // Determines whether resources have been allocated for this object.
  virtual bool IsInitialized() const;

  // Free resources associated with this object.
  virtual void Terminate();

  // Reads size bytes from the memory_device_offset of the memory device
  // to the specified destination_address.  physical_address should be
  // smaller than the size of the system's memory, size must be greater
  // than 0, destination_address must be a valid address in the calling
  // process' address space.
  // This function returns false if it's not possible to map the address
  // range specified, for example the address range may be locked.
  // If the address range is mapped this function returns true.
  //
  // NOTE : This function is only useful if the memory device
  // associated with this object doesn't support seek (e.g /dev/mem).
  // If the device supports seek the application should just use
  // lseek() then read() instead as this results in 1 system call rather
  // than the 2 (map, unmap) when using this function
  virtual bool Read(const size_t memory_device_offset, const size_t size,
                    void* const destination_address) const;

  // Writes size bytes to the memory_device_offset of the memory device
  // from the source_address with the specified write_mode.
  // size must be greater than 0.
  // source_address must be a valid address in the calling process'
  // address space.
  // returns false if it's not possible to map the address range specified
  // - for example the address range may be locked, true if successful.
  // physical_address argument from the specified source address
  //
  // NOTE : This function is only useful if the memory device
  // associated with this object doesn't support seek (e.g /dev/mem)
  // If the device supports seek the application should just use
  // lseek() then write() instead as this results in 1 system call rather
  // than the 2 (map, unmap) when using this function.
  virtual bool Write(const WriteMode write_mode,
                     const size_t memory_device_offset, const size_t size,
                     const void* const source_address) const;

  // Maps the data from region specified by memory_device_offset and size
  // from the device referenced by this object using the scoped_mmap.
  // Returns pointer to mapped area in the process' address space if
  // successful, NULL otherwise.
  // mmap_prot specifies the memory protection mode (prot) passed to mmap().
  // mmap_flags specifies the type of mapped object flags (flags) passed
  // to mmap().
  // See mmap() documentation (info mmap) for more information.
  virtual void* Map(const size_t memory_device_offset, const size_t size,
                    const int mmap_prot, const int mmap_flags,
                    ScopedMmap* const scoped_mmap) const;

  virtual int memory_page_size() const { return memory_page_size_; }
  virtual int memory_device_file_descriptor() const {
    return memory_device_file_descriptor_;
  }

  // Name of the physical memory device file.
  static inline constexpr absl::string_view kPhysicalMemoryDevice = "/dev/mem";

 protected:
  // Resets the object's member variables to their default state.
  virtual void Reset();

 private:
  // File descriptor which references the memory device.
  // See open() for more information.
  int memory_device_file_descriptor_;
  // Used to cache the page size of the system memory to avoid a system call
  // each time the page size is required.
  int memory_page_size_;
};

#endif  // THIRD_PARTY_GLOOP_UTIL_MEMORY_MEMORY_MAPPED_IO_H_
