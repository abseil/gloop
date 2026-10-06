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

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::MockFunction;
using ::testing::Pointee;

struct MakeVoid {
  template <int I>
  void operator()(std::integral_constant<int, I>) const {}
};

// Verify that switch_index compiles and runs with functors returning void.
TEST(SwitchIndex, VoidResult) { switch_index<0, 1>(MakeVoid(), 0); }

struct Identity {
  // Check that the return values are not copied, only moved.
  template <int I>
  std::unique_ptr<int> operator()(std::integral_constant<int, I>) const {
    return std::make_unique<int>(I);
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
struct SwitchCase {
  static constexpr int kFrom = From;
  static constexpr int kN = N;
};

// Generates the Cartesian product of [FromMin, FromMax] x [NMin, NMax] as a
// ::testing::Types list of SwitchCase<From, N>.
template <int FromMin, int FromMax, int NMin, int NMax,
          typename = std::make_index_sequence<(FromMax - FromMin + 1) *
                                              (NMax - NMin + 1)>>
struct CrossProduct;

template <int FromMin, int FromMax, int NMin, int NMax, std::size_t... Is>
struct CrossProduct<FromMin, FromMax, NMin, NMax, std::index_sequence<Is...>> {
  static constexpr int kNCount = NMax - NMin + 1;

  using type =
      ::testing::Types<SwitchCase<FromMin + static_cast<int>(Is / kNCount),
                                  NMin + static_cast<int>(Is % kNCount)>...>;
};

struct SwitchCaseNameGenerator {
  template <typename T>
  static std::string GetName(int) {
    return absl::StrCat(T::kFrom < 0 ? "FromNeg" : "From",
                        T::kFrom < 0 ? -T::kFrom : T::kFrom, "_N", T::kN);
  }
};

template <typename T>
class SwitchIndexTest : public ::testing::Test {};

using SwitchCases = typename CrossProduct<-2, 4, 1, 20>::type;

TYPED_TEST_SUITE(SwitchIndexTest, SwitchCases, SwitchCaseNameGenerator);

// Test switch_index<From, From + N>(f, idx) with all values of From in
// [-2, 5), N in [1, 20] and idx in [From, From + N).
TYPED_TEST(SwitchIndexTest, Functional) {
  constexpr int kFrom = TypeParam::kFrom;
  constexpr int kN = TypeParam::kN;

  int counter = 0;
  MutableIdentity f;
  for (int i = kFrom; i != kFrom + kN + 1; ++i) {
    // Test return values. They must be moved.
    EXPECT_THAT((switch_index<kFrom, kFrom + kN + 1>(Identity(), i)),
                Pointee(i));

    // Doesn't copy the functor.
    switch_index<kFrom, kFrom + kN + 1>(f, i);
    counter += i;
    EXPECT_EQ(f.counter, counter);

    // Test side effects.
    MockFunction<void(int)> callback;
    EXPECT_CALL(callback, Call(i));
    switch_index<kFrom, kFrom + kN + 1>(SetValue{callback.AsStdFunction()}, i);
  }
}

struct ConstantToPointerHelper {
  template <typename T>
  ConstantToPointerHelper(T)  // NOLINT
      : ptr(&T::value) {}
  const int* ptr;
};

const int* ConstantToPointer(ConstantToPointerHelper ptr) { return ptr.ptr; }

TEST(SwitchIndex, WorksWithFunctionPointer) {
  EXPECT_EQ((switch_index<0, 10>(&ConstantToPointer, 3)),
            (&std::integral_constant<int, 3>::value));
}

struct Overloaded {
  template <typename T>
  bool operator()(T) const {
    return false;
  }
  bool operator()(std::integral_constant<int, 7>) const { return true; }
};

TEST(SwitchIndex, WorksWithOverloads) {
  EXPECT_FALSE((switch_index<0, 10>(Overloaded(), 3)));
  EXPECT_TRUE((switch_index<0, 10>(Overloaded(), 7)));
}

// Verify that switch_index compiles when the range is large.
TEST(SwitchIndex, LargeRange) { switch_index<0, 8 << 10>(MakeVoid(), 0); }

TEST(SwitchIndex, WorksWithGenericLambdas) {
  EXPECT_EQ(
      (switch_index<0, 10>([](auto n) { return &decltype(n)::value; }, 3)),
      (&std::integral_constant<int, 3>::value));
}

}  // namespace
}  // namespace gtl
