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

#include "gloop/util/process/nul_terminated_buf_appender.h"

#include <cstddef>
#include <cstring>

#include "gtest/gtest.h"

namespace {
using ::util_process_internal::NulTerminatedBufAppender;

TEST(NulTerminatedBufAppenderTest, SimpleString) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize);
  appender.Append("Hello!");
  EXPECT_STREQ(buf, "Hello!");
  EXPECT_FALSE(appender.IsFull());
  EXPECT_EQ(appender.SizeLeft(), kBufSize - strlen(buf));
}

TEST(NulTerminatedBufAppenderTest, Number) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize);
  appender.Append(12345);
  EXPECT_STREQ(buf, "12345");
}

TEST(NulTerminatedBufAppenderTest, NegativeNumber) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize);
  appender.Append(-12345);
  EXPECT_STREQ(buf, "-12345");
}

TEST(NulTerminatedBufAppenderTest, Zero) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize);
  appender.Append(0);
  EXPECT_STREQ(buf, "0");
}

TEST(NulTerminatedBufAppenderTest, LongString) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize - 6);
  appender.Append("This string will overflow!");
  EXPECT_EQ(strlen(buf), kBufSize - 7);
  EXPECT_STREQ(buf, "This stri");
  EXPECT_TRUE(appender.IsFull());
  EXPECT_EQ(appender.SizeLeft(), 1);
}

TEST(NulTerminatedBufAppenderTest, TwoStrings) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize);
  appender.Append("Hello");
  appender.Append(" again!");
  EXPECT_STREQ(buf, "Hello again!");
  EXPECT_FALSE(appender.IsFull());
  EXPECT_EQ(appender.SizeLeft(), kBufSize - strlen(buf));
}

TEST(NulTerminatedBufAppenderTest, NumberAndString) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, kBufSize);
  appender.Append(1);
  appender.Append("23");
  EXPECT_STREQ(buf, "123");
  EXPECT_FALSE(appender.IsFull());
  EXPECT_EQ(appender.SizeLeft(), kBufSize - strlen(buf));
}

TEST(NulTerminatedBufAppenderTest, LongNumber) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, 5);
  appender.Append(12345);
  EXPECT_STREQ(buf, "1234");
  EXPECT_TRUE(appender.IsFull());
}

TEST(NulTerminatedBufAppenderTest, LongNegativeNumber) {
  const size_t kBufSize = 16;
  char buf[kBufSize] = "Uninitialized";
  NulTerminatedBufAppender appender(buf, 2);
  appender.Append(-12345);
  EXPECT_STREQ(buf, "-");
  EXPECT_TRUE(appender.IsFull());
}

}  // namespace
