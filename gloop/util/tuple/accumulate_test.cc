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

#include "gloop/util/tuple/accumulate.h"

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/push_front.h"
#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

// This indirection is needed to work around a certain gcc bug.
//
//  template <int... Is>
//  void Foo() {
//    typedef decltype(std::make_tuple(Is...)) T;
//    // Fails when compiled with GCC.
//    static_assert(is_same<T, std::tuple<>>::value, "");
//  }
//
//  Foo<>();
//
// See <internal thread>.
//
// TODO: Remove this struct when the bug is fixed.
template <size_t N>
struct MakeSizeT {
  using type = size_t;
};

template <size_t... N>
std::tuple<typename MakeSizeT<N>::type...> ToTuple(int_pack<N...> pack) {
  return std::make_tuple(N...);
}

template <size_t N>
decltype(ToTuple(make_int_pack<0, N>())) MakeTuple() {
  return ToTuple(make_int_pack<0, N>());
}

template <size_t N, class T>
decltype(push_front(MakeTuple<N>(), std::declval<T>())) MakeTuple(T first) {
  return push_front(MakeTuple<N>(), first);
}

template <class T>
struct ToValueTuple;

template <size_t... N>
struct ToValueTuple<int_pack<N...>> {
  using type = std::tuple<TestValues::Value<N>...>;
};

template <size_t N>
struct MakeValueTuple : ToValueTuple<typename make_int_pack<0, N>::type> {};

TEST(MakeTuple, Functional) {
  // Test for the test helper MakeTuple<N>().
  EXPECT_EQ(MakeTuple<0>(), std::make_tuple());
  EXPECT_EQ(MakeTuple<1>(), std::make_tuple(0));
  EXPECT_EQ(MakeTuple<2>(), std::make_tuple(0, 1));

  EXPECT_EQ(MakeTuple<0>(0.5), std::make_tuple(0.5));
  EXPECT_EQ(MakeTuple<1>(0.5), std::make_tuple(0.5, 0));
  EXPECT_EQ(MakeTuple<2>(0.5), std::make_tuple(0.5, 0, 1));
}

class AccumulateIndex : public TestValues {};

struct AppendIndex {
  template <size_t I, class T>
  std::string operator()(std::string state, const T& value) const {
    if (!state.empty()) state.push_back(' ');
    absl::StrAppend(&state, I, " ", value);
    return state;
  }
};

TEST_F(AccumulateIndex, ReturnByValue) {
  EXPECT_EQ(
      accumulate_index(AppendIndex(), std::make_tuple(), std::string("N")),
      "N");
  EXPECT_EQ(accumulate_index(AppendIndex(), std::make_tuple(42), "N"),
            "N 0 42");
  EXPECT_EQ(accumulate_index(AppendIndex(), std::make_tuple(42, "hello"), "N"),
            "N 0 42 1 hello");

  EXPECT_EQ(accumulate_index(AppendIndex(), MakeTuple<9>(), "N"),
            "N 0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8");
  EXPECT_EQ(accumulate_index(AppendIndex(), MakeTuple<10>(), "N"),
            "N 0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8 9 9");
  EXPECT_EQ(accumulate_index(AppendIndex(), MakeTuple<11>(), "N"),
            "N 0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8 9 9 10 10");
}

TEST_F(AccumulateIndex, ReturnByValueShortcut) {
  EXPECT_EQ(accumulate_index(AppendIndex(), std::make_tuple(std::string("N"))),
            "N");
  EXPECT_EQ(accumulate_index(AppendIndex(), std::make_tuple("N", 42)),
            "N 1 42");
  EXPECT_EQ(accumulate_index(AppendIndex(), std::make_tuple("N", 42, "hello")),
            "N 1 42 2 hello");

  EXPECT_EQ(accumulate_index(AppendIndex(), MakeTuple<9>("N")),
            "N 1 0 2 1 3 2 4 3 5 4 6 5 7 6 8 7 9 8");
  EXPECT_EQ(accumulate_index(AppendIndex(), MakeTuple<10>("N")),
            "N 1 0 2 1 3 2 4 3 5 4 6 5 7 6 8 7 9 8 10 9");
  EXPECT_EQ(accumulate_index(AppendIndex(), MakeTuple<11>("N")),
            "N 1 0 2 1 3 2 4 3 5 4 6 5 7 6 8 7 9 8 10 9 11 10");
}

