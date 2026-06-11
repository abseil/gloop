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

#include "gloop/util/tuple/components/for_each.h"

#include <stddef.h>

#include <array>
#include <cstdint>
#include <functional>
#include <numeric>
#include <string>
#include <tuple>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

struct IndexValueAppender {
  explicit IndexValueAppender(::std::string* dst) : dst_(dst) {}
  template <::size_t N, class T>
  void operator()(const T& val) const {
    if (!dst_->empty()) dst_->push_back(' ');
    absl::StrAppend(dst_, N, " ", val);
  }

 private:
  ::std::string* dst_;
};

struct ValueAppender {
  explicit ValueAppender(::std::string* dst) : dst_(dst) {}
  template <class T>
  void operator()(const T& val) const {
    if (!dst_->empty()) dst_->push_back(' ');
    absl::StrAppend(dst_, val);
  }

 private:
  ::std::string* dst_;
};

struct SizeAppender {
  explicit SizeAppender(::std::string* dst) : dst_(dst) {}
  template <class T>
  void operator()() const {
    if (!dst_->empty()) dst_->push_back(' ');
    absl::StrAppend(dst_, sizeof(T));
  }

 private:
  ::std::string* dst_;
};

struct IndexSizeAppender {
  explicit IndexSizeAppender(::std::string* dst) : dst_(dst) {}
  template <::size_t N, class T>
  void operator()() const {
    if (!dst_->empty()) dst_->push_back(' ');
    absl::StrAppend(dst_, N, " ", sizeof(T));
  }

 private:
  ::std::string* dst_;
};

struct Inc {
  template <class T>
  void operator()(T& val) const {
    ++val;
  }
};

template <class T>
::std::string AppendIndexValue(const T& t) {
  ::std::string res;
  for_each_index(IndexValueAppender(&res), t);
  return res;
}

template <class T>
::std::string AppendValue(const T& t) {
  ::std::string res;
  for_each(ValueAppender(&res), t);
  return res;
}

template <class T>
::std::string AppendSize() {
  ::std::string res;
  for_each<T>(SizeAppender(&res));
  return res;
}

template <class T>
::std::string AppendIndexSize() {
  ::std::string res;
  for_each_index<T>(IndexSizeAppender(&res));
  return res;
}

struct MutableOp {
  template <class T>
  void operator()(const T& t) {}
};

TEST(ForEach, IndexValue) {
  EXPECT_EQ("", AppendIndexValue(::std::make_tuple()));
  EXPECT_EQ("0 42", AppendIndexValue(::std::make_tuple(42)));
  EXPECT_EQ("0 42 1 hello", AppendIndexValue(::std::make_tuple(42, "hello")));
}

TEST(ForEach, Value) {
  EXPECT_EQ("", AppendValue(::std::make_tuple()));
  EXPECT_EQ("42", AppendValue(::std::make_tuple(42)));
  EXPECT_EQ("42 hello", AppendValue(::std::make_tuple(42, "hello")));

  ::std::tuple<int, double> t(1, 2);
  for_each(Inc(), t);
  EXPECT_EQ(2, ::std::get<0>(t));
  EXPECT_EQ(3.0, ::std::get<1>(t));
}

TEST(ForEach, Size) {
  EXPECT_EQ("", (AppendSize<::std::tuple<>>()));
  EXPECT_EQ("4", (AppendSize<::std::tuple<int32_t>>()));
  EXPECT_EQ("4 8", (AppendSize<::std::tuple<int32_t, int64_t>>()));
}

TEST(ForEach, IndexSize) {
  EXPECT_EQ("", (AppendIndexSize<::std::tuple<>>()));
  EXPECT_EQ("0 4", (AppendIndexSize<::std::tuple<int32_t>>()));
  EXPECT_EQ("0 4 1 8", (AppendIndexSize<::std::tuple<int32_t, int64_t>>()));
}

TEST(ForEach, WithReferenceWrapper) {
  MutableOp op;
  for_each(::std::ref(op), ::std::make_tuple(0));
}

TEST(ForEach, OrderWithLargeTuple) {
  ::std::array<int, 42> src;
  ::std::iota(src.begin(), src.end(), 0);
  ::std::array<int, 42> dst;
  auto out = dst.begin();
  for_each([&](int x) { *out++ = x; }, src);
  EXPECT_THAT(dst, ::testing::ContainerEq(src));
}

TEST(ForEach, Constexpr) {
  constexpr int sum = []() {
    auto t = std::make_tuple(1, 2, 3);
    int s = 0;
    for_each([&](int val) { s += val; }, t);
    return s;
  }();
  static_assert(sum == 6);
}

}  // namespace
}  // namespace tuple
}  // namespace util
