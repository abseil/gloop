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

// Copyright 2013 Google Inc. All Rights Reserved.

#include "gloop/util/tuple/struct.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/util/tuple/accumulate.h"
#include "gloop/util/tuple/compile_string.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/matchers.h"
#include "gloop/util/tuple/test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::is_same;

namespace adapt_struct {

struct Zero {};

TUPLE_ADAPT_STRUCT(Zero);

struct One {
  int a;

  friend TUPLE_ADAPT_STRUCT(One, a);
};

template <class A, class B>
struct Two {
  A a;
  B b;
};

template <class A, class B>
TUPLE_ADAPT_STRUCT((Two<A, B>), a, b);

struct Nineteen {
  TestValues::Value<1> v1;
  TestValues::Value<2> v2;
  TestValues::Value<3> v3;
  TestValues::Value<4> v4;
  TestValues::Value<5> v5;
  TestValues::Value<6> v6;
  TestValues::Value<7> v7;
  TestValues::Value<8> v8;
  TestValues::Value<9> v9;
  TestValues::Value<10> v10;
  TestValues::Value<11> v11;
  TestValues::Value<12> v12;
  TestValues::Value<13> v13;
  TestValues::Value<14> v14;
  TestValues::Value<15> v15;
  TestValues::Value<16> v16;
  TestValues::Value<17> v17;
  TestValues::Value<18> v18;
  TestValues::Value<19> v19;
};

TUPLE_ADAPT_STRUCT(Nineteen, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12,
                   v13, v14, v15, v16, v17, v18, v19);

struct SixtyThree {
  TestValues::Value<1> v1;
  TestValues::Value<2> v2;
  TestValues::Value<3> v3;
  TestValues::Value<4> v4;
  TestValues::Value<5> v5;
  TestValues::Value<6> v6;
  TestValues::Value<7> v7;
  TestValues::Value<8> v8;
  TestValues::Value<9> v9;
  TestValues::Value<10> v10;
  TestValues::Value<11> v11;
  TestValues::Value<12> v12;
  TestValues::Value<13> v13;
  TestValues::Value<14> v14;
  TestValues::Value<15> v15;
  TestValues::Value<16> v16;
  TestValues::Value<17> v17;
  TestValues::Value<18> v18;
  TestValues::Value<19> v19;
  TestValues::Value<20> v20;
  TestValues::Value<21> v21;
  TestValues::Value<22> v22;
  TestValues::Value<23> v23;
  TestValues::Value<24> v24;
  TestValues::Value<25> v25;
  TestValues::Value<26> v26;
  TestValues::Value<27> v27;
  TestValues::Value<28> v28;
  TestValues::Value<29> v29;
  TestValues::Value<30> v30;
  TestValues::Value<31> v31;
  TestValues::Value<32> v32;
  TestValues::Value<33> v33;
  TestValues::Value<34> v34;
  TestValues::Value<35> v35;
  TestValues::Value<36> v36;
  TestValues::Value<37> v37;
  TestValues::Value<38> v38;
  TestValues::Value<39> v39;
  TestValues::Value<40> v40;
  TestValues::Value<41> v41;
  TestValues::Value<42> v42;
  TestValues::Value<43> v43;
  TestValues::Value<44> v44;
  TestValues::Value<45> v45;
  TestValues::Value<46> v46;
  TestValues::Value<47> v47;
  TestValues::Value<48> v48;
  TestValues::Value<49> v49;
  TestValues::Value<50> v50;
  TestValues::Value<51> v51;
  TestValues::Value<52> v52;
  TestValues::Value<53> v53;
  TestValues::Value<54> v54;
  TestValues::Value<55> v55;
  TestValues::Value<56> v56;
  TestValues::Value<57> v57;
  TestValues::Value<58> v58;
  TestValues::Value<59> v59;
  TestValues::Value<60> v60;
  TestValues::Value<61> v61;
  TestValues::Value<62> v62;
  TestValues::Value<63> v63;
};

TUPLE_ADAPT_STRUCT(SixtyThree, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11,
                   v12, v13, v14, v15, v16, v17, v18, v19, v20, v21, v22, v23,
                   v24, v25, v26, v27, v28, v29, v30, v31, v32, v33, v34, v35,
                   v36, v37, v38, v39, v40, v41, v42, v43, v44, v45, v46, v47,
                   v48, v49, v50, v51, v52, v53, v54, v55, v56, v57, v58, v59,
                   v60, v61, v62, v63);

struct NonConstAndConst {
  int n = 0;
  const int cn = 0;
  friend TUPLE_ADAPT_STRUCT(NonConstAndConst, n, cn);
};

class AdaptStruct : public TestValues {};

TEST_F(AdaptStruct, Assemble) {
  EXPECT_TRUE((is_same<assemble<tag<Zero>::type>::type, Zero>::value));
  EXPECT_TRUE((is_same<assemble<tag<One>::type, int>::type, One>::value));
  EXPECT_TRUE((is_same<assemble<tag<Two<int, char>>::type, int, char>::type,
                       Two<int, char>>::value));
  EXPECT_TRUE(
      (is_same<
          assemble<tag<Nineteen>::type, Value<1>, Value<2>, Value<3>, Value<4>,
                   Value<5>, Value<6>, Value<7>, Value<8>, Value<9>, Value<10>,
                   Value<11>, Value<12>, Value<13>, Value<14>, Value<15>,
                   Value<16>, Value<17>, Value<18>, Value<19>>::type,
          Nineteen>::value));
  EXPECT_TRUE(
      (is_same<
          assemble<
              tag<SixtyThree>::type, Value<1>, Value<2>, Value<3>, Value<4>,
              Value<5>, Value<6>, Value<7>, Value<8>, Value<9>, Value<10>,
              Value<11>, Value<12>, Value<13>, Value<14>, Value<15>, Value<16>,
              Value<17>, Value<18>, Value<19>, Value<20>, Value<21>, Value<22>,
              Value<23>, Value<24>, Value<25>, Value<26>, Value<27>, Value<28>,
              Value<29>, Value<30>, Value<31>, Value<32>, Value<33>, Value<34>,
              Value<35>, Value<36>, Value<37>, Value<38>, Value<39>, Value<40>,
              Value<41>, Value<42>, Value<43>, Value<44>, Value<45>, Value<46>,
              Value<47>, Value<48>, Value<49>, Value<50>, Value<51>, Value<52>,
              Value<53>, Value<54>, Value<55>, Value<56>, Value<57>, Value<58>,
              Value<59>, Value<60>, Value<61>, Value<62>, Value<63>>::type,
          SixtyThree>::value));
}

TEST_F(AdaptStruct, Element) {
  EXPECT_TRUE((is_same<element<0, One>::type, int>::value));
  EXPECT_TRUE((is_same<element<0, Two<int, char>>::type, int>::value));
  EXPECT_TRUE((is_same<element<1, Two<int, char>>::type, char>::value));
  EXPECT_TRUE((is_same<element<0, Nineteen>::type, Value<1>>::value));
  EXPECT_TRUE((is_same<element<1, Nineteen>::type, Value<2>>::value));
  EXPECT_TRUE((is_same<element<2, Nineteen>::type, Value<3>>::value));
  EXPECT_TRUE((is_same<element<3, Nineteen>::type, Value<4>>::value));
  EXPECT_TRUE((is_same<element<4, Nineteen>::type, Value<5>>::value));
  EXPECT_TRUE((is_same<element<5, Nineteen>::type, Value<6>>::value));
  EXPECT_TRUE((is_same<element<6, Nineteen>::type, Value<7>>::value));
  EXPECT_TRUE((is_same<element<7, Nineteen>::type, Value<8>>::value));
  EXPECT_TRUE((is_same<element<8, Nineteen>::type, Value<9>>::value));
  EXPECT_TRUE((is_same<element<9, Nineteen>::type, Value<10>>::value));
  EXPECT_TRUE((is_same<element<10, Nineteen>::type, Value<11>>::value));
  EXPECT_TRUE((is_same<element<11, Nineteen>::type, Value<12>>::value));
  EXPECT_TRUE((is_same<element<12, Nineteen>::type, Value<13>>::value));
  EXPECT_TRUE((is_same<element<13, Nineteen>::type, Value<14>>::value));
  EXPECT_TRUE((is_same<element<14, Nineteen>::type, Value<15>>::value));
  EXPECT_TRUE((is_same<element<15, Nineteen>::type, Value<16>>::value));
  EXPECT_TRUE((is_same<element<16, Nineteen>::type, Value<17>>::value));
  EXPECT_TRUE((is_same<element<17, Nineteen>::type, Value<18>>::value));
  EXPECT_TRUE((is_same<element<18, Nineteen>::type, Value<19>>::value));
  EXPECT_TRUE((is_same<element<0, SixtyThree>::type, Value<1>>::value));
  EXPECT_TRUE((is_same<element<1, SixtyThree>::type, Value<2>>::value));
  EXPECT_TRUE((is_same<element<2, SixtyThree>::type, Value<3>>::value));
  EXPECT_TRUE((is_same<element<3, SixtyThree>::type, Value<4>>::value));
  EXPECT_TRUE((is_same<element<4, SixtyThree>::type, Value<5>>::value));
  EXPECT_TRUE((is_same<element<5, SixtyThree>::type, Value<6>>::value));
  EXPECT_TRUE((is_same<element<6, SixtyThree>::type, Value<7>>::value));
  EXPECT_TRUE((is_same<element<7, SixtyThree>::type, Value<8>>::value));
  EXPECT_TRUE((is_same<element<8, SixtyThree>::type, Value<9>>::value));
  EXPECT_TRUE((is_same<element<9, SixtyThree>::type, Value<10>>::value));
  EXPECT_TRUE((is_same<element<10, SixtyThree>::type, Value<11>>::value));
  EXPECT_TRUE((is_same<element<11, SixtyThree>::type, Value<12>>::value));
  EXPECT_TRUE((is_same<element<12, SixtyThree>::type, Value<13>>::value));
  EXPECT_TRUE((is_same<element<13, SixtyThree>::type, Value<14>>::value));
  EXPECT_TRUE((is_same<element<14, SixtyThree>::type, Value<15>>::value));
  EXPECT_TRUE((is_same<element<15, SixtyThree>::type, Value<16>>::value));
  EXPECT_TRUE((is_same<element<16, SixtyThree>::type, Value<17>>::value));
  EXPECT_TRUE((is_same<element<17, SixtyThree>::type, Value<18>>::value));
  EXPECT_TRUE((is_same<element<18, SixtyThree>::type, Value<19>>::value));
  EXPECT_TRUE((is_same<element<19, SixtyThree>::type, Value<20>>::value));
  EXPECT_TRUE((is_same<element<20, SixtyThree>::type, Value<21>>::value));
  EXPECT_TRUE((is_same<element<21, SixtyThree>::type, Value<22>>::value));
  EXPECT_TRUE((is_same<element<22, SixtyThree>::type, Value<23>>::value));
  EXPECT_TRUE((is_same<element<23, SixtyThree>::type, Value<24>>::value));
  EXPECT_TRUE((is_same<element<24, SixtyThree>::type, Value<25>>::value));
  EXPECT_TRUE((is_same<element<25, SixtyThree>::type, Value<26>>::value));
  EXPECT_TRUE((is_same<element<26, SixtyThree>::type, Value<27>>::value));
  EXPECT_TRUE((is_same<element<27, SixtyThree>::type, Value<28>>::value));
  EXPECT_TRUE((is_same<element<28, SixtyThree>::type, Value<29>>::value));
  EXPECT_TRUE((is_same<element<29, SixtyThree>::type, Value<30>>::value));
  EXPECT_TRUE((is_same<element<30, SixtyThree>::type, Value<31>>::value));
  EXPECT_TRUE((is_same<element<31, SixtyThree>::type, Value<32>>::value));
  EXPECT_TRUE((is_same<element<32, SixtyThree>::type, Value<33>>::value));
  EXPECT_TRUE((is_same<element<33, SixtyThree>::type, Value<34>>::value));
  EXPECT_TRUE((is_same<element<34, SixtyThree>::type, Value<35>>::value));
  EXPECT_TRUE((is_same<element<35, SixtyThree>::type, Value<36>>::value));
  EXPECT_TRUE((is_same<element<36, SixtyThree>::type, Value<37>>::value));
  EXPECT_TRUE((is_same<element<37, SixtyThree>::type, Value<38>>::value));
  EXPECT_TRUE((is_same<element<38, SixtyThree>::type, Value<39>>::value));
  EXPECT_TRUE((is_same<element<39, SixtyThree>::type, Value<40>>::value));
  EXPECT_TRUE((is_same<element<40, SixtyThree>::type, Value<41>>::value));
  EXPECT_TRUE((is_same<element<41, SixtyThree>::type, Value<42>>::value));
  EXPECT_TRUE((is_same<element<42, SixtyThree>::type, Value<43>>::value));
  EXPECT_TRUE((is_same<element<43, SixtyThree>::type, Value<44>>::value));
  EXPECT_TRUE((is_same<element<44, SixtyThree>::type, Value<45>>::value));
  EXPECT_TRUE((is_same<element<45, SixtyThree>::type, Value<46>>::value));
  EXPECT_TRUE((is_same<element<46, SixtyThree>::type, Value<47>>::value));
  EXPECT_TRUE((is_same<element<47, SixtyThree>::type, Value<48>>::value));
  EXPECT_TRUE((is_same<element<48, SixtyThree>::type, Value<49>>::value));
  EXPECT_TRUE((is_same<element<49, SixtyThree>::type, Value<50>>::value));
  EXPECT_TRUE((is_same<element<50, SixtyThree>::type, Value<51>>::value));
  EXPECT_TRUE((is_same<element<51, SixtyThree>::type, Value<52>>::value));
  EXPECT_TRUE((is_same<element<52, SixtyThree>::type, Value<53>>::value));
  EXPECT_TRUE((is_same<element<53, SixtyThree>::type, Value<54>>::value));
  EXPECT_TRUE((is_same<element<54, SixtyThree>::type, Value<55>>::value));
  EXPECT_TRUE((is_same<element<55, SixtyThree>::type, Value<56>>::value));
  EXPECT_TRUE((is_same<element<56, SixtyThree>::type, Value<57>>::value));
  EXPECT_TRUE((is_same<element<57, SixtyThree>::type, Value<58>>::value));
  EXPECT_TRUE((is_same<element<58, SixtyThree>::type, Value<59>>::value));
  EXPECT_TRUE((is_same<element<59, SixtyThree>::type, Value<60>>::value));
  EXPECT_TRUE((is_same<element<60, SixtyThree>::type, Value<61>>::value));
  EXPECT_TRUE((is_same<element<61, SixtyThree>::type, Value<62>>::value));
  EXPECT_TRUE((is_same<element<62, SixtyThree>::type, Value<63>>::value));
}

TEST_F(AdaptStruct, Size) {
  EXPECT_EQ(0, size<Zero>::value);
  EXPECT_EQ(1, size<One>::value);
  EXPECT_EQ(2, (size<Two<int, char>>::value));
  EXPECT_EQ(19, size<Nineteen>::value);
  EXPECT_EQ(63, size<SixtyThree>::value);
}

TEST_F(AdaptStruct, Get) {
  One one;
  Two<int, char> two;
  Nineteen nineteen = {
      Value<1>::instance,  Value<2>::instance,  Value<3>::instance,
      Value<4>::instance,  Value<5>::instance,  Value<6>::instance,
      Value<7>::instance,  Value<8>::instance,  Value<9>::instance,
      Value<10>::instance, Value<11>::instance, Value<12>::instance,
      Value<13>::instance, Value<14>::instance, Value<15>::instance,
      Value<16>::instance, Value<17>::instance, Value<18>::instance,
      Value<19>::instance};

  // Assignment to fields.
  get<0>(one) = 42;
  get<0>(two) = 42;
  get<1>(two) = 'A';
  get<0>(nineteen) = Value<1>::instance;
  get<1>(nineteen) = Value<2>::instance;
  get<2>(nineteen) = Value<3>::instance;
  get<3>(nineteen) = Value<4>::instance;
  get<4>(nineteen) = Value<5>::instance;
  get<5>(nineteen) = Value<6>::instance;
  get<6>(nineteen) = Value<7>::instance;
  get<7>(nineteen) = Value<8>::instance;
  get<8>(nineteen) = Value<9>::instance;
  get<9>(nineteen) = Value<10>::instance;
  get<10>(nineteen) = Value<11>::instance;
  get<11>(nineteen) = Value<12>::instance;
  get<12>(nineteen) = Value<13>::instance;
  get<13>(nineteen) = Value<14>::instance;
  get<14>(nineteen) = Value<15>::instance;
  get<15>(nineteen) = Value<16>::instance;
  get<16>(nineteen) = Value<17>::instance;
  get<17>(nineteen) = Value<18>::instance;
  get<18>(nineteen) = Value<19>::instance;

  // Non-const getter.
  EXPECT_EQ(42, get<0>(one));
  EXPECT_EQ(42, get<0>(two));
  EXPECT_EQ('A', get<1>(two));
  EXPECT_EQ(Value<1>::instance, get<0>(nineteen));
  EXPECT_EQ(Value<2>::instance, get<1>(nineteen));
  EXPECT_EQ(Value<3>::instance, get<2>(nineteen));
  EXPECT_EQ(Value<4>::instance, get<3>(nineteen));
  EXPECT_EQ(Value<5>::instance, get<4>(nineteen));
  EXPECT_EQ(Value<6>::instance, get<5>(nineteen));
  EXPECT_EQ(Value<7>::instance, get<6>(nineteen));
  EXPECT_EQ(Value<8>::instance, get<7>(nineteen));
  EXPECT_EQ(Value<9>::instance, get<8>(nineteen));
  EXPECT_EQ(Value<10>::instance, get<9>(nineteen));
  EXPECT_EQ(Value<11>::instance, get<10>(nineteen));
  EXPECT_EQ(Value<12>::instance, get<11>(nineteen));
  EXPECT_EQ(Value<13>::instance, get<12>(nineteen));
  EXPECT_EQ(Value<14>::instance, get<13>(nineteen));
  EXPECT_EQ(Value<15>::instance, get<14>(nineteen));
  EXPECT_EQ(Value<16>::instance, get<15>(nineteen));
  EXPECT_EQ(Value<17>::instance, get<16>(nineteen));
  EXPECT_EQ(Value<18>::instance, get<17>(nineteen));
  EXPECT_EQ(Value<19>::instance, get<18>(nineteen));

  // Const getter.
  const One& c_one = one;
  const Two<int, char>& c_two = two;
  const Nineteen& c_nineteen = nineteen;

  EXPECT_EQ(42, get<0>(c_one));
  EXPECT_EQ(42, get<0>(c_two));
  EXPECT_EQ('A', get<1>(c_two));
  EXPECT_EQ(Value<1>::instance, get<0>(c_nineteen));
  EXPECT_EQ(Value<2>::instance, get<1>(c_nineteen));
  EXPECT_EQ(Value<3>::instance, get<2>(c_nineteen));
  EXPECT_EQ(Value<4>::instance, get<3>(c_nineteen));
  EXPECT_EQ(Value<5>::instance, get<4>(c_nineteen));
  EXPECT_EQ(Value<6>::instance, get<5>(c_nineteen));
  EXPECT_EQ(Value<7>::instance, get<6>(c_nineteen));
  EXPECT_EQ(Value<8>::instance, get<7>(c_nineteen));
  EXPECT_EQ(Value<9>::instance, get<8>(c_nineteen));
  EXPECT_EQ(Value<10>::instance, get<9>(c_nineteen));
  EXPECT_EQ(Value<11>::instance, get<10>(c_nineteen));
  EXPECT_EQ(Value<12>::instance, get<11>(c_nineteen));
  EXPECT_EQ(Value<13>::instance, get<12>(c_nineteen));
  EXPECT_EQ(Value<14>::instance, get<13>(c_nineteen));
  EXPECT_EQ(Value<15>::instance, get<14>(c_nineteen));
  EXPECT_EQ(Value<16>::instance, get<15>(c_nineteen));
  EXPECT_EQ(Value<17>::instance, get<16>(c_nineteen));
  EXPECT_EQ(Value<18>::instance, get<17>(c_nineteen));
  EXPECT_EQ(Value<19>::instance, get<18>(c_nineteen));
}

template <class T, class U>
bool SameType(U&&) {
  return ::std::is_same<T, U&&>();
}

TEST_F(AdaptStruct, GetField) {
  NonConstAndConst s;
  s.n = 17;
  EXPECT_EQ(get_field(TUPLE_FIELD(n), s), 17);
  EXPECT_EQ(get_field(TUPLE_FIELD(cn), s), 0);
  auto s2 = Build(NonConstAndConst{}, TUPLE_FIELD(n), 16);
  EXPECT_EQ(get_field(TUPLE_FIELD(n), s2), 16);
  EXPECT_EQ(get_field(TUPLE_FIELD(cn), s2), 0);
  auto s3 = Build(s2, TUPLE_FIELD(n), 17);
  EXPECT_EQ(get_field(TUPLE_FIELD(n), s2), 16);
  EXPECT_EQ(get_field(TUPLE_FIELD(cn), s2), 0);
  EXPECT_EQ(get_field(TUPLE_FIELD(n), s3), 17);
}

TEST_F(AdaptStruct, GetType) {
  NonConstAndConst s = {};
  const NonConstAndConst cs = {};

  EXPECT_TRUE(SameType<int&>(get<int>(s)));
  EXPECT_TRUE(SameType<const int&>(get<const int>(s)));

  EXPECT_TRUE(SameType<const int&>(get<int>(cs)));
  EXPECT_TRUE(SameType<const int&>(get<const int>(cs)));

  EXPECT_TRUE(SameType<int&&>(get<int>(::std::move(s))));
  EXPECT_TRUE(SameType<const int&&>(get<const int>(::std::move(s))));

  EXPECT_TRUE(SameType<const int&&>(get<int>(::std::move(cs))));
  EXPECT_TRUE(SameType<const int&&>(get<const int>(::std::move(cs))));
}

TEST_F(AdaptStruct, Name) {
  EXPECT_STREQ("a", (name<0, One>()));
  static_assert(absl::string_view(name<0, One>()) == "a");
  EXPECT_STREQ("a", (name<0, Two<int, char>>()));
  static_assert(absl::string_view(name<0, Two<int, char>>()) == "a");
  EXPECT_STREQ("b", (name<1, Two<int, char>>()));
  static_assert(absl::string_view(name<1, Two<int, char>>()) == "b");
  EXPECT_STREQ("v1", (name<0, Nineteen>()));
  EXPECT_STREQ("v2", (name<1, Nineteen>()));
  EXPECT_STREQ("v3", (name<2, Nineteen>()));
  EXPECT_STREQ("v4", (name<3, Nineteen>()));
  EXPECT_STREQ("v5", (name<4, Nineteen>()));
  EXPECT_STREQ("v6", (name<5, Nineteen>()));
  EXPECT_STREQ("v7", (name<6, Nineteen>()));
  EXPECT_STREQ("v8", (name<7, Nineteen>()));
  EXPECT_STREQ("v9", (name<8, Nineteen>()));
  EXPECT_STREQ("v10", (name<9, Nineteen>()));
  EXPECT_STREQ("v11", (name<10, Nineteen>()));
  EXPECT_STREQ("v12", (name<11, Nineteen>()));
  EXPECT_STREQ("v13", (name<12, Nineteen>()));
  EXPECT_STREQ("v14", (name<13, Nineteen>()));
  EXPECT_STREQ("v15", (name<14, Nineteen>()));
  EXPECT_STREQ("v16", (name<15, Nineteen>()));
  EXPECT_STREQ("v17", (name<16, Nineteen>()));
  EXPECT_STREQ("v18", (name<17, Nineteen>()));
  EXPECT_STREQ("v19", (name<18, Nineteen>()));
  EXPECT_STREQ("v1", (name<0, SixtyThree>()));
  EXPECT_STREQ("v2", (name<1, SixtyThree>()));
  EXPECT_STREQ("v3", (name<2, SixtyThree>()));
  EXPECT_STREQ("v4", (name<3, SixtyThree>()));
  EXPECT_STREQ("v5", (name<4, SixtyThree>()));
  EXPECT_STREQ("v6", (name<5, SixtyThree>()));
  EXPECT_STREQ("v7", (name<6, SixtyThree>()));
  EXPECT_STREQ("v8", (name<7, SixtyThree>()));
  EXPECT_STREQ("v9", (name<8, SixtyThree>()));
  EXPECT_STREQ("v10", (name<9, SixtyThree>()));
  EXPECT_STREQ("v11", (name<10, SixtyThree>()));
  EXPECT_STREQ("v12", (name<11, SixtyThree>()));
  EXPECT_STREQ("v13", (name<12, SixtyThree>()));
  EXPECT_STREQ("v14", (name<13, SixtyThree>()));
  EXPECT_STREQ("v15", (name<14, SixtyThree>()));
  EXPECT_STREQ("v16", (name<15, SixtyThree>()));
  EXPECT_STREQ("v17", (name<16, SixtyThree>()));
  EXPECT_STREQ("v18", (name<17, SixtyThree>()));
  EXPECT_STREQ("v19", (name<18, SixtyThree>()));
  EXPECT_STREQ("v20", (name<19, SixtyThree>()));
  EXPECT_STREQ("v21", (name<20, SixtyThree>()));
  EXPECT_STREQ("v22", (name<21, SixtyThree>()));
  EXPECT_STREQ("v23", (name<22, SixtyThree>()));
  EXPECT_STREQ("v24", (name<23, SixtyThree>()));
  EXPECT_STREQ("v25", (name<24, SixtyThree>()));
  EXPECT_STREQ("v26", (name<25, SixtyThree>()));
  EXPECT_STREQ("v27", (name<26, SixtyThree>()));
  EXPECT_STREQ("v28", (name<27, SixtyThree>()));
  EXPECT_STREQ("v29", (name<28, SixtyThree>()));
  EXPECT_STREQ("v30", (name<29, SixtyThree>()));
  EXPECT_STREQ("v31", (name<30, SixtyThree>()));
  EXPECT_STREQ("v32", (name<31, SixtyThree>()));
  EXPECT_STREQ("v33", (name<32, SixtyThree>()));
  EXPECT_STREQ("v34", (name<33, SixtyThree>()));
  EXPECT_STREQ("v35", (name<34, SixtyThree>()));
  EXPECT_STREQ("v36", (name<35, SixtyThree>()));
  EXPECT_STREQ("v37", (name<36, SixtyThree>()));
  EXPECT_STREQ("v38", (name<37, SixtyThree>()));
  EXPECT_STREQ("v39", (name<38, SixtyThree>()));
  EXPECT_STREQ("v40", (name<39, SixtyThree>()));
  EXPECT_STREQ("v41", (name<40, SixtyThree>()));
  EXPECT_STREQ("v42", (name<41, SixtyThree>()));
  EXPECT_STREQ("v43", (name<42, SixtyThree>()));
  EXPECT_STREQ("v44", (name<43, SixtyThree>()));
  EXPECT_STREQ("v45", (name<44, SixtyThree>()));
  EXPECT_STREQ("v46", (name<45, SixtyThree>()));
  EXPECT_STREQ("v47", (name<46, SixtyThree>()));
  EXPECT_STREQ("v48", (name<47, SixtyThree>()));
  EXPECT_STREQ("v49", (name<48, SixtyThree>()));
  EXPECT_STREQ("v50", (name<49, SixtyThree>()));
  EXPECT_STREQ("v51", (name<50, SixtyThree>()));
  EXPECT_STREQ("v52", (name<51, SixtyThree>()));
  EXPECT_STREQ("v53", (name<52, SixtyThree>()));
  EXPECT_STREQ("v54", (name<53, SixtyThree>()));
  EXPECT_STREQ("v55", (name<54, SixtyThree>()));
  EXPECT_STREQ("v56", (name<55, SixtyThree>()));
  EXPECT_STREQ("v57", (name<56, SixtyThree>()));
  EXPECT_STREQ("v58", (name<57, SixtyThree>()));
  EXPECT_STREQ("v59", (name<58, SixtyThree>()));
  EXPECT_STREQ("v60", (name<59, SixtyThree>()));
  EXPECT_STREQ("v61", (name<60, SixtyThree>()));
  EXPECT_STREQ("v62", (name<61, SixtyThree>()));
  EXPECT_STREQ("v63", (name<62, SixtyThree>()));
}

TEST_F(AdaptStruct, HasAllElements) {
  EXPECT_TRUE(has_all_elements<Zero>::value);
  EXPECT_TRUE(has_all_elements<One>::value);
  EXPECT_TRUE((has_all_elements<Two<int, char>>::value));
  EXPECT_TRUE(has_all_elements<Nineteen>::value);
  EXPECT_TRUE(has_all_elements<SixtyThree>::value);
}

}  // namespace adapt_struct