template <class State>
struct IdentityIndex {
  template <size_t I, class T>
  State& operator()(State& state, const T& value) const {
    return state;
  }
};

TEST_F(AccumulateIndex, ReturnStateByReference) {
  int n = 0;
  EXPECT_EQ(&accumulate_index(IdentityIndex<int>(), MakeTuple<0>(), n), &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<int>(), MakeTuple<1>(), n), &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<int>(), MakeTuple<2>(), n), &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<int>(), MakeTuple<9>(), n), &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<int>(), MakeTuple<10>(), n), &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<int>(), MakeTuple<11>(), n), &n);

  EXPECT_EQ(&accumulate_index(IdentityIndex<const int>(), MakeTuple<0>(), n),
            &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<const int>(), MakeTuple<1>(), n),
            &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<const int>(), MakeTuple<2>(), n),
            &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<const int>(), MakeTuple<9>(), n),
            &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<const int>(), MakeTuple<10>(), n),
            &n);
  EXPECT_EQ(&accumulate_index(IdentityIndex<const int>(), MakeTuple<11>(), n),
            &n);
}

TEST_F(AccumulateIndex, ReturnStateByReferenceShortcut) {
  auto t1 = MakeTuple<1>();
  EXPECT_EQ(&accumulate_index(IdentityIndex<size_t>(), t1), &get<0>(t1));
  auto t2 = MakeTuple<2>();
  EXPECT_EQ(&accumulate_index(IdentityIndex<size_t>(), t2), &get<0>(t2));
  auto t3 = MakeTuple<3>();
  EXPECT_EQ(&accumulate_index(IdentityIndex<size_t>(), t3), &get<0>(t3));
  auto t10 = MakeTuple<10>();
  EXPECT_EQ(&accumulate_index(IdentityIndex<size_t>(), t10), &get<0>(t10));
  auto t12 = MakeTuple<12>();
  EXPECT_EQ(&accumulate_index(IdentityIndex<size_t>(), t12), &get<0>(t12));
  auto t11 = MakeTuple<11>();
  EXPECT_EQ(&accumulate_index(IdentityIndex<size_t>(), t11), &get<0>(t11));

  EXPECT_EQ(&accumulate_index(IdentityIndex<const size_t>(), t1), &get<0>(t1));
  EXPECT_EQ(&accumulate_index(IdentityIndex<const size_t>(), t2), &get<0>(t2));
  EXPECT_EQ(&accumulate_index(IdentityIndex<const size_t>(), t3), &get<0>(t3));
  EXPECT_EQ(&accumulate_index(IdentityIndex<const size_t>(), t10),
            &get<0>(t10));
  EXPECT_EQ(&accumulate_index(IdentityIndex<const size_t>(), t12),
            &get<0>(t12));
  EXPECT_EQ(&accumulate_index(IdentityIndex<const size_t>(), t11),
            &get<0>(t11));
}

struct SecondIndex {
  template <size_t N, class T, class U>
  U& operator()(T& t, U& u) const {
    return u;
  }
};

TEST_F(AccumulateIndex, ReturnElementByReference) {
  EXPECT_EQ(&accumulate_index(SecondIndex(), std::tie(), a), &a);
  EXPECT_EQ(&accumulate_index(SecondIndex(), std::tie(b), a), &b);
  EXPECT_EQ(&accumulate_index(SecondIndex(), std::tie(b, c), a), &c);
}

TEST_F(AccumulateIndex, ReturnElementByReferenceShortcut) {
  EXPECT_EQ(&accumulate_index(SecondIndex(), std::tie(a)), &a);
  EXPECT_EQ(&accumulate_index(SecondIndex(), std::tie(a, b)), &b);
  EXPECT_EQ(&accumulate_index(SecondIndex(), std::tie(a, b, c)), &c);
}

