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

#include "gloop/util/gtl/typeid.h"

#include <cstddef>

#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace gtl {

// Need to be able to declare a type in this namespace in another
// translation unit.
namespace TypeId_unittest_namespace {

class TestType {};

size_t FastTypeIdOfTestType();
size_t TypeIdOfTestType();

TEST(TypeId, Simple) {
  EXPECT_EQ(FastTypeId<int>(), FastTypeId<int>());
  EXPECT_NE(FastTypeId<float>(), FastTypeId<double>());

  EXPECT_EQ(TypeId::get<int>(), TypeId::get<int>());
  EXPECT_NE(TypeId::get<float>(), TypeId::get<double>());

  // Note(jyasskin): It would be nice to test that no two distinct
  // types get the same TypeId, but I can't think of a way to do that.
}

TEST(TypeId, OtherTranslationUnit) {
  EXPECT_EQ(FastTypeId<TestType>(), FastTypeIdOfTestType());

  EXPECT_EQ(TypeId::get<TestType>(), TypeIdOfTestType());
}

TEST(TypeId, Size) {
  // These are the only types whose TypeId's are looked up in this
  // program, so they should all get Ids less than 7.
  const size_t kMaxTypeId = 7;
  EXPECT_LT(TypeId::get<int>(), kMaxTypeId);
  EXPECT_LT(TypeId::get<float>(), kMaxTypeId);
  EXPECT_LT(TypeId::get<double>(), kMaxTypeId);
  EXPECT_LT(TypeId::get<TestType>(), kMaxTypeId);
  EXPECT_LT(TypeId::get<const TestType>(), kMaxTypeId);
  EXPECT_LT(TypeId::get<volatile TestType>(), kMaxTypeId);
  EXPECT_LT(TypeId::get<const volatile TestType>(), kMaxTypeId);

  // This is the only use of TestType as an IdSet for TypeIdInSet, so it should
  // return continuous ids starting from 0.
  EXPECT_EQ(TypeIdInSet<TestType>::get<void>(), 0);
  EXPECT_EQ(TypeIdInSet<TestType>::num_ids(), 1);
  EXPECT_EQ(TypeIdInSet<TestType>::get<int>(), 1);
  EXPECT_EQ(TypeIdInSet<TestType>::num_ids(), 2);
  EXPECT_EQ(TypeIdInSet<TestType>::get<bool>(), 2);
  EXPECT_EQ(TypeIdInSet<TestType>::num_ids(), 3);
}

// Giving cv-qualified types a different ID from the base type is a
// policy decision and not essential. If you need them to be the same,
// you can normalize them yourself, or convince us to add a
// convenience function.
TEST(TypeId, ConstVolatile) {
  EXPECT_NE(FastTypeId<TestType>(), FastTypeId<const TestType>());
  EXPECT_NE(FastTypeId<const TestType>(), FastTypeId<volatile TestType>());
  EXPECT_NE(FastTypeId<TestType>(), FastTypeId<const volatile TestType>());
  EXPECT_NE(FastTypeId<volatile TestType>(),
            FastTypeId<const volatile TestType>());

  EXPECT_NE(TypeId::get<TestType>(), TypeId::get<const TestType>());
  EXPECT_NE(TypeId::get<const TestType>(), TypeId::get<volatile TestType>());
  EXPECT_NE(TypeId::get<TestType>(), TypeId::get<const volatile TestType>());
  EXPECT_NE(TypeId::get<volatile TestType>(),
            TypeId::get<const volatile TestType>());

  // Here's how to normalize against cv-ness.
  typedef volatile TestType vTestType;
  EXPECT_EQ(FastTypeId<const volatile TestType>(),
            FastTypeId<const volatile vTestType>());

  EXPECT_EQ(TypeId::get<const volatile TestType>(),
            TypeId::get<const volatile vTestType>());
}

// Benchmarks for comparing TypeId vs FastTypeId
// Run on baal2.mtv (6 X 2661 MHz CPUs); 2012/02/03-18:12:04
// CPU: Intel Westmere with HyperThreading (3 cores) dL1:32KB dL2:256KB
// Benchmark       Time(ns)    CPU(ns) Iterations
// ----------------------------------------------
// BM_TypeId              3          3  269230769
// BM_FastTypeId          0          0 1000000000

static void BM_TypeId(benchmark::State& state) {
  for (auto s : state) {
    TypeId::get<int>();
    TypeId::get<float>();
    TypeId::get<double>();
    TypeId::get<TestType>();
    TypeId::get<const TestType>();
    TypeId::get<volatile TestType>();
    TypeId::get<const volatile TestType>();
  }
}
BENCHMARK(BM_TypeId);

// Note that this benchmark is slightly silly: FastTypeId is simple enough that
// the compiler can inline/discard everything, likely even the loop itself.
static void BM_FastTypeId(benchmark::State& state) {
  for (auto s : state) {
    FastTypeId<int>();
    FastTypeId<float>();
    FastTypeId<double>();
    FastTypeId<TestType>();
    FastTypeId<const TestType>();
    FastTypeId<volatile TestType>();
    FastTypeId<const volatile TestType>();
  }
}
BENCHMARK(BM_FastTypeId);

}  // namespace TypeId_unittest_namespace
}  // namespace gtl