namespace define_op {

struct InClass {
  int a;

  friend TUPLE_ADAPT_STRUCT(InClass, a);

  friend TUPLE_DEFINE_OP(InClass, lt);
  friend TUPLE_DEFINE_OP(InClass, gt);
  friend TUPLE_DEFINE_OP(InClass, le);
  friend TUPLE_DEFINE_OP(InClass, ge);
  friend TUPLE_DEFINE_OP(InClass, eq);
  friend TUPLE_DEFINE_OP(InClass, ne);
  friend TUPLE_DEFINE_OP(InClass, ostream);
  friend TUPLE_DEFINE_OP(InClass, absl_format);
  friend TUPLE_DEFINE_OP(InClass, swap);
};
// hash cannot be preceded by the friend keyword.
TUPLE_DEFINE_OP(InClass, absl_hash);

struct OutOfClass {
  int a;

  friend TUPLE_ADAPT_STRUCT(OutOfClass, a);
};

TUPLE_DEFINE_OP(OutOfClass, lt);
TUPLE_DEFINE_OP(OutOfClass, gt);
TUPLE_DEFINE_OP(OutOfClass, le);
TUPLE_DEFINE_OP(OutOfClass, ge);
TUPLE_DEFINE_OP(OutOfClass, eq);
TUPLE_DEFINE_OP(OutOfClass, ne);
TUPLE_DEFINE_OP(OutOfClass, ostream);
TUPLE_DEFINE_OP(OutOfClass, swap);
TUPLE_DEFINE_OP(OutOfClass, absl_hash);
TUPLE_DEFINE_OP(OutOfClass, absl_format);

struct Rel {
  int a;

