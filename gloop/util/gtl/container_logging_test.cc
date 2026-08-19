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

#include "gloop/util/gtl/container_logging.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

#ifdef UTIL_GTL_STL_LOGGING_H_
#error Do not include stl_logging.h
#endif

namespace {

class ContainerLoggingTest : public ::testing::Test {
 protected:
  ContainerLoggingTest() : stream_(new std::stringstream) {}
  std::ostream& stream() { return *stream_; }
  std::string logged() {
    std::string r = stream_->str();
    stream_ = std::make_unique<std::stringstream>();
    return r;
  }

 private:
  std::unique_ptr<std::stringstream> stream_;
};

TEST_F(ContainerLoggingTest, ShortRange) {
  std::vector<std::string> words = {"hi", "hello"};
  gtl::LogRangeToStream(stream(), words.begin(), words.end(),
                        gtl::LogMultiline());
  EXPECT_EQ("[\nhi\nhello\n]", logged());
}

TEST_F(ContainerLoggingTest, LegacyRange) {
  std::vector<int> lengths = {1, 2};
  gtl::LogRangeToStream(stream(), lengths.begin(), lengths.end(),
                        gtl::LogLegacyUpTo100());
  EXPECT_EQ("1 2", logged());
}

TEST_F(ContainerLoggingTest, ToString) {
  std::vector<int> lengths = {1, 2, 3, 4, 5};
  EXPECT_EQ(gtl::LogContainer(lengths).str(), "[1, 2, 3, 4, 5]");
}

class UserDefFriend {
 public:
  explicit UserDefFriend(int i) : i_(i) {}

 private:
  friend std::ostream& operator<<(std::ostream& str, const UserDefFriend& i) {
    return str << i.i_;
  }
  int i_;
};

TEST_F(ContainerLoggingTest, RangeOfUserDefined) {
  std::vector<UserDefFriend> ints = {UserDefFriend(1), UserDefFriend(2),
                                     UserDefFriend(3)};
  gtl::LogRangeToStream(stream(), ints.begin(), ints.begin() + 1,
                        gtl::LogDefault());
  gtl::LogRangeToStream(stream(), ints.begin() + 1, ints.begin() + 2,
                        gtl::LogMultiline());
  gtl::LogRangeToStream(stream(), ints.begin() + 2, ints.begin() + 3,
                        gtl::LogDefault());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.begin(),
                        gtl::LogMultiline());

  EXPECT_EQ("[1][\n2\n][3][\n]", logged());
}

TEST_F(ContainerLoggingTest, FullContainer) {
  std::vector<int> ints;
  std::vector<int> ints100;
  std::vector<int> ints123;
  int64_t max_elements = 123;
  std::string expected1;
  std::string expected2;
  std::string expected3;
  std::string expected4;
  std::string expected5;
  std::string expected6;
  std::string expected7;
  std::string expected8;
  std::string expected9;
  for (int i = 0; i < 1000; ++i) {
    ints.push_back(i);
    if (i < 100) {
      ints100.push_back(i);
    }
    if (i < max_elements) {
      ints123.push_back(i);
    }
  }
  expected1 = "[\n" + absl::StrJoin(ints, "\n") + "\n]";
  expected2 = "[" + absl::StrJoin(ints, ", ") + "]";
  expected3 = "[\n" + absl::StrJoin(ints100, "\n") + "\n...\n]";
  expected4 = "[" + absl::StrJoin(ints100, ", ") + ", ...]";
  expected5 = absl::StrJoin(ints100, " ") + " ...";
  expected6 = "[\n" + absl::StrJoin(ints, "\n") + "\n]";
  expected7 = "[\n" + absl::StrJoin(ints123, "\n") + "\n...\n]";
  expected8 = "[" + absl::StrJoin(ints, ", ") + "]";
  expected9 = "[" + absl::StrJoin(ints123, ", ") + ", ...]";

  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogMultiline());
  EXPECT_EQ(expected1, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(), gtl::LogShort());
  EXPECT_EQ(expected2, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogMultilineUpTo100());
  EXPECT_EQ(expected3, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogShortUpTo100());
  EXPECT_EQ(expected4, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogLegacyUpTo100());
  EXPECT_EQ(expected5, logged());

  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogMultilineUpToN(ints.size()));
  EXPECT_EQ(expected6, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogMultilineUpToN(max_elements));
  EXPECT_EQ(expected7, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogShortUpToN(ints.size()));
  EXPECT_EQ(expected8, logged());
  gtl::LogRangeToStream(stream(), ints.begin(), ints.end(),
                        gtl::LogShortUpToN(max_elements));
  EXPECT_EQ(expected9, logged());
}

