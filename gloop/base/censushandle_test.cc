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

#include "gloop/base/censushandle.h"

#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "benchmark/benchmark.h"
#include "gloop/base/censushandle_test_entry.h"
#include "gtest/gtest.h"

namespace {

// Compile time enforce constexpr ctor as we should not accidentally drop that.
// We could SFINAE an `is_constexpr_constructible`, alas...
[[maybe_unused]] constexpr absl::NoDestructor<CensusHandle> handle();

TEST(CensusHandleTest, ReferenceCounts) {
  auto e1 = new TestEntry();
  auto e2 = new TestEntry();
  CensusHandle h1 = TestEntry::Wrap(e1);
  CensusHandle h2 = TestEntry::Wrap(e2);
  EXPECT_EQ(e1->rc(), 1);
  EXPECT_EQ(e2->rc(), 1);
  {
    CensusHandle h(h1);
    EXPECT_EQ(e1->rc(), 2);
    h = h2;
    EXPECT_EQ(e1->rc(), 1);
    EXPECT_EQ(e2->rc(), 2);
  }
  EXPECT_EQ(e1->rc(), 1);
  EXPECT_EQ(e2->rc(), 1);
  {
    CensusHandle h(std::move(h2));
    EXPECT_EQ(e2->rc(), 1);
    h = std::move(h1);
    EXPECT_EQ(e1->rc(), 1);
  }
}

TEST(CensusHandleTest, UnrefNoDelete) {
  auto e = new TestEntry();
  CensusHandle h1 = TestEntry::Wrap(e);
  EXPECT_EQ(e->rc(), 1);
  {
    CensusHandle h2 = h1;
    EXPECT_EQ(e->rc(), 2);
    // We are not at the last reference so we don't do anything.
    EXPECT_FALSE(TestEntry::Get(h2)->UnrefNoDeleteForTest());
    EXPECT_EQ(e->rc(), 1);
    // Set rep to 0 to avoid invoking Unref() again when h2 goes out of scope.
    TestEntry::ResetRep(h2);
  }
  // We are at the last reference so we delete the entry manually at the end.
  EXPECT_TRUE(TestEntry::Get(h1)->UnrefNoDeleteForTest());
  CensusHandle h = std::move(h1);
  TestEntry::ResetRep(h);
  delete e;
}

TEST(CensusHandleTest, Swap) {
  auto e1 = new TestEntry();
  auto e2 = new TestEntry();
  CensusHandle h1 = TestEntry::Wrap(e1);
  CensusHandle h2 = TestEntry::Wrap(e2);
  EXPECT_EQ(TestEntry::Get(h1), e1);
  EXPECT_EQ(TestEntry::Get(h2), e2);

  using std::swap;
  swap(h1, h2);
  EXPECT_EQ(TestEntry::Get(h1), e2);
  EXPECT_EQ(TestEntry::Get(h2), e1);

  h1.Swap(&h2);
  EXPECT_EQ(TestEntry::Get(h1), e1);
  EXPECT_EQ(TestEntry::Get(h2), e2);
}

TEST(CensusHandleTest, Move) {
  auto e = new TestEntry();
  CensusHandle h1 = TestEntry::Wrap(e);
  CensusHandle h2 = std::move(h1);
  EXPECT_EQ(TestEntry::Get(h1), nullptr);  // NOLINT: use-after-move ok
  EXPECT_EQ(TestEntry::Get(h2), e);
  h1 = std::move(h2);
  EXPECT_EQ(TestEntry::Get(h1), e);        // NOLINT: use-after-move ok
  EXPECT_EQ(TestEntry::Get(h2), nullptr);  // NOLINT: use-after-move ok
}

void BM_CensusHandleConstructDestruct(benchmark::State& state) {
  auto e = new TestEntry();
  CensusHandle h = TestEntry::Wrap(e);
  for (auto _ : state) {
    CensusHandle h2(h);
  }
}

BENCHMARK(BM_CensusHandleConstructDestruct);

void BM_CensusHandleCopyAssign(benchmark::State& state) {
  int n = state.range(0);
  std::vector<CensusHandle> f(n);
  for (int i = 0; i < n; i++) {
    f[i] = TestEntry::Wrap(new TestEntry());
  }
  CensusHandle h;
  while (state.KeepRunningBatch(n)) {
    for (int i = 0; i < n; i++) {
      h = f[i];
    }
  }
}

BENCHMARK(BM_CensusHandleCopyAssign)->Range(1 << 5, 1 << 20);

void BM_EntryConstructDestruct(benchmark::State& state) {
  int n = state.range(0);
  std::vector<TestEntry*> f(n);
  while (state.KeepRunningBatch(n)) {
    for (int i = 0; i < n; i++) {
      f[i] = new TestEntry();
    }
    for (int i = 0; i < n; i++) {
      f[i]->UnrefForTest();
    }
  }
}

BENCHMARK(BM_EntryConstructDestruct)->Range(1 << 5, 1 << 20);

void BM_EntryRefUnref(benchmark::State& state) {
  int n = state.range(0);
  std::vector<TestEntry*> f(n);
  while (state.KeepRunningBatch(20 * n)) {
    for (int i = 0; i < n; i++) f[i] = new TestEntry();
    for (int j = 0; j < 19; j++) {
      for (int i = 0; i < n; i++) f[i]->RefForTest();
    }
    for (int j = 0; j < 20; j++) {
      for (int i = 0; i < n; i++) f[i]->UnrefForTest();
    }
  }
}

BENCHMARK(BM_EntryRefUnref)->Range(1 << 5, 1 << 20);

}  // namespace
