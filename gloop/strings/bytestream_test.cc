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

#include "gloop/strings/bytestream.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/internal/resize_uninitialized.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace {

// We use this class instead of ArrayByteSource to simulate a
// strings::ByteSource that contains multiple fragments.  ArrayByteSource
// returns the entire array in one fragment.
class MockByteSource : public strings::ByteSource {
 public:
  MockByteSource(absl::string_view data, int block_size)
      : data_(data), block_size_(block_size) {}

  size_t Available() const override { return data_.size(); }
  absl::string_view Peek() override { return data_.substr(0, block_size_); }
  void Skip(size_t n) override { data_.remove_prefix(n); }

 private:
  absl::string_view data_;
  int block_size_;
};

TEST(ByteSourceTest, CopyTo) {
  absl::string_view data("Hello world!");
  MockByteSource source(data, 3);
  std::string str;
  strings::StringByteSink sink(&str);

  source.CopyTo(&sink, data.size());
  EXPECT_EQ(data, str);
}

TEST(ByteSourceTest, CopySubstringTo) {
  absl::string_view data("Hello world!");
  MockByteSource source(data, 3);
  source.Skip(1);
  std::string str;
  strings::StringByteSink sink(&str);

  source.CopyTo(&sink, data.size() - 2);
  EXPECT_EQ(data.substr(1, data.size() - 2), str);
  EXPECT_EQ("!", source.Peek());
}

TEST(ByteSourceTest, LimitByteSource) {
  absl::string_view data("Hello world!");
  MockByteSource source(data, 3);
  strings::LimitByteSource limit_source(&source, 6);
  EXPECT_EQ(6, limit_source.Available());
  limit_source.Skip(1);
  EXPECT_EQ(5, limit_source.Available());

  {
    std::string str;
    strings::StringByteSink sink(&str);
    limit_source.CopyTo(&sink, limit_source.Available());
    EXPECT_EQ("ello ", str);
    EXPECT_EQ(0, limit_source.Available());
    EXPECT_EQ(6, source.Available());
  }

  {
    std::string str;
    strings::StringByteSink sink(&str);
    source.CopyTo(&sink, source.Available());
    EXPECT_EQ("world!", str);
    EXPECT_EQ(0, source.Available());
  }
}

TEST(ByteSinkTest, AppendExternalMemory) {
  absl::string_view data("Hello world!");
  bool released = false;
  {
    std::string str;
    strings::StringByteSink sink(&str);
    sink.AppendExternalMemory(data, &released, [](void* arg) {
      bool* rel = static_cast<bool*>(arg);
      ASSERT_TRUE(!*rel);
      *rel = true;
    });
    EXPECT_EQ(data, str);
  }
  EXPECT_TRUE(released);
}

TEST(ByteSinkTest, GetAppendBuffer) {
  char scratch[8];
  std::string str;
  strings::StringByteSink sink(&str);
  sink.Append("a", 1);
  EXPECT_EQ(1, str.length());

  size_t capacity = 0;
  char* p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  EXPECT_GE(capacity, 3);
  snprintf(p, capacity, "bc");
  sink.Append(p, 2);  // Do not include the NUL.
  EXPECT_EQ(3, str.length());
  EXPECT_STREQ("abc", str.c_str());
}

TEST(StringByteSinkTest, GetAppendBuffer) {
  char scratch[8];
  std::string str;
  strings::StringByteSink sink(&str);
  sink.Append("a", 1);
  EXPECT_EQ(1, str.length());

  size_t capacity = 0;
  char* p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  if (absl::strings_internal::STLStringSupportsNontrashingResize(&str)) {
    EXPECT_GE(capacity, 99);
    EXPECT_EQ(&str[0] + 1, p);
  }
  EXPECT_GE(capacity, 3);
  snprintf(p, capacity, "bc");
  sink.Append(p, 2);
  EXPECT_EQ(3, str.length());
  EXPECT_STREQ("abc", str.c_str());

  p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  if (absl::strings_internal::STLStringSupportsNontrashingResize(&str)) {
    EXPECT_GE(capacity, 99);
    EXPECT_EQ(&str[0] + 3, p);
  }
  EXPECT_GE(capacity, 3);
  snprintf(p, capacity, "de");
  sink.Append(p, 2);
  EXPECT_EQ(5, str.length());
  EXPECT_STREQ("abcde", str.c_str());

  p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  if (absl::strings_internal::STLStringSupportsNontrashingResize(&str)) {
    EXPECT_GE(capacity, 99);
    EXPECT_EQ(&str[0] + 5, p);
  }
  EXPECT_GE(capacity, 3);
  p[0] = 'f';
  sink.Append(p, 1);
  EXPECT_EQ(6, str.length());
  EXPECT_STREQ("abcdef", str.c_str());

  std::string str2("abc");
  strings::StringByteSink sink2(&str2);
  sink2.Append("d", 1);
  capacity = 0;
  p = sink2.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  if (absl::strings_internal::STLStringSupportsNontrashingResize(&str2)) {
    EXPECT_GE(capacity, 99);
    EXPECT_EQ(&str2[0] + 4, p);
  }
  EXPECT_GE(capacity, 3);
  snprintf(p, capacity, "ef");
  sink2.Append(p, 2);
  EXPECT_EQ(6, str2.length());
  EXPECT_STREQ("abcdef", str2.c_str());
}

