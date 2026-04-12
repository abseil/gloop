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

#include "gloop/util/gtl/switch.h"

#include <stddef.h>

#include <functional>
#include <memory>
#include <type_traits>

#include "gloop/gloop_test.h"

namespace gtl {
namespace {

struct MakeVoid {
  template <int I>
  void operator()(std::integral_constant<int, I>) const {}
};

TEST(SwitchIndex, VoidResult) { switch_index<0, 1>(MakeVoid(), 0); }

struct Identity {
  // Check that the return values are not copied, only moved.
  template <int I>
  std::unique_ptr<int> operator()(std::integral_constant<int, I>) const {
    return std::unique_ptr<int>(new int(I));
  }
};

struct MutableIdentity {
  int counter = 0;
  template <int I>
  void operator()(std::integral_constant<int, I>) {
    counter += I;
  }
};

struct SetValue {
  std::function<void(int)> f;
  template <int I>
  void operator()(std::integral_constant<int, I>) {
    f(I);
  }
};

template <int From, int N>
void TestSwitchIndex() {
  int counter = 0;
  MutableIdentity f;
  for (int i = From; i != From + N + 1; ++i) {
    // Test return values. They must be moved.
    EXPECT_THAT((switch_index<From, From + N + 1>(Identity(), i)),
                ::testing::Pointee(i));

    // Doesn't copy the functor.
    switch_index<From, From + N + 1>(f, i);
    counter += i;
    EXPECT_EQ(counter, f.counter);

    // Test side effects.
    ::testing::MockFunction<void(int)> callback;
    EXPECT_CALL(callback, Call(i));
    switch_index<From, From + N + 1>(SetValue{callback.AsStdFunction()}, i);
  }
}

template <int From>
void TestFrom() {
  TestSwitchIndex<From, 1>();
  TestSwitchIndex<From, 2>();
  TestSwitchIndex<From, 3>();
  TestSwitchIndex<From, 4>();
  TestSwitchIndex<From, 5>();
  TestSwitchIndex<From, 6>();
  TestSwitchIndex<From, 7>();
  TestSwitchIndex<From, 8>();
  TestSwitchIndex<From, 9>();
  TestSwitchIndex<From, 10>();
  TestSwitchIndex<From, 11>();
  TestSwitchIndex<From, 12>();
  TestSwitchIndex<From, 13>();
  TestSwitchIndex<From, 14>();
  TestSwitchIndex<From, 15>();
  TestSwitchIndex<From, 16>();
  TestSwitchIndex<From, 17>();
  TestSwitchIndex<From, 18>();
  TestSwitchIndex<From, 19>();
  TestSwitchIndex<From, 20>();
}

TEST(SwitchIndex, Functional) {
  // Test switch_index<From, From + N>(f, idx) with all values of From in
  // [-2, 5), N in [1, 20] and idx in [From, From + N).
  TestFrom<-2>();
  TestFrom<-1>();
  TestFrom<0>();
  TestFrom<1>();
  TestFrom<2>();
  TestFrom<3>();
  TestFrom<4>();
}

struct ConstantToPointerHelper {
  template <typename T>
  ConstantToPointerHelper(T)  // NOLINT
      : ptr(&T::value) {}
  const int* ptr;
};

const int* ConstantToPointer(ConstantToPointerHelper ptr) { return ptr.ptr; }

TEST(SwitchIndex, WorksWithFunctionPointer) {
  EXPECT_EQ((&std::integral_constant<int, 3>::value),
            (switch_index<0, 10>(&ConstantToPointer, 3)));
}

struct Overloaded {
  template <typename T>
  bool operator()(T) const {
    return false;
  }
  bool operator()(std::integral_constant<int, 7>) const { return true; }
};

TEST(SwitchIndex, WorksWithOverlads) {
  EXPECT_FALSE((switch_index<0, 10>(Overloaded(), 3)));
  EXPECT_TRUE((switch_index<0, 10>(Overloaded(), 7)));
}

// Verify that switch_index compiles when the range is large.
TEST(SwitchIndex, LargeRange) { switch_index<0, 8 << 10>(MakeVoid(), 0); }

// TODO: Add test case for generic lambdas when available.
// Tested with --per_file_copt=util/gtl/switch_test.cc@--std=c++14
#if 0
TEST(SwitchIndex, WorksWithGenericLambdas) {
  EXPECT_EQ(
      (&std::integral_constant<int, 3>::value),
      (switch_index<0, 10>([](auto n) { return &decltype(n)::value; }, 3)));
}
#endif

}  // namespace
}  // namespace gtl
