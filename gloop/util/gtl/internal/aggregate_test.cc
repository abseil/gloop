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

#include "gloop/util/gtl/internal/aggregate.h"

#include <tuple>
#include <type_traits>
#include <utility>

#include "gloop/gloop_test.h"

namespace gtl::internal_aggregate {
namespace {

template <typename T,
          typename = std::enable_if_t<!std::is_same_v<T, FieldGetter::Error>>>
T NotAnError(T);

// Given a reference to an aggregate `T`, constructs a tuple of references to
// the fields in the aggregate. This only works for types that have either no
// base class or 1 empty base class.
template <typename T>
auto Unpack(T&& t) -> decltype(NotAnError(
    FieldGetter::Unpack<
        0, gtl::internal_aggregate::NumFields<std::decay_t<T>>(), T>(
        std::forward<T>(t)))) {
  return FieldGetter::Unpack<
      0, gtl::internal_aggregate::NumFields<std::decay_t<T>>(), T>(
      std::forward<T>(t));
}

TEST(Qualifiers, Collapse) {
  EXPECT_EQ(kRef | kRefRef, kRef);
  EXPECT_NE(kConst, kVolatile);
  EXPECT_NE(kConst, kRef);
  EXPECT_NE(kConst, kRefRef);
  EXPECT_NE(kVolatile, kRef);
  EXPECT_NE(kVolatile, kRefRef);
  EXPECT_NE(kRef, kRefRef);
}

TEST(ExtractQualifiers, Works) {
  EXPECT_EQ(ExtractQualifiers<int>(), 0);
  EXPECT_EQ(ExtractQualifiers<int&>(), kRef);
  EXPECT_EQ(ExtractQualifiers<int&&>(), kRefRef);
  EXPECT_EQ(ExtractQualifiers<const int>(), kConst);
  EXPECT_EQ(ExtractQualifiers<const int&>(), kConst | kRef);
  EXPECT_EQ(ExtractQualifiers<const int&&>(), kConst | kRefRef);
  EXPECT_EQ(ExtractQualifiers<volatile int>(), kVolatile);
  EXPECT_EQ(ExtractQualifiers<volatile int&>(), kVolatile | kRef);
  EXPECT_EQ(ExtractQualifiers<volatile int&&>(), kVolatile | kRefRef);
  EXPECT_EQ(ExtractQualifiers<volatile const int>(), kVolatile | kConst);
  EXPECT_EQ(ExtractQualifiers<volatile const int&>(),
            kVolatile | kConst | kRef);
  EXPECT_EQ(ExtractQualifiers<volatile const int&&>(),
            kVolatile | kConst | kRefRef);
}

TEST(ApplyQualifiers, OnBareType) {
  EXPECT_TRUE((std::is_same_v<typename ApplyQualifiers<int, 0>::type, int>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kRef>::type, int&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kRefRef>::type, int&&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kConst>::type, const int>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kConst | kRef>::type,
                      const int&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kConst | kRefRef>::type,
                      const int&&>));
  EXPECT_TRUE((std::is_same_v<typename ApplyQualifiers<int, kVolatile>::type,
                              volatile int>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kVolatile | kRef>::type,
                      volatile int&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kVolatile | kRefRef>::type,
                      volatile int&&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<int, kVolatile | kConst>::type,
                      volatile const int>));
  EXPECT_TRUE((std::is_same_v<
               typename ApplyQualifiers<int, kVolatile | kConst | kRef>::type,
               volatile const int&>));
  EXPECT_TRUE(
      (std::is_same_v<
          typename ApplyQualifiers<int, kVolatile | kConst | kRefRef>::type,
          volatile const int&&>));
}

TEST(ApplyQualifiers, OnConstType) {
  EXPECT_TRUE((
      std::is_same_v<typename ApplyQualifiers<const int, 0>::type, const int>));
  EXPECT_TRUE((std::is_same_v<typename ApplyQualifiers<const int, kRef>::type,
                              const int&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<const int, kRefRef>::type,
                      const int&&>));
  EXPECT_TRUE((std::is_same_v<typename ApplyQualifiers<const int, kConst>::type,
                              const int>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<const int, kConst | kRef>::type,
                      const int&>));
  EXPECT_TRUE((std::is_same_v<
               typename ApplyQualifiers<const int, kConst | kRefRef>::type,
               const int&&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<const int, kVolatile>::type,
                      const volatile int>));
  EXPECT_TRUE((std::is_same_v<
               typename ApplyQualifiers<const int, kVolatile | kRef>::type,
               const volatile int&>));
  EXPECT_TRUE((std::is_same_v<
               typename ApplyQualifiers<const int, kVolatile | kRefRef>::type,
               const volatile int&&>));
  EXPECT_TRUE((std::is_same_v<
               typename ApplyQualifiers<const int, kVolatile | kConst>::type,
               volatile const int>));
  EXPECT_TRUE(
      (std::is_same_v<
          typename ApplyQualifiers<const int, kVolatile | kConst | kRef>::type,
          volatile const int&>));
  EXPECT_TRUE(
      (std::is_same_v<typename ApplyQualifiers<const int, kVolatile | kConst |
                                                              kRefRef>::type,
                      volatile const int&&>));
}

TEST(CorrectQualifiers, Type) {
  struct A {
    int val;
    int&& rval;
    const int cval;
    const int& clval;
    const int&& crval;
  };

  A a{
      3,                                // .val
      static_cast<int&&>(a.val),        // .rval
      3,                                // .cval
      a.cval,                           // .clval
      static_cast<const int&&>(a.cval)  // .crval
  };
  auto&& [v, r, c, cl, cr] = a;
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A, decltype(v)>(v)),
                              int&&>();
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A, decltype(r)>(r)),
                              int&&>();
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A, decltype(c)>(c)),
                              const int&&>();
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A, decltype(cl)>(cl)),
                              const int&>();

  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A&, decltype(v)>(v)),
                              int&>();
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A&, decltype(r)>(r)),
                              int&&>();
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A&, decltype(c)>(c)),
                              const int&>();
  testing::StaticAssertTypeEq<decltype(CorrectQualifiers<A&, decltype(cl)>(cl)),
                              const int&>();

  testing::StaticAssertTypeEq<
      decltype(CorrectQualifiers<const A&, decltype(v)>(v)), const int&>();
  testing::StaticAssertTypeEq<
      decltype(CorrectQualifiers<const A&, decltype(r)>(r)), int&&>();
}