  friend TUPLE_ADAPT_STRUCT(Rel, a);
  // Note: 'rel' can only be used in the struct scope.
  friend TUPLE_DEFINE_OP(Rel, rel);
  // Add ostream, absl_format, swap, and absl_hash as well to simplify testing.
  friend TUPLE_DEFINE_OP(Rel, ostream);
  friend TUPLE_DEFINE_OP(Rel, absl_format);
  friend TUPLE_DEFINE_OP(Rel, swap);
};
// absl_hash cannot be preceded by the friend keyword.
TUPLE_DEFINE_OP(Rel, absl_hash);

template <class T>
void DoTest() {
  T one = {1};
  T two = {2};
  EXPECT_FALSE(one < one);
  EXPECT_TRUE(one < two);
  EXPECT_FALSE(two < one);
  EXPECT_FALSE(one > one);
  EXPECT_FALSE(one > two);
  EXPECT_TRUE(two > one);
  EXPECT_TRUE(one <= one);
  EXPECT_TRUE(one <= two);
  EXPECT_FALSE(two <= one);
  EXPECT_TRUE(one >= one);
  EXPECT_FALSE(one >= two);
  EXPECT_TRUE(two >= one);
  EXPECT_TRUE(one == one);
  EXPECT_FALSE(one == two);
  EXPECT_FALSE(two == one);
  EXPECT_FALSE(one != one);
  EXPECT_TRUE(one != two);
  EXPECT_TRUE(two != one);
  ::std::ostringstream strm;
  strm << one;
  EXPECT_EQ("{a = 1}", strm.str());
  EXPECT_EQ("{a = 1}", absl::StrFormat("%s", one));
  swap(one, two);
  EXPECT_EQ(1, two.a);
  EXPECT_EQ(2, one.a);

  {  // Testing hashing with default hashing.
    absl::flat_hash_set<T> hash_set;
    EXPECT_TRUE(hash_set.insert(one).second);
    EXPECT_EQ(1, hash_set.count(one));
    EXPECT_EQ(0, hash_set.count(two));
  }
}

TEST(DefineOp, InClass) { DoTest<InClass>(); }

TEST(DefineOp, OutOfClass) { DoTest<OutOfClass>(); }

TEST(DefineOp, Rel) { DoTest<Rel>(); }

template <class T, class U>
struct Pair {
  friend TUPLE_ADAPT_STRUCT(Pair);
};

template <class T, class U>
TUPLE_DEFINE_OP((Pair<T, U>), lt);

}  // namespace define_op

