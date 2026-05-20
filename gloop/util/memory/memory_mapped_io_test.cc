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

#include "gloop/util/memory/memory_mapped_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/util/memory/memory_mapped_io_test_data.h"
#include "gloop/util/memory/scoped_mmap.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

namespace {

class MemoryMappedIOTest : public testing::Test {
 protected:
  MemoryMappedIOTest() {
    CHECK(kMinReadWriteSize_ <= kTestDataSize_);
    test_data_.Initialize(kRandomSeed_, kTestDataSize_);
    CHECK(test_data_file_.Write(GetMockMemoryDeviceName(), test_data_,
                                kMinAddress_));
  }

  ~MemoryMappedIOTest() { test_data_file_.Delete(); }

  virtual void SetUp() {
    // Called before *each* test.
  }

  virtual void TearDown() {
    // Called after *each* test.
  }

  // Test data written to test_data_file_.
  memory_mapped_io_test::TestData test_data_;
  // File of test data used to ensure memory mapped file operations work
  // correctly.
  memory_mapped_io_test::TestDataFile test_data_file_;

  // Name of the mock memory device used for testing
  static const char* GetMockMemoryDeviceName() {
    // Allocated once, never freed
    static std::string* mock_memory_device_name =
        new std::string(::testing::TempDir() + "/memory_mapped_io_test_file");
    return mock_memory_device_name->c_str();
  }

  // Size of the test data written to and read back from the memory device
  // kTestDataSize_ should be a multiple of kMinReadWriteSize_.
  static const size_t kTestDataSize_;
  // Seed of the random number generator used to generate test data and
  // random addresses for random read / write test.
  static const uint32_t kRandomSeed_;
  // Number of random read / writes to perform in the random read / write test.
  static const size_t kNumRandomReadWrites_;
  // Minimum address to read from / write to.
  static const size_t kMinAddress_;
  // Minimum amount of data to read / write.
  static const size_t kMinReadWriteSize_;
};

const size_t MemoryMappedIOTest::kTestDataSize_ = 0x10000;
const uint32_t MemoryMappedIOTest::kRandomSeed_ = 0x12345678;
const size_t MemoryMappedIOTest::kNumRandomReadWrites_ = 0x1000;
const size_t MemoryMappedIOTest::kMinAddress_ = 0x100;
const size_t MemoryMappedIOTest::kMinReadWriteSize_ = 1;

// Ensure the memory mapped I/O class initialization works correctly.
TEST_F(MemoryMappedIOTest, Initialization) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_FALSE(memory_mapped_io.IsInitialized());
  ASSERT_TRUE(memory_mapped_io.Initialize(GetMockMemoryDeviceName(),
                                          MemoryMappedIO::OPEN_MODE_READ_ONLY));
  ASSERT_TRUE(memory_mapped_io.IsInitialized());
  memory_mapped_io.Terminate();
  ASSERT_FALSE(memory_mapped_io.IsInitialized());
}

// Ensure the memory mapped I/O class can be initialized using a valid file
// descriptor.
TEST_F(MemoryMappedIOTest, InitializeUsingFileDescriptor) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_FALSE(memory_mapped_io.IsInitialized());
  FILE* const temporary_file = tmpfile();
  CHECK(temporary_file);
  memory_mapped_io.InitializeUsingFileDescriptor(fileno(temporary_file));
  ASSERT_TRUE(memory_mapped_io.IsInitialized());
  memory_mapped_io.Terminate();
  ASSERT_FALSE(memory_mapped_io.IsInitialized());
}

// Ensure mapping memory using the memory mapped I/O class works correctly.
TEST_F(MemoryMappedIOTest, RandomMap) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_TRUE(memory_mapped_io.Initialize(GetMockMemoryDeviceName(),
                                          MemoryMappedIO::OPEN_MODE_READ_ONLY));

  ACMRandom random_address_size(kRandomSeed_);
  for (uint32_t i = 0; i < kNumRandomReadWrites_; ++i) {
    size_t offset;
    size_t size;
    test_data_.RandomOffsetSize(random_address_size, kMinReadWriteSize_,
                                &offset, &size);
    ScopedMmap scoped_mmap;
    ASSERT_FALSE(scoped_mmap.IsMapped());
    const void* const mem = memory_mapped_io.Map(
        kMinAddress_ + offset, size, PROT_READ, MAP_SHARED, &scoped_mmap);
    CHECK(mem);
    ASSERT_TRUE(scoped_mmap.IsMapped());
    ASSERT_EQ(0, memcmp(mem, test_data_.random_data() + offset, size));
    scoped_mmap.Unmap();
    ASSERT_FALSE(scoped_mmap.IsMapped());
  }
}

// Ensure the memory mapped I/O class writes to memory without modifying the
// source buffer.
TEST_F(MemoryMappedIOTest, WriteBufferConstant) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_TRUE(memory_mapped_io.Initialize(
      GetMockMemoryDeviceName(), MemoryMappedIO::OPEN_MODE_READ_WRITE));
  // Write data to device and ensure written data isn't modified.
  ASSERT_TRUE(memory_mapped_io.Write(MemoryMappedIO::WRITE_MODE_SHARED,
                                     kMinAddress_, kTestDataSize_,
                                     test_data_.write_data()));
  ASSERT_EQ(0, memcmp(test_data_.write_data(), test_data_.random_data(),
                      kTestDataSize_));
}