TEST(RemoveQualifiersAndReferencesFromTuple, Type) {
  EXPECT_TRUE(
      (std::is_same_v<RemoveQualifiersAndReferencesFromTuple<std::tuple<
                          int, int&, const int&, volatile const int&>>::type,
                      std::tuple<int, int, int, int>>));
}

TEST(Unpack, Basic) {
  struct A1 {
    int a;
  };
  struct A2 {
    int a;
    int b;
  };
  struct A3 {
    int a;
    int&& b;
  };

  {
    A1 a;
    auto [f1] = Unpack(a);
    EXPECT_EQ(&f1, &a.a);
  }

  {
    A2 a;
    auto [f1, f2] = Unpack(a);
    EXPECT_EQ(&f1, &a.a);
    EXPECT_EQ(&f2, &a.b);
  }

  {
    int b = 0;
    A3 a{0, std::move(b)};  // NOLINT(performance-move-const-arg)
    auto [f1, f2] = Unpack(a);
    EXPECT_EQ(&f1, &a.a);
    EXPECT_EQ(&f2, &a.b);
  }
}

TEST(Unpack, Types) {
  struct A3 {
    int field_v;
    const int& field_l;
    int&& field_r;
  };

  int x = 0, y = 0;
  A3 my_struct{0, x, std::move(y)};  // NOLINT(performance-move-const-arg)
  auto unpacked = Unpack(my_struct);
  const auto& const_lvalue_ref = my_struct;
  auto unpacked_const_lvalue_ref = Unpack(const_lvalue_ref);
  auto unpacked_rvalue_ref = Unpack(std::move(my_struct));
  testing::StaticAssertTypeEq<decltype(unpacked),
                              std::tuple<int&, const int&, int&&>>();
  testing::StaticAssertTypeEq<decltype(unpacked_const_lvalue_ref),
                              std::tuple<const int&, const int&, int&&>>();
  testing::StaticAssertTypeEq<decltype(unpacked_rvalue_ref),
                              std::tuple<int&&, const int&, int&&>>();
}