struct PackIndex {
  template <size_t N, class T, class U>
  std::tuple<size_t, T, U> operator()(const T& t, const U& u) const {
    return std::make_tuple(N, t, u);
  }
};

TEST_F(AccumulateIndex, CallTree) {
  EXPECT_EQ(accumulate_index(PackIndex(), std::make_tuple(), a), a);
  EXPECT_EQ(accumulate_index(PackIndex(), std::make_tuple(b), a),
            std::make_tuple(0, a, b));
  EXPECT_EQ(accumulate_index(PackIndex(), std::make_tuple(b, c), a),
            std::make_tuple(1, std::make_tuple(0, a, b), c));
}

TEST_F(AccumulateIndex, CallTreeShortcut) {
  EXPECT_EQ(accumulate_index(PackIndex(), std::make_tuple(a)), a);
  EXPECT_EQ(accumulate_index(PackIndex(), std::make_tuple(a, b)),
            std::make_tuple(1, a, b));
  EXPECT_EQ(accumulate_index(PackIndex(), std::make_tuple(a, b, c)),
            std::make_tuple(2, std::make_tuple(1, a, b), c));
}

struct CopyTracker {
  CopyTracker(int) {}
  CopyTracker(const CopyTracker& other) {
    ADD_FAILURE() << "CopyTracker has been copied (unexpected)";
  }
};

struct PassCopyTrackerIndex {
  template <size_t N, class T>
  CopyTracker operator()(CopyTracker state, const T& t) const {
    return 0;
  }
};

TEST_F(AccumulateIndex, NoCopies) {
  // accumulate_index() over a tuple of 9 elements incurs no copies but
  // with 10 elements it'll incur one copy. Total number of copies performed by
  // this call is floor((N - 1) / 9).
  accumulate_index(PassCopyTrackerIndex(), MakeTuple<9>(), 0);
}

TEST_F(AccumulateIndex, NoCopiesShortcut) {
  accumulate_index(PassCopyTrackerIndex(), MakeTuple<10>());
}

class Accumulate : public TestValues {};

struct Append {
  template <class T>
  std::string operator()(std::string state, const T& value) const {
    if (!state.empty()) state.push_back(' ');
    absl::StrAppend(&state, value);
    return state;
  }
};

TEST_F(Accumulate, ReturnByValue) {
  EXPECT_EQ(accumulate(Append(), std::make_tuple(), "N"), "N");
  EXPECT_EQ(accumulate(Append(), std::make_tuple(42), "N"), "N 42");
  EXPECT_EQ(accumulate(Append(), std::make_tuple(42, "hello"), "N"),
            "N 42 hello");

  EXPECT_EQ(accumulate(Append(), MakeTuple<9>(), "N"), "N 0 1 2 3 4 5 6 7 8");
  EXPECT_EQ(accumulate(Append(), MakeTuple<10>(), "N"),
            "N 0 1 2 3 4 5 6 7 8 9");
  EXPECT_EQ(accumulate(Append(), MakeTuple<11>(), "N"),
            "N 0 1 2 3 4 5 6 7 8 9 10");
}

TEST_F(Accumulate, ReturnByValueShortcut) {
  EXPECT_EQ(accumulate(Append(), std::make_tuple(std::string("N"))), "N");
  EXPECT_EQ(accumulate(Append(), std::make_tuple("N", 42)), "N 42");
  EXPECT_EQ(accumulate(Append(), std::make_tuple("N", 42, "hello")),
            "N 42 hello");

  EXPECT_EQ(accumulate(Append(), MakeTuple<9>("N")), "N 0 1 2 3 4 5 6 7 8");
  EXPECT_EQ(accumulate(Append(), MakeTuple<10>("N")), "N 0 1 2 3 4 5 6 7 8 9");
  EXPECT_EQ(accumulate(Append(), MakeTuple<11>("N")),
            "N 0 1 2 3 4 5 6 7 8 9 10");
}

template <class State>
struct Identity {
  template <class T>
  State& operator()(State& state, const T& value) const {
    return state;
  }
};

