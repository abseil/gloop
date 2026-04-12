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

// Common test code for `ReferenceCounted` implementations.
#ifndef THIRD_PARTY_GLOOP_UTIL_REFCOUNT_TEST_COMMON_H_
#define THIRD_PARTY_GLOOP_UTIL_REFCOUNT_TEST_COMMON_H_

#include <cstddef>
#include <vector>

#include "gloop/gloop_test.h"
#include "gloop/util/refcount/reffed_ptr.h"

namespace refcount {

// `TypeFactory` should define a:
// `template <typename T> ReferenceCountedBaseT`, which is used as base type.
// Note that this type *can*, but does not *have to* be a template.
template <typename TypeFactory>
class ReferenceCountedTest : public testing::Test {};

TYPED_TEST_SUITE_P(ReferenceCountedTest);

// Tests simple creation, Unref() and public observable state.
TYPED_TEST_P(ReferenceCountedTest, Basic) {
  class BasicRefCounted final
      : public TypeParam::template ReferenceCountedBaseT<BasicRefCounted> {};

  BasicRefCounted* rc = new BasicRefCounted();
  EXPECT_TRUE(rc->RefCountIsOne());
  EXPECT_TRUE(rc->Unref());
}

// Tests Ref(), Unref(), reference counts and OnRefCountIsZero / Dtor
// invocations.
TYPED_TEST_P(ReferenceCountedTest, RefUnrefAndRefCount) {
  class TrackingRefCounted final
      : public TypeParam::template ReferenceCountedBaseT<TrackingRefCounted> {
   public:
    using Base =
        typename TypeParam::template ReferenceCountedBaseT<TrackingRefCounted>;

    TrackingRefCounted(int* destructor_counter, int* final_unref_count)
        : destruct_count_(destructor_counter),
          final_unref_count_(final_unref_count) {}

    ~TrackingRefCounted() {  // NOLINT: virtuality is dependent.
      if (destruct_count_) ++*destruct_count_;
    }

    void OnRefCountIsZero() const {  // NOLINT: virtuality is dependent.
      if (final_unref_count_) ++*final_unref_count_;
      Base::OnRefCountIsZero();
    }

    size_t ref_count() const { return Base::ref_count(); }

   private:
    int* const destruct_count_;
    int* const final_unref_count_;
  };

  using ::testing::Eq;
  int destruct_count = 0;
  int finalize_count = 0;
  TrackingRefCounted* rc =
      new TrackingRefCounted(&destruct_count, &finalize_count);
  EXPECT_TRUE(rc->RefCountIsOne());
  EXPECT_THAT(rc->ref_count(), Eq(1));

  rc->Ref();
  EXPECT_FALSE(rc->RefCountIsOne());
  EXPECT_THAT(rc->ref_count(), Eq(2));

  EXPECT_FALSE(rc->Unref());
  EXPECT_TRUE(rc->RefCountIsOne());
  EXPECT_THAT(rc->ref_count(), Eq(1));

  EXPECT_THAT(destruct_count, Eq(0));
  EXPECT_THAT(finalize_count, Eq(0));

  EXPECT_TRUE(rc->Unref());
  EXPECT_THAT(destruct_count, Eq(1));
  EXPECT_THAT(finalize_count, Eq(1));
}

// Tests OnRefCountIsZero() overrides and overrules finalization behavior.
TYPED_TEST_P(ReferenceCountedTest, NineLives) {
  // A class that counts dtor and OnRefCountIsZero invocations, exposes
  // ref_count(), and consumes the first 8 OnRefCountIsZero() calls before
  // calling the default OnRefCountIsZero() on the 9th call.
  class NineLives final
      : public TypeParam::template ReferenceCountedBaseT<NineLives> {
   public:
    using Base = typename TypeParam::template ReferenceCountedBaseT<NineLives>;

    NineLives(int* destructor_counter, int* final_unref_count)
        : destruct_count_(destructor_counter),
          final_unref_count_(final_unref_count) {}

    ~NineLives() {  // NOLINT: virtuality is dependent.
      if (destruct_count_) ++*destruct_count_;
    }

    void OnRefCountIsZero() const {  // NOLINT: virtuality is dependent.
      if (final_unref_count_) ++*final_unref_count_;
      if (--const_cast<NineLives*>(this)->lives_ == 0) {
        Base::OnRefCountIsZero();
      }
    }

    size_t ref_count() const { return Base::ref_count(); }

   private:
    int* const destruct_count_;
    int* const final_unref_count_;
    int lives_{9};
  };

  using ::testing::Eq;
  int destruct_count = 0;
  int finalize_count = 0;
  NineLives* rc = new NineLives(&destruct_count, &finalize_count);
  for (int i = 1; i <= 8; ++i) {
    EXPECT_TRUE(rc->RefCountIsOne());
    EXPECT_THAT(rc->ref_count(), Eq(1));
    EXPECT_TRUE(rc->Unref());
    EXPECT_THAT(rc->ref_count(), Eq(0));
    EXPECT_THAT(destruct_count, Eq(0));
    EXPECT_THAT(finalize_count, Eq(i));
    rc->Ref();  // Raise from the death.
  }
  EXPECT_TRUE(rc->Unref());
  EXPECT_THAT(destruct_count, Eq(1));
  EXPECT_THAT(finalize_count, Eq(9));
}

REGISTER_TYPED_TEST_SUITE_P(ReferenceCountedTest, Basic, RefUnrefAndRefCount,
                            NineLives);

template <typename T>
void BenchmarkCopy(benchmark::State& state) {
  constexpr int kNumCopies = 1000;
  reffed_ptr<T> src(new T());
  for (auto _ : state) {
    std::vector<reffed_ptr<T>> copies;
    for (int i = 0; i < kNumCopies; ++i) {
      copies.push_back(src);
    }
    benchmark::DoNotOptimize(copies);
  }
}

template <typename T>
void BenchmarkCtorDtor(benchmark::State& state) {
  constexpr int kNumPtrs = 1000;
  for (auto _ : state) {
    std::vector<reffed_ptr<T>> ptrs;
    for (int i = 0; i < kNumPtrs; ++i) {
      ptrs.emplace_back(new T());
    }
    benchmark::DoNotOptimize(ptrs);
  }
}

}  // namespace refcount

#endif  // THIRD_PARTY_GLOOP_UTIL_REFCOUNT_TEST_COMMON_H_