TEST(Unpack, Const) {
  struct A1 {
    const int a;
  };
  struct A2 {
    const int a;
    const int b;
  };
  struct A3 {
    const int a;
    const int& b;
    int&& c;
  };

  {
    A1 a{};
    auto [f1] = Unpack(a);
    EXPECT_EQ(&f1, &a.a);
  }

  {
    A2 a{};
    auto [f1, f2] = Unpack(a);
    EXPECT_EQ(&f1, &a.a);
    EXPECT_EQ(&f2, &a.b);
  }

  {
    int b = 0, c = 0;
    A3 a{0, b, std::move(c)};  // NOLINT(performance-move-const-arg)
    auto [f1, f2, f3] = Unpack(a);
    EXPECT_EQ(&f1, &a.a);
    EXPECT_EQ(&f2, &a.b);
    EXPECT_EQ(&f3, &a.c);
  }
}

TEST(Unpack, BaseClass) {
  struct Base {};
  struct A2 : Base {
    int arg1, arg2;
  };

  A2 a;
  auto [f1, f2] = Unpack(a);
  EXPECT_EQ(&f1, &a.arg1);
  EXPECT_EQ(&f2, &a.arg2);
}

TEST(Unpack, Immovable) {
  struct Immovable {
    Immovable() = default;
    Immovable(Immovable&&) = delete;
  };
  struct A2 {
    const Immovable& a;
    Immovable&& b;
  };

  Immovable i1, i2;
  A2 a{i1, std::move(i2)};
  auto [f1, f2] = Unpack(a);
  EXPECT_EQ(&f1, &a.a);
  EXPECT_EQ(&f2, &a.b);
}

TEST(Unpack, Empty) {
  struct Empty {};
  EXPECT_TRUE((std::is_same_v<std::tuple<>, decltype(Unpack(Empty{}))>));
  Empty lvalue_empty;
  EXPECT_TRUE((std::is_same_v<std::tuple<>, decltype(Unpack(lvalue_empty))>));

  struct EmptyWithBase : Empty {};
  EXPECT_TRUE(
      (std::is_same_v<std::tuple<>, decltype(Unpack(EmptyWithBase{}))>));
}

TEST(Unpack, Autodetect) {
  struct NoBases {
    int i = 7;
  } no_bases;
  auto [i2] = Unpack(no_bases);
  EXPECT_EQ(i2, 7);

  struct Base {};
  struct OneBase : Base {
    int j = 17;
  } one_base;
  auto [j2] = Unpack(one_base);
  EXPECT_EQ(j2, 17);

  constexpr auto try_unpack =
      [](auto&& v) -> decltype(NumBases(std::forward<decltype(v)>(v))) {};

  struct Base2 {};
  struct TwoBases : Base, Base2 {
    int k;
  };
  EXPECT_FALSE((std::is_invocable_v<decltype(try_unpack), TwoBases>));
}

TEST(Unpack, FailsSubstitution) {
  struct Aggregate {
    int i;
  };
  struct NonAggregate {
    explicit NonAggregate(int) {}
    int i;
  };

  const auto unpack =
      [](auto&& v) -> decltype(Unpack(std::forward<decltype(v)>(v))) {};
  EXPECT_TRUE((std::is_invocable_v<decltype(unpack), Aggregate>));
  EXPECT_FALSE((std::is_invocable_v<decltype(unpack), NonAggregate>));
}

template <int N, class T>
void CheckTupleElementsEqualToIndex(const T& t) {
  if constexpr (N > 0) {
    EXPECT_EQ(std::get<N - 1>(t), N - 1);
    CheckTupleElementsEqualToIndex<N - 1>(t);
  }
}

TEST(Unpack, CorrectOrder1) {
  struct S1 {
    int arg0 = 0;
  };
  CheckTupleElementsEqualToIndex<1>(Unpack(S1()));
}

TEST(Unpack, CorrectOrder2) {
  struct S2 {
    int arg0 = 0, arg1 = 1;
  };
  CheckTupleElementsEqualToIndex<2>(Unpack(S2()));
}

TEST(Unpack, CorrectOrder3) {
  struct S3 {
    int arg0 = 0, arg1 = 1, arg2 = 2;
  };
  CheckTupleElementsEqualToIndex<3>(Unpack(S3()));
}

TEST(Unpack, CorrectOrder4) {
  struct S4 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3;
  };
  CheckTupleElementsEqualToIndex<4>(Unpack(S4()));
}

TEST(Unpack, CorrectOrder5) {
  struct S5 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4;
  };
  CheckTupleElementsEqualToIndex<5>(Unpack(S5()));
}