// Perform a simple read / write test to make sure data written to memory is
// the same as that read back.
TEST_F(MemoryMappedIOTest, ReadWrite) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_TRUE(memory_mapped_io.Initialize(
      GetMockMemoryDeviceName(), MemoryMappedIO::OPEN_MODE_READ_WRITE));
  ASSERT_TRUE(memory_mapped_io.Write(MemoryMappedIO::WRITE_MODE_PRIVATE,
                                     kMinAddress_, kTestDataSize_,
                                     test_data_.write_data()));
  ASSERT_TRUE(memory_mapped_io.Read(kMinAddress_, kTestDataSize_,
                                    test_data_.read_data()));
  ASSERT_TRUE(memcmp(test_data_.read_data(), test_data_.write_data(),
                     kTestDataSize_) == 0);
}

// Read from random physical addresses ensuring the test data is read
// correctly.
TEST_F(MemoryMappedIOTest, RandomRead) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_TRUE(memory_mapped_io.Initialize(GetMockMemoryDeviceName(),
                                          MemoryMappedIO::OPEN_MODE_READ_ONLY));

  ACMRandom random_address_size(kRandomSeed_);
  for (uint32_t i = 0; i < kNumRandomReadWrites_; ++i) {
    size_t offset;
    size_t size;
    test_data_.RandomOffsetSize(random_address_size, kMinReadWriteSize_,
                                &offset, &size);
    const uint8_t* const random_data = test_data_.random_data() + offset;
    uint8_t* const read_data = test_data_.read_data() + offset;
    ASSERT_TRUE(memory_mapped_io.Read(kMinAddress_ + offset, size, read_data));
    ASSERT_EQ(0, memcmp(read_data, random_data, size));
  }
}

// Write to and read from random physical addresses ensuring the data that
// is written is read back correctly.
TEST_F(MemoryMappedIOTest, RandomReadWrite) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_TRUE(memory_mapped_io.Initialize(
      GetMockMemoryDeviceName(), MemoryMappedIO::OPEN_MODE_READ_WRITE));

  ACMRandom random_address_size(kRandomSeed_);
  for (uint32_t i = 0; i < kNumRandomReadWrites_; ++i) {
    size_t offset;
    size_t size;
    test_data_.RandomOffsetSize(random_address_size, kMinReadWriteSize_,
                                &offset, &size);
    uint8_t* const write_data = test_data_.write_data() + offset;
    uint8_t* const read_data = test_data_.read_data() + offset;
    ASSERT_TRUE(memory_mapped_io.Write(MemoryMappedIO::WRITE_MODE_SHARED,
                                       kMinAddress_ + offset, size,
                                       write_data));
    ASSERT_TRUE(memory_mapped_io.Read(kMinAddress_ + offset, size, read_data));
    ASSERT_EQ(0, memcmp(read_data, write_data, size));
  }
}

// Ensure memory mapped io initialization fails with an invalid open mode.
TEST_F(MemoryMappedIOTest, InvalidDevice) {
  MemoryMappedIO memory_mapped_io;
  ASSERT_FALSE(memory_mapped_io.Initialize(
      "something_that_does_not_exist", MemoryMappedIO::OPEN_MODE_READ_WRITE));
}

#ifndef ABSL_HAVE_ADDRESS_SANITIZER
// Ensure memory mapped io read or write fails with the mmap resource limit
// set to 0.  Fails sporadically with the wrong error code due to earlier-than
// expected sanitizer failure.
TEST_F(MemoryMappedIOTest, MapFail) {
  const pid_t process_id = fork();
  ASSERT_GE(process_id, 0);
  if (process_id == 0) {
    // In the child process limit the amount of memory that can be mapped.
    struct rlimit mmap_resource_limit = {0, 0};
    CHECK_EQ(0, setrlimit(RLIMIT_AS, &mmap_resource_limit));
    // Try to read / write via the memory mapped I/O object.
    MemoryMappedIO memory_mapped_io;
    ASSERT_TRUE(memory_mapped_io.Initialize(
        GetMockMemoryDeviceName(), MemoryMappedIO::OPEN_MODE_READ_WRITE));
    bool failed = !memory_mapped_io.Write(MemoryMappedIO::WRITE_MODE_PRIVATE,
                                          kMinAddress_, kTestDataSize_,
                                          test_data_.write_data());
    failed &= !memory_mapped_io.Read(kMinAddress_, kTestDataSize_,
                                     test_data_.read_data());
    // We need to quick_exit here. Doing a regular exit will run global
    // destructors that might try to allocate memory and fail, causing the
    // process to exit with a segfault instead of the exit code we just set.
    std::quick_exit(failed ? 1 : 0);
  } else {
    int child_process_return_value = -1;
    waitpid(process_id, &child_process_return_value, 0);
    ASSERT_EQ(1, WEXITSTATUS(child_process_return_value));
  }
}
#endif  // ABSL_HAVE_ADDRESS_SANITIZER

}  // namespace

int main(int argc, char* argv[]) {
  // NOTE: cl/8223485 hooks exit() using atexit() to flush the logs which
  // causes a hang on exit() in any child process forked from a test case.
  // Disabling threaded logging works around the issue.
  absl::SetFlag(&FLAGS_threaded_logging, false);
  InitGoogle("", &argc, &argv, true);
  return RUN_ALL_TESTS();
}