TEST_F(Accumulate, ReturnStateByReference) {
  int n = 0;
  EXPECT_EQ(&accumulate(Identity<int>(), MakeTuple<0>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<int>(), MakeTuple<1>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<int>(), MakeTuple<2>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<int>(), MakeTuple<9>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<int>(), MakeTuple<10>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<int>(), MakeTuple<11>(), n), &n);

  EXPECT_EQ(&accumulate(Identity<const int>(), MakeTuple<0>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<const int>(), MakeTuple<1>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<const int>(), MakeTuple<2>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<const int>(), MakeTuple<9>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<const int>(), MakeTuple<10>(), n), &n);
  EXPECT_EQ(&accumulate(Identity<const int>(), MakeTuple<11>(), n), &n);
}

TEST_F(Accumulate, ReturnStateByReferenceShortcut) {
  auto t1 = MakeTuple<1>();
  EXPECT_EQ(&accumulate(Identity<size_t>(), t1), &get<0>(t1));
  auto t2 = MakeTuple<2>();
  EXPECT_EQ(&accumulate(Identity<size_t>(), t2), &get<0>(t2));
  auto t3 = MakeTuple<3>();
  EXPECT_EQ(&accumulate(Identity<size_t>(), t3), &get<0>(t3));
  auto t10 = MakeTuple<10>();
  EXPECT_EQ(&accumulate(Identity<size_t>(), t10), &get<0>(t10));
  auto t11 = MakeTuple<11>();
  EXPECT_EQ(&accumulate(Identity<size_t>(), t11), &get<0>(t11));
  auto t12 = MakeTuple<12>();
  EXPECT_EQ(&accumulate(Identity<size_t>(), t12), &get<0>(t12));

  EXPECT_EQ(&accumulate(Identity<const size_t>(), t1), &get<0>(t1));
  EXPECT_EQ(&accumulate(Identity<const size_t>(), t2), &get<0>(t2));
  EXPECT_EQ(&accumulate(Identity<const size_t>(), t3), &get<0>(t3));
  EXPECT_EQ(&accumulate(Identity<const size_t>(), t10), &get<0>(t10));
  EXPECT_EQ(&accumulate(Identity<const size_t>(), t11), &get<0>(t11));
  EXPECT_EQ(&accumulate(Identity<const size_t>(), t12), &get<0>(t12));
}

struct Second {
  template <class T, class U>
  U& operator()(T& t, U& u) const {
    return u;
  }
};

TEST_F(Accumulate, ReturnElementByReference) {
  EXPECT_EQ(&accumulate(Second(), std::tie(), a), &a);
  EXPECT_EQ(&accumulate(Second(), std::tie(b), a), &b);
  EXPECT_EQ(&accumulate(Second(), std::tie(b, c), a), &c);
}

TEST_F(Accumulate, ReturnElementByReferenceShortcut) {
  EXPECT_EQ(&accumulate(Second(), std::tie(a)), &a);
  EXPECT_EQ(&accumulate(Second(), std::tie(a, b)), &b);
  EXPECT_EQ(&accumulate(Second(), std::tie(a, b, c)), &c);
}

struct Pack {
  template <class T, class U>
  std::tuple<T, U> operator()(const T& t, const U& u) const {
    return std::make_tuple(t, u);
  }
};

TEST_F(Accumulate, CallTree) {
  EXPECT_EQ(accumulate(Pack(), std::make_tuple(), a), a);
  EXPECT_EQ(accumulate(Pack(), std::make_tuple(b), a), std::make_tuple(a, b));
  EXPECT_EQ(accumulate(Pack(), std::make_tuple(b, c), a),
            std::make_tuple(std::make_tuple(a, b), c));
}

TEST_F(Accumulate, CallTreeShortcut) {
  EXPECT_EQ(accumulate(Pack(), std::make_tuple(a)), a);
  EXPECT_EQ(accumulate(Pack(), std::make_tuple(a, b)), std::make_tuple(a, b));
  EXPECT_EQ(accumulate(Pack(), std::make_tuple(a, b, c)),
            std::make_tuple(std::make_tuple(a, b), c));
}