TEST(StringByteSinkTest, GetAppendBufferGrowthRate) {
  char scratch[8];
  std::string str;
  strings::StringByteSink sink(&str);

  if (absl::strings_internal::STLStringSupportsNontrashingResize(&str)) {
    const auto push = [&](size_t n) {
      size_t s;
      char* p = sink.GetAppendBuffer(1, n, scratch, sizeof(scratch), &s);
      std::fill(p, p + s, 'x');
      sink.Append(p, s);
      return s;
    };

    push(1024);
    push(1024);
    const size_t push2 = push(1024);
    const size_t push3 = push(1024);
    const size_t push4 = push(1024);

    // With 1.5x growth optimization, early steps might be linear due to
    // step size being larger than capacity/2.
    // We test later steps to verify exponential growth.
    EXPECT_GT(1. * (push4 - push3) / (push3 - push2), 1.4);
  }
}

TEST(ByteSourceTest, CopyToStringByteSink) {
  absl::string_view data("Hello world!");
  MockByteSource source(data, 3);
  std::string str;
  strings::StringByteSink sink(&str);
  source.CopyTo(&sink, data.size());
  EXPECT_EQ(data, str);
}

TEST(UncheckedArrayByteSinkTest, GetAppendBuffer) {
  char fixed_array[4];
  char scratch[8];
  strings::UncheckedArrayByteSink sink(fixed_array);
  sink.Append("a", 1);
  size_t capacity = 0;
  char* p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  EXPECT_EQ(99, capacity);  // This sink gives us whatever we ask for.
  EXPECT_EQ(fixed_array + 1, p);
  snprintf(p, capacity, "bc");
  sink.Append(p, 3);  // Include the NUL.
  EXPECT_EQ(fixed_array + 4, sink.CurrentDestination());
  EXPECT_STREQ("abc", fixed_array);
}

TEST(CheckedArrayByteSinkTest, GetAppendBuffer) {
  char fixed_array[4];
  char scratch[8];
  strings::CheckedArrayByteSink sink(fixed_array, sizeof(fixed_array));
  sink.Append("a", 1);
  size_t capacity = 0;
  char* p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  EXPECT_EQ(3, capacity);  // This sink gives us its remaining buffer.
  EXPECT_EQ(fixed_array + 1, p);
  snprintf(p, capacity, "bc");
  sink.Append(p, 3);  // Include the NUL.
  EXPECT_EQ(4, sink.NumberOfBytesWritten());
  EXPECT_STREQ("abc", fixed_array);
  EXPECT_FALSE(sink.Overflowed());
  p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  EXPECT_EQ(sizeof(scratch), capacity);  // The array is full.
  EXPECT_EQ(scratch, p);
  snprintf(p, capacity, "de");
  sink.Append(p, 2);                          // Anything >0.
  EXPECT_EQ(4, sink.NumberOfBytesWritten());  // No change except Overflowed().
  EXPECT_STREQ("abc", fixed_array);
  EXPECT_TRUE(sink.Overflowed());
}

TEST(GrowingArrayByteSinkTest, GetAppendBuffer) {
  char scratch[40];
  strings::GrowingArrayByteSink sink(4);
  sink.Append("a", 1);
  size_t capacity = 0;
  char* p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  EXPECT_GE(capacity, 3);  // This sink allocated 4 or more bytes.
  EXPECT_NE(scratch, p);
  memcpy(p, "bcd", 3);  // Do not write the NUL.
  sink.Append(p, 3);
  p = sink.GetAppendBuffer(20, 30, scratch, sizeof(scratch), &capacity);
  EXPECT_GE(capacity, 30);  // Should have triggered expansion (reallocation).
  EXPECT_NE(scratch, p);
  snprintf(p, capacity, "efghijklmnopqrstuvw");
  sink.Append(p, 20);  // Include the NUL.
  size_t length;
  std::unique_ptr<char[]> owned(sink.GetBuffer(&length));
  EXPECT_EQ(24, length);
  EXPECT_STREQ("abcdefghijklmnopqrstuvw", owned.get());

  owned = sink.GetBuffer(&length);
  EXPECT_EQ(0, length);
  EXPECT_TRUE(owned == nullptr);
}

// Trivial test for a trivial class
TEST(ByteSinkTest, NullByteSink) {
  // We don't really have much to check in this test, since the class
  // by definition doesn't save anything.  This is really just to make
  // sure we don't crash the underlying implementation.
  strings::NullByteSink sink;
  sink.Append("abcd", 4);
  sink.Flush();

  absl::string_view data("Hello world!");
  sink.AppendExternalMemory(data, nullptr, [](void*) {});

  char scratch[8];
  size_t capacity = 0;
  char* p = sink.GetAppendBuffer(3, 99, scratch, sizeof(scratch), &capacity);
  EXPECT_EQ(scratch, p);
  size_t len = snprintf(p, capacity, "bc");
  sink.Append(p, len);
}

}  // namespace
