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

#include "gloop/util/tuple/slice.h"

#include <tuple>

#include "gloop/gloop_test.h"
#include "gloop/util/tuple/test_util.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;

class Slice : public TestValues {};

TEST_F(Slice, Empty) {
  EXPECT_EQ(make_tuple(), slice(make_tuple()));
  EXPECT_EQ(make_tuple(), slice(make_tuple(a, b)));
}

TEST_F(Slice, SliceAll) {
  EXPECT_EQ(make_tuple(a), (slice<0>(make_tuple(a))));
  EXPECT_EQ(make_tuple(a, b), (slice<0, 1>(make_tuple(a, b))));
}

TEST_F(Slice, SliceFirst) {
  EXPECT_EQ(make_tuple(a), slice<0>(make_tuple(a, b)));
}

TEST_F(Slice, SliceLast) {
  EXPECT_EQ(make_tuple(b), slice<1>(make_tuple(a, b)));
}

TEST_F(Slice, SliceMiddle) {
  EXPECT_EQ(make_tuple(b), slice<1>(make_tuple(a, b, c)));
}

TEST_F(Slice, SliceEven) {
  EXPECT_EQ(make_tuple(a, c), (slice<0, 2>(make_tuple(a, b, c))));
}

TEST_F(Slice, DuplicateIndex) {
  EXPECT_EQ(make_tuple(a, a), (slice<0, 0>(make_tuple(a))));
}

TEST_F(Slice, ReverseOrder) {
  EXPECT_EQ(make_tuple(b, a), (slice<1, 0>(make_tuple(a, b))));
}

TEST_F(Slice, NonConst) {
  ::std::tuple<A&, B&> t(a, b);
  ::std::tuple<A&> q = slice<0>(t);
  EXPECT_EQ(&a, &get<0>(q));
}

class SliceRange : public TestValues {};

TEST_F(SliceRange, Empty) {
  EXPECT_EQ(make_tuple(), (slice_range<0, 0>(make_tuple())));

  auto t = make_tuple(a, b);
  EXPECT_EQ(make_tuple(), (slice_range<0, 0>(t)));
  EXPECT_EQ(make_tuple(), (slice_range<1, 1>(t)));
  EXPECT_EQ(make_tuple(), (slice_range<2, 2>(t)));
}

TEST_F(SliceRange, SliceAll) {
  EXPECT_EQ(make_tuple(a), (slice_range<0, 1>(make_tuple(a))));
  EXPECT_EQ(make_tuple(a, b), (slice_range<0, 2>(make_tuple(a, b))));
}

TEST_F(SliceRange, SliceFirst) {
  EXPECT_EQ(make_tuple(a), (slice_range<0, 1>(make_tuple(a, b))));
}

TEST_F(SliceRange, SliceLast) {
  EXPECT_EQ(make_tuple(b), (slice_range<1, 2>(make_tuple(a, b))));
}

TEST_F(SliceRange, SliceMiddle) {
  EXPECT_EQ(make_tuple(b), (slice_range<1, 2>(make_tuple(a, b, c))));
}

TEST_F(SliceRange, LeaveFirst) {
  EXPECT_EQ(make_tuple(b, c), (slice_range<1, 3>(make_tuple(a, b, c))));
}

TEST_F(SliceRange, LeaveLast) {
  EXPECT_EQ(make_tuple(a, b), (slice_range<0, 2>(make_tuple(a, b, c))));
}

TEST_F(SliceRange, NonConst) {
  ::std::tuple<A&, B&> t(a, b);
  ::std::tuple<A&> q = slice_range<0, 1>(t);
  EXPECT_EQ(&a, &get<0>(q));
}

}  // namespace
}  // namespace tuple
}  // namespace util
