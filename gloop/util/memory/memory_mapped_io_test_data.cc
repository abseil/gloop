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

#include "gloop/util/memory/memory_mapped_io_test_data.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/util/random/acmrandom.h"

namespace memory_mapped_io_test {

TestData::TestData() { Reset(); }

TestData::~TestData() {
  if (IsInitialized()) Terminate();
}

void TestData::Initialize(const uint32_t random_seed, const size_t size_bytes) {
  CHECK(!IsInitialized());
  const size_t array_size = size_bytes / sizeof(*random_data_);
  random_data_ = new uint32_t[array_size * 3];
  CHECK(random_data_);

  ACMRandom random(random_seed);
  // generate a pseudo random array of data
  for (uint32_t i = 0; i < array_size; i++) {
    random_data_[i] = random.Next();
  }
  // fix up pointers for write and read arrays
  write_data_ = random_data_ + array_size;
  read_data_ = write_data_ + array_size;
  data_size_bytes_ = size_bytes;
  memcpy(write_data_, random_data_, size_bytes);
  memset(read_data_, 0, size_bytes);
}

bool TestData::IsInitialized() const {
  return random_data_ && write_data_ && read_data_;
}

void TestData::Terminate() {
  CHECK(IsInitialized());
  delete[] random_data_;
  Reset();
}

void TestData::RandomOffsetSize(ACMRandom& random, const size_t min_size,
                                size_t* const offset, size_t* const size) {
  const size_t data_size = data_size_bytes();
  CHECK(offset);
  CHECK(size);
  CHECK(min_size < data_size);
  const size_t data_range = data_size - min_size;
  *offset = static_cast<size_t>(random.Next64()) % data_range;
  const size_t remaining = data_size - *offset;
  *size = static_cast<size_t>(random.Next64()) % remaining;
  *size = std::max<size_t>(*size, min_size);
}

void TestData::Reset() {
  random_data_ = nullptr;
  write_data_ = nullptr;
  read_data_ = nullptr;
}

TestDataFile::TestDataFile() { filename_ = nullptr; }

TestDataFile::~TestDataFile() {
  if (HasFile()) {
    Delete();
  }
}

bool TestDataFile::Write(const char* const test_filename,
                         const TestData& test_data,
                         const size_t padding_bytes) {
  CHECK(!HasFile());
  const int fd =
      open(test_filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    LOG(WARNING) << "Unable to open " << test_filename;
    return false;
  }
  CHECK_EQ(padding_bytes, lseek(fd, padding_bytes, SEEK_SET));
  const size_t data_size = test_data.data_size_bytes();
  const bool succeeded =
      data_size == write(fd, test_data.random_data(), data_size);
  CHECK_EQ(0, close(fd));
  if (succeeded) {
    filename_ = test_filename;
    return true;
  }
  LOG(WARNING) << "Unable to write " << test_filename;
  return false;
}

bool TestDataFile::HasFile() const { return filename() != nullptr; }
void TestDataFile::Delete() {
  CHECK(HasFile());
  CHECK_EQ(0, remove(filename_));
  filename_ = nullptr;
}

}  // namespace memory_mapped_io_test