namespace define_struct {

struct Zero {
  TUPLE_DEFINE_STRUCT(Zero, ());
};

struct One {
  TUPLE_DEFINE_STRUCT(One, (), (int, a));
};

struct Two {
  TUPLE_DEFINE_STRUCT(Two, (), (int, a), (std::string, b));
};

struct PairField {
  TUPLE_DEFINE_STRUCT(PairField, (), ((const std::pair<int, int>), a));
};

template <class T>
struct TemplatePairField {
  TUPLE_DEFINE_STRUCT(TemplatePairField, (), ((const std::pair<T, T>), a));
};

struct WithInitializer {
  TUPLE_DEFINE_STRUCT(WithInitializer, (), (int, a, 42));
};

struct OnlyOps {
  TUPLE_DEFINE_STRUCT(OnlyOps, (rel, ostream, absl_hash));
};

struct OpsAndFields {
  TUPLE_DEFINE_STRUCT(OpsAndFields, (rel, ostream, absl_hash), (int, a));
};

struct ZeroCtor {
  TUPLE_DEFINE_STRUCT(ZeroCtor, (ctor));
};

struct OneCtor {
  TUPLE_DEFINE_STRUCT(OneCtor, (ctor), (int, a));
};

struct TwoCtor {
  TUPLE_DEFINE_STRUCT(TwoCtor, (ctor),
                      (const ::std::unique_ptr<int>, a, nullptr),
                      ((const std::pair<int, int>), b));
};

TEST(DefineStruct, Zero) {
  Zero obj = {};
  (void)obj;
  EXPECT_EQ(0, size<Zero>::value);
}

TEST(DefineStruct, One) {
  One obj = {42};
  get<0>(obj) = 24;
  EXPECT_EQ(24, obj.a);
  EXPECT_EQ(1, size<One>::value);
  EXPECT_TRUE((is_same<decltype(One::a), int>::value));
}

TEST(DefineStruct, Two) {
  Two obj = {42, "hello"};
  get<0>(obj) = 24;
  get<1>(obj) = "bye";
  EXPECT_EQ(24, obj.a);
  EXPECT_EQ("bye", obj.b);
  EXPECT_EQ(2, size<Two>::value);
  EXPECT_TRUE((is_same<decltype(Two::a), int>::value));
  EXPECT_TRUE((is_same<decltype(Two::b), std::string>::value));
}

TEST(DefineStruct, PairField) {
  EXPECT_TRUE(
      (is_same<decltype(PairField::a), const std::pair<int, int>>::value));
}

TEST(DefineStruct, TemplatePairField) {
  EXPECT_TRUE((is_same<decltype(TemplatePairField<int>::a),
                       const std::pair<int, int>>::value));
}

TEST(DefineStruct, WithInitializer) {
  WithInitializer obj;
  EXPECT_EQ(42, obj.a);
}

TEST(DefineStruct, OnlyOps) {
  OnlyOps obj = {};
  EXPECT_EQ(obj, obj);
  absl::flat_hash_set<OnlyOps> hash_set;
  EXPECT_TRUE(hash_set.insert(obj).second);
}

TEST(DefineStruct, OpsAndFields) {
  OpsAndFields obj = {42};
  EXPECT_EQ(42, obj.a);
  EXPECT_EQ(obj, obj);

  absl::flat_hash_set<OpsAndFields> hash_set;
  OpsAndFields missing_obj = {5 * 7};
  EXPECT_TRUE(hash_set.insert(obj).second);
  EXPECT_EQ(1, hash_set.count(obj));
  EXPECT_EQ(0, hash_set.count(missing_obj));
}

TEST(DefineStruct, ZeroCtor) {
  // No warning because there is a user-defined ctor.
  ZeroCtor obj;
  // Legit because there is a user-defined ctor.
  const ZeroCtor cobj;
}

TEST(DefineStruct, OneCtor) {
  static_assert(!std::is_convertible<int, OneCtor>::value,
                "Constructor should be explicit");
  {
    OneCtor obj(42);
    EXPECT_EQ(42, obj.a);
  }
  {
    int n = 42;
    OneCtor obj(n);
    EXPECT_EQ(42, obj.a);
  }
}

TEST(DefineStruct, TwoCtor) {
  TwoCtor obj = {std::make_unique<int>(42), {1, 2}};
  EXPECT_THAT(obj.a, ::testing::Pointee(42));
  EXPECT_THAT(obj.b, ::testing::Pair(1, 2));
}

namespace test_examples {
struct Person {
  TUPLE_DEFINE_STRUCT(Person, (eq, ostream), (std::string, name),
                      (int, birth_year));
};

Person Author(absl::string_view book);
Person Author(absl::string_view book) {
  if (book == "The C++ Programming Language") {
    return Person{"Bjarne Stroustrup", 1950};
  }
  return Person{"", 0};
}

TEST(CppPLTest, CorrectAuthor) {
  EXPECT_EQ((Person{"Bjarne Stroustrup", 1950}),
            Author("The C++ Programming Language"));
}

using ::testing::Lt;
using ::util::tuple::testing::Tuple;

TEST(CppPLTest, BornBeforeCpp98) {
  EXPECT_THAT(Author("The C++ Programming Language"),
              Tuple("Bjarne Stroustrup", Lt(1998)));
}
}  // namespace test_examples

}  // namespace define_struct