TEST(Unpack, CorrectOrder6) {
  struct S6 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5;
  };
  CheckTupleElementsEqualToIndex<6>(Unpack(S6()));
}

TEST(Unpack, CorrectOrder7) {
  struct S7 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6;
  };
  CheckTupleElementsEqualToIndex<7>(Unpack(S7()));
}

TEST(Unpack, CorrectOrder8) {
  struct S8 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7;
  };
  CheckTupleElementsEqualToIndex<8>(Unpack(S8()));
}

TEST(Unpack, CorrectOrder9) {
  struct S9 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8;
  };
  CheckTupleElementsEqualToIndex<9>(Unpack(S9()));
}

TEST(Unpack, CorrectOrder10) {
  struct S10 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9;
  };
  CheckTupleElementsEqualToIndex<10>(Unpack(S10()));
}

TEST(Unpack, CorrectOrder11) {
  struct S11 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10;
  };
  CheckTupleElementsEqualToIndex<11>(Unpack(S11()));
}

TEST(Unpack, CorrectOrder12) {
  struct S12 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11;
  };
  CheckTupleElementsEqualToIndex<12>(Unpack(S12()));
}

TEST(Unpack, CorrectOrder13) {
  struct S13 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12;
  };
  CheckTupleElementsEqualToIndex<13>(Unpack(S13()));
}

TEST(Unpack, CorrectOrder14) {
  struct S14 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13;
  };
  CheckTupleElementsEqualToIndex<14>(Unpack(S14()));
}

TEST(Unpack, CorrectOrder15) {
  struct S15 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14;
  };
  CheckTupleElementsEqualToIndex<15>(Unpack(S15()));
}

TEST(Unpack, CorrectOrder16) {
  struct S16 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15;
  };
  CheckTupleElementsEqualToIndex<16>(Unpack(S16()));
}

TEST(Unpack, CorrectOrder17) {
  struct S17 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16;
  };
  CheckTupleElementsEqualToIndex<17>(Unpack(S17()));
}

TEST(Unpack, CorrectOrder18) {
  struct S18 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17;
  };
  CheckTupleElementsEqualToIndex<18>(Unpack(S18()));
}

TEST(Unpack, CorrectOrder19) {
  struct S19 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18;
  };
  CheckTupleElementsEqualToIndex<19>(Unpack(S19()));
}

TEST(Unpack, CorrectOrder20) {
  struct S20 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19;
  };
  CheckTupleElementsEqualToIndex<20>(Unpack(S20()));
}

TEST(Unpack, CorrectOrder21) {
  struct S21 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20;
  };
  CheckTupleElementsEqualToIndex<21>(Unpack(S21()));
}

TEST(Unpack, CorrectOrder22) {
  struct S22 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21;
  };
  CheckTupleElementsEqualToIndex<22>(Unpack(S22()));
}

TEST(Unpack, CorrectOrder23) {
  struct S23 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22;
  };
  CheckTupleElementsEqualToIndex<23>(Unpack(S23()));
}

TEST(Unpack, CorrectOrder24) {
  struct S24 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23;
  };
  CheckTupleElementsEqualToIndex<24>(Unpack(S24()));
}

TEST(Unpack, CorrectOrder25) {
  struct S25 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24;
  };
  CheckTupleElementsEqualToIndex<25>(Unpack(S25()));
}

TEST(Unpack, CorrectOrder26) {
  struct S26 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25;
  };
  CheckTupleElementsEqualToIndex<26>(Unpack(S26()));
}

TEST(Unpack, CorrectOrder27) {
  struct S27 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26;
  };
  CheckTupleElementsEqualToIndex<27>(Unpack(S27()));
}

TEST(Unpack, CorrectOrder28) {
  struct S28 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27;
  };
  CheckTupleElementsEqualToIndex<28>(Unpack(S28()));
}

TEST(Unpack, CorrectOrder29) {
  struct S29 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28;
  };
  CheckTupleElementsEqualToIndex<29>(Unpack(S29()));
}

TEST(Unpack, CorrectOrder30) {
  struct S30 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29;
  };
  CheckTupleElementsEqualToIndex<30>(Unpack(S30()));
}

TEST(Unpack, CorrectOrder31) {
  struct S31 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30;
  };
  CheckTupleElementsEqualToIndex<31>(Unpack(S31()));
}

TEST(Unpack, CorrectOrder32) {
  struct S32 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31;
  };
  CheckTupleElementsEqualToIndex<32>(Unpack(S32()));
}