TEST_F(ContainerLoggingTest, LogContainer) {
  std::set<int> ints = {1, 2, 3};
  stream() << gtl::LogContainer(ints, gtl::LogMultiline());
  EXPECT_EQ("[\n1\n2\n3\n]", logged());

  stream() << gtl::LogContainer(ints);
  EXPECT_EQ("[1, 2, 3]", logged());

  stream() << gtl::LogContainer(std::vector<int>(ints.begin(), ints.end()),
                                gtl::LogLegacyUpTo100());
  EXPECT_EQ("1 2 3", logged());
}

TEST_F(ContainerLoggingTest, LogMutableSpan) {
  std::vector<int> ints = {1, 2, 3};
  absl::Span<int> int_span(ints);
  stream() << gtl::LogContainer(int_span);
  EXPECT_EQ("[1, 2, 3]", logged());
}

TEST_F(ContainerLoggingTest, LogRange) {
  std::set<int> ints = {1, 2, 3};
  stream() << gtl::LogRange(ints.begin(), ints.end(), gtl::LogMultiline());
  EXPECT_EQ("[\n1\n2\n3\n]", logged());

  stream() << gtl::LogRange(ints.begin(), ints.end());
  EXPECT_EQ("[1, 2, 3]", logged());
}

// Some class with a custom Stringify
class C {
 public:
  explicit C(int x) : x_(x) {}

 private:
  // This is intentionally made private for the purposes of the test;
  //` AbslStringify` isn't meant to be called directly, and instead invoked
  // via `StrCat` and friends.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const C& p) {
    absl::Format(&sink, "C(%d)", p.x_);
  }

  int x_;
};

TEST_F(ContainerLoggingTest, LogContainerWithCustomStringify) {
  std::vector<C> c = {C(1), C(2), C(3)};
  stream() << gtl::LogContainer(c);
  EXPECT_EQ("[C(1), C(2), C(3)]", logged());
}

class LogEnumTest : public ContainerLoggingTest {
 protected:
  enum Unscoped { kUnscoped0, kUnscoped1, kUnscoped2 };

  enum StreamableUnscoped {
    kStreamableUnscoped0,
    kStreamableUnscoped1,
    kStreamableUnscoped2
  };

  enum class Scoped { k0, k1, k2 };

  enum class StreamableScoped { k0, k1, k2 };

  friend std::ostream& operator<<(std::ostream& os, StreamableUnscoped v) {
    return os << gtl::LogEnum(v);
  }

  friend std::ostream& operator<<(std::ostream& os, StreamableScoped v) {
    return os << gtl::LogEnum(v);
  }
};

TEST_F(LogEnumTest, Unscoped) {
  stream() << gtl::LogEnum(kUnscoped0) << "," << gtl::LogEnum(kUnscoped1) << ","
           << gtl::LogEnum(kUnscoped2);
  EXPECT_EQ("0,1,2", logged());
}

TEST_F(LogEnumTest, StreamableUnscoped) {
  stream() << kStreamableUnscoped0 << "," << kStreamableUnscoped1 << ","
           << kStreamableUnscoped2;
  EXPECT_EQ("0,1,2", logged());
}

TEST_F(LogEnumTest, Scoped) {
  stream() << gtl::LogEnum(Scoped::k0) << "," << gtl::LogEnum(Scoped::k1) << ","
           << gtl::LogEnum(Scoped::k2);
  EXPECT_EQ("0,1,2", logged());
}

TEST_F(LogEnumTest, StreamableScoped) {
  // Test using LogEnum to implement an operator<<.
  stream() << StreamableScoped::k0 << "," << StreamableScoped::k1 << ","
           << StreamableScoped::k2;
  EXPECT_EQ("0,1,2", logged());
}

}  // namespace