namespace internal_string {

TEST(InternalString, MaxString) {
  // Verify that there is no typo in the boilerplate of TUPLE_INTERNAL_STRING's
  // definition.
  using S = decltype(TUPLE_COMPILE_STRING(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZAB"));
  EXPECT_STREQ(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "AB",
      S::value);
}

}  // namespace internal_string

namespace base_and_derived {

struct A {
  TUPLE_DEFINE_STRUCT(A, (), (int, x));
};

struct B : A {};

struct C : A {
  double y;
};

TUPLE_ADAPT_STRUCT(C, x, y);

TEST(BaseAndDerived, InheritTupleIntrinsics) {
  static_assert(::std::is_same<element<0, B>::type, int>(), "");
  B b;
  get<0>(b) = 42;
  EXPECT_EQ(42, b.x);
}

TEST(BaseAndDerived, OverrideTupleIntrinsics) {
  static_assert(::std::is_same<element<0, C>::type, int>(), "");
  static_assert(::std::is_same<element<1, C>::type, double>(), "");
  C c;
  get<0>(c) = 42;
  get<1>(c) = 1.5;
  EXPECT_EQ(42, c.x);
  EXPECT_EQ(1.5, c.y);
}

struct D {
  TUPLE_DEFINE_STRUCT(D, (), (std::vector<int>, x, {1, 2}));
};

TEST(ComplexInit, Works) {
  D d;
  EXPECT_EQ(d.x[0], 1);
}

}  // namespace base_and_derived

namespace constexpr_accumulate {

struct A {
  TUPLE_DEFINE_STRUCT(A, (),          //
                      (uint16_t, a),  //
                      // 2 bytes of padding.
                      (uint32_t, b));
};

TEST(ConstexprAccumulate, CountPadding) {
  // Binary operation that sums the size of a struct fields.
  auto sum_sizes = [](size_t accumulator, const auto& field) {
    return accumulator + sizeof(field);
  };
  EXPECT_EQ(accumulate(sum_sizes, A(), size_t(0)),
            sizeof(uint16_t) + sizeof(uint32_t));

  // Make sure accumulate can yield a compile-time consttant.
  static_assert(sizeof(A) - accumulate(sum_sizes, A(), size_t(0)) == 2);
}

}  // namespace constexpr_accumulate

}  // namespace
}  // namespace tuple
}  // namespace util
