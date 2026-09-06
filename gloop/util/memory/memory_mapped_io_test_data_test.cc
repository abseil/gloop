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

#include "gloop/util/memory/memory_mapped_io_test_data.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

namespace {

// Tests the test data for the MemoryMappedIO and ScopedMmap classes.
class MemoryMappedIOTestDataTest : public testing::Test {
 protected:
  MemoryMappedIOTestDataTest() {}

  ~MemoryMappedIOTestDataTest() {}

  virtual void SetUp() {
    // Called before *each* test.
  }

  virtual void TearDown() {
    // Called after *each* test.
    unlink(GetTestDataFileName());
  }

  // Name of the test data file.
  static const char* GetTestDataFileName() {
    // Allocated once, never freed
    static std::string* test_data_file_name = new std::string(
        ::testing::TempDir() + "/memory_mapped_io_unittest_data_test_data");
    return test_data_file_name->c_str();
  }

  // Size of the data written to the test file.
  static const size_t kTestDataSize_;
  // Used to seed the random number generator.
  static const uint32_t kRandomSeed_;
  // Number of times to test RandomOffsetSize().
  static const size_t kOffsetSizeIterations_;
  // Number of zero bytes to write to the start of the file prior to the
  // test data.
  static const size_t kTestDataPadding_;
};

const size_t MemoryMappedIOTestDataTest::kTestDataSize_ = 0x00100000;
const uint32_t MemoryMappedIOTestDataTest::kRandomSeed_ = 0x1234567;
const size_t MemoryMappedIOTestDataTest::kOffsetSizeIterations_ = 0x00001000;
const size_t MemoryMappedIOTestDataTest::kTestDataPadding_ = 0x00001000;

// Ensure test data initializes correctly.
TEST_F(MemoryMappedIOTestDataTest, Initialize) {
  memory_mapped_io_test::TestData test_data;
  ASSERT_FALSE(test_data.IsInitialized());
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  ASSERT_TRUE(test_data.IsInitialized());
  test_data.Terminate();
  ASSERT_FALSE(test_data.IsInitialized());
}

// Ensure test data size is stored correctly.
TEST_F(MemoryMappedIOTestDataTest, DataSize) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  ASSERT_EQ(kTestDataSize_, test_data.data_size_bytes());
}

// Ensure test data TestData::random_data() and TestData::write_data() are
// the same.
TEST_F(MemoryMappedIOTestDataTest, RandomWriteSame) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  ASSERT_EQ(0, memcmp(test_data.random_data(), test_data.write_data(),
                      kTestDataSize_));
}

// Ensure TestData::read_data() contains zeros.
TEST_F(MemoryMappedIOTestDataTest, ReadDataZero) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  uint8_t* const read_data = test_data.read_data();
  for (size_t i = 0; i < kTestDataSize_; ++i) {
    ASSERT_EQ(0, read_data[i]);
  }
}

// Ensure random offset size returns valid offsets and size.
TEST_F(MemoryMappedIOTestDataTest, RandomOffsetSize) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  ACMRandom random(kRandomSeed_);
  size_t min_size = 1;
  for (size_t i = 0; i < kOffsetSizeIterations_; ++i) {
    size_t offset;
    size_t size;
    test_data.RandomOffsetSize(random, min_size, &offset, &size);
    ASSERT_LE(offset, kTestDataSize_);
    ASSERT_GE(size, min_size);
    ASSERT_LE(offset + size, kTestDataSize_);
    min_size = (min_size + 1) % kTestDataSize_;
  }
}

// ensure test data file class initialization functions correctly
TEST_F(MemoryMappedIOTestDataTest, FileInitialization) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  memory_mapped_io_test::TestDataFile test_data_file;
  ASSERT_FALSE(test_data_file.HasFile());
  ASSERT_TRUE(test_data_file.Write(GetTestDataFileName(), test_data,
                                   kTestDataPadding_));
  ASSERT_TRUE(test_data_file.HasFile());
  test_data_file.Delete();
  ASSERT_FALSE(test_data_file.HasFile());
}

// Ensure test data file class writes data correctly.
TEST_F(MemoryMappedIOTestDataTest, FileRead) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  memory_mapped_io_test::TestDataFile test_data_file;
  ASSERT_TRUE(test_data_file.Write(GetTestDataFileName(), test_data,
                                   kTestDataPadding_));
  const int fd = open(GetTestDataFileName(), O_RDONLY);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(kTestDataPadding_, lseek(fd, kTestDataPadding_, SEEK_SET));
  ASSERT_EQ(kTestDataSize_, read(fd, test_data.read_data(), kTestDataSize_));
  ASSERT_EQ(0, memcmp(test_data.read_data(), test_data.random_data(),
                      kTestDataSize_));
  close(fd);
}

// Ensure test data file class deletes file on destruction.
TEST_F(MemoryMappedIOTestDataTest, FileDelete) {
  memory_mapped_io_test::TestData test_data;
  test_data.Initialize(kRandomSeed_, kTestDataSize_);
  {
    memory_mapped_io_test::TestDataFile test_data_file;
    ASSERT_TRUE(test_data_file.Write(GetTestDataFileName(), test_data,
                                     kTestDataPadding_));
  }
  struct stat stat_buf;
  ASSERT_EQ(-1, stat(GetTestDataFileName(), &stat_buf));
}

// Ensure correct behaviour when file write fails but file open for write
// succeeds.
TEST_F(MemoryMappedIOTestDataTest, WriteFailOpenSucceed) {
  auto run_in_child = [] {
    // In the child process limit the file size. Note: logging will not work
    // after this point (all writes will fail with EFBIG.
    struct rlimit file_size_resource_limit = {0, 0};
    CHECK_EQ(0, setrlimit(RLIMIT_FSIZE, &file_size_resource_limit));
    // Ignore SIGXFSZ. Otherwise the process will be terminated when
    // exceeding the file size resource limit.
    CHECK_NE(SIG_ERR, signal(SIGXFSZ, SIG_IGN));
    // Try to write the test data file, should fail.
    memory_mapped_io_test::TestData test_data;
    test_data.Initialize(kRandomSeed_, kTestDataSize_);
    memory_mapped_io_test::TestDataFile test_data_file;
    const bool succeeded = test_data_file.Write(GetTestDataFileName(),
                                                test_data, kTestDataPadding_);
    // Note: calling _exit to avoid any at-exit processing (e.g. leak checker).
    // The code at exit may want to write files and CHECK-fail, and we don't
    // care about any of that.
    _exit(succeeded ? 0 : 1);
  };
  ASSERT_EXIT(run_in_child(), ::testing::ExitedWithCode(1), "");
}

}  // namespace