TEST(Unpack, CorrectOrder33) {
  struct S33 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32;
  };
  CheckTupleElementsEqualToIndex<33>(Unpack(S33()));
}

TEST(Unpack, CorrectOrder34) {
  struct S34 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33;
  };
  CheckTupleElementsEqualToIndex<34>(Unpack(S34()));
}

TEST(Unpack, CorrectOrder35) {
  struct S35 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34;
  };
  CheckTupleElementsEqualToIndex<35>(Unpack(S35()));
}

TEST(Unpack, CorrectOrder36) {
  struct S36 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35;
  };
  CheckTupleElementsEqualToIndex<36>(Unpack(S36()));
}

TEST(Unpack, CorrectOrder37) {
  struct S37 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36;
  };
  CheckTupleElementsEqualToIndex<37>(Unpack(S37()));
}

TEST(Unpack, CorrectOrder38) {
  struct S38 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37;
  };
  CheckTupleElementsEqualToIndex<38>(Unpack(S38()));
}

TEST(Unpack, CorrectOrder39) {
  struct S39 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38;
  };
  CheckTupleElementsEqualToIndex<39>(Unpack(S39()));
}

TEST(Unpack, CorrectOrder40) {
  struct S40 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39;
  };
  CheckTupleElementsEqualToIndex<40>(Unpack(S40()));
}

TEST(Unpack, CorrectOrder41) {
  struct S41 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40;
  };
  CheckTupleElementsEqualToIndex<41>(Unpack(S41()));
}

TEST(Unpack, CorrectOrder42) {
  struct S42 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41;
  };
  CheckTupleElementsEqualToIndex<42>(Unpack(S42()));
}

TEST(Unpack, CorrectOrder43) {
  struct S43 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42;
  };
  CheckTupleElementsEqualToIndex<43>(Unpack(S43()));
}

TEST(Unpack, CorrectOrder44) {
  struct S44 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43;
  };
  CheckTupleElementsEqualToIndex<44>(Unpack(S44()));
}

TEST(Unpack, CorrectOrder45) {
  struct S45 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43, arg44 = 44;
  };
  CheckTupleElementsEqualToIndex<45>(Unpack(S45()));
}

TEST(Unpack, CorrectOrder46) {
  struct S46 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43, arg44 = 44, arg45 = 45;
  };
  CheckTupleElementsEqualToIndex<46>(Unpack(S46()));
}

TEST(Unpack, CorrectOrder47) {
  struct S47 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43, arg44 = 44, arg45 = 45, arg46 = 46;
  };
  CheckTupleElementsEqualToIndex<47>(Unpack(S47()));
}

TEST(Unpack, CorrectOrder48) {
  struct S48 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43, arg44 = 44, arg45 = 45, arg46 = 46, arg47 = 47;
  };
  CheckTupleElementsEqualToIndex<48>(Unpack(S48()));
}

TEST(Unpack, CorrectOrder49) {
  struct S49 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43, arg44 = 44, arg45 = 45, arg46 = 46, arg47 = 47, arg48 = 48;
  };
  CheckTupleElementsEqualToIndex<49>(Unpack(S49()));
}

TEST(Unpack, CorrectOrder50) {
  struct S50 {
    int arg0 = 0, arg1 = 1, arg2 = 2, arg3 = 3, arg4 = 4, arg5 = 5, arg6 = 6,
        arg7 = 7, arg8 = 8, arg9 = 9, arg10 = 10, arg11 = 11, arg12 = 12,
        arg13 = 13, arg14 = 14, arg15 = 15, arg16 = 16, arg17 = 17, arg18 = 18,
        arg19 = 19, arg20 = 20, arg21 = 21, arg22 = 22, arg23 = 23, arg24 = 24,
        arg25 = 25, arg26 = 26, arg27 = 27, arg28 = 28, arg29 = 29, arg30 = 30,
        arg31 = 31, arg32 = 32, arg33 = 33, arg34 = 34, arg35 = 35, arg36 = 36,
        arg37 = 37, arg38 = 38, arg39 = 39, arg40 = 40, arg41 = 41, arg42 = 42,
        arg43 = 43, arg44 = 44, arg45 = 45, arg46 = 46, arg47 = 47, arg48 = 48,
        arg49 = 49;
  };
  CheckTupleElementsEqualToIndex<50>(Unpack(S50()));
}

}  // namespace
}  // namespace gtl::internal_aggregate
