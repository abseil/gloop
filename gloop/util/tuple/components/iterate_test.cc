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

#include "gloop/util/tuple/components/iterate.h"

#include <stddef.h>

#include <string>
#include <tuple>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gloop/util/tuple/test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::make_tuple;
using ::std::tuple;

struct AppendReturnIndex {
  template <::size_t I>
  ::std::string operator()(::std::string state) const {
    absl::StrAppend(&state, I);
    return state;
  }
};

class IterateIndex : public TestValues {};

TEST_F(IterateIndex, ReturnByValue) {
  EXPECT_EQ("N", iterate_index<0>(AppendReturnIndex(), "N"));
  EXPECT_EQ("N0", iterate_index<1>(AppendReturnIndex(), "N"));
  EXPECT_EQ("N01", iterate_index<2>(AppendReturnIndex(), "N"));
  EXPECT_EQ("N012345678", iterate_index<9>(AppendReturnIndex(), "N"));
  EXPECT_EQ("N0123456789", iterate_index<10>(AppendReturnIndex(), "N"));
  EXPECT_EQ("N012345678910", iterate_index<11>(AppendReturnIndex(), "N"));
}

struct IdentityIndex {
  template <::size_t I, class T>
  const T& operator()(const T& state) const {
    return state;
  }
};

TEST_F(IterateIndex, ReturnByReference) {
  int n = 0;
  EXPECT_EQ(&n, &iterate_index<0>(IdentityIndex(), n));
  EXPECT_EQ(&n, &iterate_index<1>(IdentityIndex(), n));
  EXPECT_EQ(&n, &iterate_index<2>(IdentityIndex(), n));
  EXPECT_EQ(&n, &iterate_index<9>(IdentityIndex(), n));
  EXPECT_EQ(&n, &iterate_index<10>(IdentityIndex(), n));
  EXPECT_EQ(&n, &iterate_index<11>(IdentityIndex(), n));
}

struct PackIndex {
  template <::size_t N, class T>
  ::std::tuple<::size_t, T> operator()(const T& t) const {
    return make_tuple(N, t);
  }
};

TEST_F(IterateIndex, CallTree) {
  EXPECT_EQ(a, iterate_index<0>(PackIndex(), a));
  EXPECT_EQ(make_tuple(0, a), iterate_index<1>(PackIndex(), a));
  EXPECT_EQ(make_tuple(1, make_tuple(0, a)), iterate_index<2>(PackIndex(), a));
}

struct CopyTracker {
  CopyTracker(int) {}
  CopyTracker(const CopyTracker& other) {
    ADD_FAILURE() << "CopyTracker has been copied (unexpected)";
  }
};

struct PassCopyTrackerIndex {
  template <::size_t N>
  CopyTracker operator()(CopyTracker state) const {
    return 0;
  }
};

TEST_F(IterateIndex, NoCopies) {
  // iterate_index<9>() incurs no copies but iterate_index<10> incurs one copy.
  // Total number of copies performed by this call is floor((N - 1) / 9).
  iterate_index<9>(PassCopyTrackerIndex(), 0);
}

::std::string AppendReturn(::std::string state) {
  absl::StrAppend(&state, 0);
  return state;
}

TEST(Iterate, ReturnByValue) {
  EXPECT_EQ("N", iterate<0>(AppendReturn, std::string("N")));
  EXPECT_EQ("N0", iterate<1>(AppendReturn, "N"));
  EXPECT_EQ("N00", iterate<2>(AppendReturn, "N"));
  EXPECT_EQ("N000000000", iterate<9>(AppendReturn, "N"));
  EXPECT_EQ("N0000000000", iterate<10>(AppendReturn, "N"));
  EXPECT_EQ("N00000000000", iterate<11>(AppendReturn, "N"));
}

struct Identity {
  template <class T>
  const T& operator()(const T& state) const {
    return state;
  }
};

TEST(Iterate, ReturnByReference) {
  int n = 0;
  EXPECT_EQ(&n, &iterate<0>(Identity(), n));
  EXPECT_EQ(&n, &iterate<1>(Identity(), n));
  EXPECT_EQ(&n, &iterate<2>(Identity(), n));
  EXPECT_EQ(&n, &iterate<9>(Identity(), n));
  EXPECT_EQ(&n, &iterate<10>(Identity(), n));
  EXPECT_EQ(&n, &iterate<11>(Identity(), n));
}

struct PassCopyTracker {
  CopyTracker operator()(CopyTracker state) const { return 0; }
};

TEST(Iterate, NoCopies) { iterate<9>(PassCopyTracker(), 0); }

struct AppendIndex {
  template <::size_t I>
  void operator()() const {
    v->push_back(I);
  }
  std::vector<int>* v;
};

template <int N>
std::vector<int> DoAppendIndex(std::vector<int> state) {
  state.reserve(N);
  iterate_index<N>(AppendIndex{&state});
  return state;
}

// Make a vector containing -1 and all the numbers from 0 to max - 1 included,
// i.e. the expected result of DoAppendIndex when passing a state containing
// -1.
std::vector<int> MakeVectorWithMinusOne(int max) {
  std::vector<int> v;
  v.reserve(max + 1);
  for (int i = -1; i < max; ++i) v.push_back(i);
  return v;
}

TEST(IterateVoidIndex, Functional) {
  EXPECT_THAT(DoAppendIndex<0>({-1}), MakeVectorWithMinusOne(0));
  EXPECT_THAT(DoAppendIndex<1>({-1}), MakeVectorWithMinusOne(1));
  EXPECT_THAT(DoAppendIndex<2>({-1}), MakeVectorWithMinusOne(2));
  EXPECT_THAT(DoAppendIndex<9>({-1}), MakeVectorWithMinusOne(9));
  EXPECT_THAT(DoAppendIndex<10>({-1}), MakeVectorWithMinusOne(10));
  EXPECT_THAT(DoAppendIndex<11>({-1}), MakeVectorWithMinusOne(11));
  EXPECT_THAT(DoAppendIndex<5000>({-1}), MakeVectorWithMinusOne(5000));
  EXPECT_THAT(DoAppendIndex<5001>({-1}), MakeVectorWithMinusOne(5001));
}

struct Increment {
  void operator()() const { ++*n; }
  int* n;
};

template <::size_t N>
int DoIncrement() {
  int n = 0;
  iterate<N>(Increment{&n});
  return n;
}

TEST(IterateVoid, Functional) {
  EXPECT_EQ(0, DoIncrement<0>());
  EXPECT_EQ(1, DoIncrement<1>());
  EXPECT_EQ(2, DoIncrement<2>());
  EXPECT_EQ(9, DoIncrement<9>());
  EXPECT_EQ(10, DoIncrement<10>());
  EXPECT_EQ(11, DoIncrement<11>());
}

struct ConstexprIterateIndexF {
  template <size_t I>
  constexpr int operator()(int state) const {
    return state + I;
  }
};

TEST(Iterate, Constexpr) {
  constexpr auto r_2 = iterate<2>([](auto n) { return n + 1; }, 0);
  static_assert(r_2 == 2);

  constexpr auto r_20 = iterate<20>([](auto n) { return n + 1; }, 0);
  static_assert(r_20 == 20);

  constexpr auto r_index_2 = iterate_index<2>(ConstexprIterateIndexF(), 0);
  static_assert(r_index_2 == 1);

  constexpr auto r_index_20 = iterate_index<20>(ConstexprIterateIndexF(), 0);
  static_assert(r_index_20 == 190);
}

}  // namespace
}  // namespace tuple
}  // namespace util
