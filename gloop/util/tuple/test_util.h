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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_TEST_UTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_TEST_UTIL_H_

#include "gtest/gtest.h"

namespace util {
namespace tuple {

class TestValues : public ::testing::Test {
 public:
  template <int N>
  class Value {
   public:
    static const int value = N;
    static const Value instance;

   private:
    Value() {}
    friend class TestValues;
  };

  typedef Value<0> A;
  typedef Value<1> B;
  typedef Value<2> C;
  typedef Value<3> D;

  typedef Value<-4> W;
  typedef Value<-3> X;
  typedef Value<-2> Y;
  typedef Value<-1> Z;

  A a;
  B b;
  C c;
  D d;

  W w;
  X x;
  Y y;
  Z z;
};

template <int N>
const int TestValues::Value<N>::value;

template <int N>
const TestValues::Value<N> TestValues::Value<N>::instance;

template <int N>
bool operator==(TestValues::Value<N> a, TestValues::Value<N> b) {
  return true;
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_TEST_UTIL_H_