struct PassCopyTracker {
  template <class T>
  CopyTracker operator()(CopyTracker state, const T& t) const {
    return 0;
  }
};

TEST_F(Accumulate, NoCopies) {
  // accumulate() over a tuple of 9 elements incurs no copies but
  // with 10 elements it'll incur one copy. Total number of copies performed by
  // this call is floor((N - 1) / 9).
  accumulate(PassCopyTracker(), MakeTuple<9>(), 0);
}

TEST_F(Accumulate, NoCopiesShortcut) {
  accumulate(PassCopyTracker(), MakeTuple<10>());
}

class AccumulateTypeIndex : public TestValues {};

struct AppendTypeIndex {
  template <size_t I, class T>
  std::string operator()(std::string state) const {
    if (!state.empty()) state.push_back(' ');
    absl::StrAppend(&state, I, " ", T::value);
    return state;
  }
};

TEST_F(AccumulateTypeIndex, ReturnByValue) {
  EXPECT_EQ(accumulate_index<std::tuple<>>(AppendTypeIndex(), std::string("N")),
            "N");
  EXPECT_EQ(accumulate_index<std::tuple<A>>(AppendTypeIndex(), "N"), "N 0 0");
  EXPECT_EQ((accumulate_index<std::tuple<A, B>>(AppendTypeIndex(), "N")),
            "N 0 0 1 1");
}

template <class State>
struct IdentityTypeIndex {
  template <size_t I, class T>
  State& operator()(State& state) const {
    return state;
  }
};

TEST_F(AccumulateTypeIndex, ReturnStateByReference) {
  int n = 0;
  EXPECT_EQ(
      &accumulate_index<MakeValueTuple<0>::type>(IdentityTypeIndex<int>(), n),
      &n);
  EXPECT_EQ(
      &accumulate_index<MakeValueTuple<1>::type>(IdentityTypeIndex<int>(), n),
      &n);
  EXPECT_EQ(
      &accumulate_index<MakeValueTuple<2>::type>(IdentityTypeIndex<int>(), n),
      &n);
  EXPECT_EQ(
      &accumulate_index<MakeValueTuple<8>::type>(IdentityTypeIndex<int>(), n),
      &n);
  EXPECT_EQ(
      &accumulate_index<MakeValueTuple<9>::type>(IdentityTypeIndex<int>(), n),
      &n);
  EXPECT_EQ(
      &accumulate_index<MakeValueTuple<10>::type>(IdentityTypeIndex<int>(), n),
      &n);

  EXPECT_EQ(&accumulate_index<MakeValueTuple<0>::type>(
                IdentityTypeIndex<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate_index<MakeValueTuple<1>::type>(
                IdentityTypeIndex<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate_index<MakeValueTuple<2>::type>(
                IdentityTypeIndex<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate_index<MakeValueTuple<8>::type>(
                IdentityTypeIndex<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate_index<MakeValueTuple<9>::type>(
                IdentityTypeIndex<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate_index<MakeValueTuple<10>::type>(
                IdentityTypeIndex<const int>(), n),
            &n);
}

struct PackTypeIndex {
  template <size_t N, class T, class U>
  std::tuple<size_t, U, int> operator()(const U& u) const {
    return std::make_tuple(N, u, T::value);
  }
};

TEST_F(AccumulateTypeIndex, CallTree) {
  EXPECT_EQ(accumulate_index<std::tuple<>>(PackTypeIndex(), a), a);
  EXPECT_EQ(accumulate_index<std::tuple<B>>(PackTypeIndex(), a),
            std::make_tuple(0, a, 1));
  EXPECT_EQ((accumulate_index<std::tuple<B, C>>(PackTypeIndex(), a)),
            std::make_tuple(1, std::make_tuple(0, a, 1), 2));
}

struct PassCopyTrackerTypeIndex {
  template <size_t N, class T>
  CopyTracker operator()(CopyTracker state) const {
    return 0;
  }
};

TEST_F(AccumulateTypeIndex, NoCopies) {
  accumulate_index<MakeValueTuple<9>::type>(PassCopyTrackerTypeIndex(), 0);
}

class AccumulateType : public TestValues {};

struct AppendType {
  template <class T>
  std::string operator()(std::string state) const {
    if (!state.empty()) state.push_back(' ');
    absl::StrAppend(&state, T::value);
    return state;
  }
};

TEST_F(AccumulateType, ReturnByValue) {
  EXPECT_EQ(accumulate<std::tuple<>>(AppendType(), std::string("N")), "N");
  EXPECT_EQ(accumulate<std::tuple<A>>(AppendType(), "N"), "N 0");
  EXPECT_EQ((accumulate<std::tuple<A, B>>(AppendType(), "N")), "N 0 1");
}

template <class State>
struct IdentityType {
  template <class T>
  State& operator()(State& state) const {
    return state;
  }
};

TEST_F(AccumulateType, ReturnStateByReference) {
  int n = 0;
  EXPECT_EQ(&accumulate<MakeValueTuple<0>::type>(IdentityType<int>(), n), &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<1>::type>(IdentityType<int>(), n), &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<2>::type>(IdentityType<int>(), n), &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<8>::type>(IdentityType<int>(), n), &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<9>::type>(IdentityType<int>(), n), &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<10>::type>(IdentityType<int>(), n), &n);

  EXPECT_EQ(&accumulate<MakeValueTuple<0>::type>(IdentityType<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<1>::type>(IdentityType<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<2>::type>(IdentityType<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<8>::type>(IdentityType<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<9>::type>(IdentityType<const int>(), n),
            &n);
  EXPECT_EQ(&accumulate<MakeValueTuple<10>::type>(IdentityType<const int>(), n),
            &n);
}

struct PackType {
  template <class T, class U>
  std::tuple<U, int> operator()(const U& u) const {
    return std::make_tuple(u, T::value);
  }
};

TEST_F(AccumulateType, CallTree) {
  EXPECT_EQ(accumulate<std::tuple<>>(PackType(), a), a);
  EXPECT_EQ(accumulate<std::tuple<B>>(PackType(), a), std::make_tuple(a, 1));
  EXPECT_EQ((accumulate<std::tuple<B, C>>(PackType(), a)),
            std::make_tuple(std::make_tuple(a, 1), 2));
}

struct PassCopyTrackerType {
  template <class T>
  CopyTracker operator()(CopyTracker state) const {
    return 0;
  }
};

TEST_F(AccumulateType, NoCopies) {
  accumulate<MakeValueTuple<9>::type>(PassCopyTrackerType(), 0);
}

struct AddOne {
  template <typename Element>
  constexpr size_t operator()(size_t s) const {
    return s + 1;
  }
};

TEST(AccumulateConstexpr, Constexpr) {
  constexpr size_t k0 = accumulate<std::tuple<>>(AddOne{}, 0);
  EXPECT_EQ(k0, 0);
  constexpr size_t k1 = accumulate<std::tuple<size_t>>(AddOne{}, 0);
  EXPECT_EQ(k1, 1);
  constexpr size_t k2 = accumulate<std::tuple<size_t, size_t>>(AddOne{}, 0);
  EXPECT_EQ(k2, 2);
  constexpr size_t k9 =
      accumulate<std::tuple<size_t, size_t, size_t, size_t, size_t, size_t,
                            size_t, size_t, size_t>>(AddOne{}, 0);
  EXPECT_EQ(k9, 9);
  constexpr size_t k10 =
      accumulate<std::tuple<size_t, size_t, size_t, size_t, size_t, size_t,
                            size_t, size_t, size_t, size_t>>(AddOne{}, 0);
  EXPECT_EQ(k10, 10);
  constexpr size_t k11 =
      accumulate<std::tuple<size_t, size_t, size_t, size_t, size_t, size_t,
                            size_t, size_t, size_t, size_t, size_t>>(AddOne{},
                                                                     0);
  EXPECT_EQ(k11, 11);
}

}  // namespace
}  // namespace tuple
}  // namespace util
