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

// A unittest for the PerThread code.

#include "gloop/base/per_thread.h"

#include <stdlib.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>  // NOLINT(build/c++11)

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::IsNull;

static PerThread::Key per_thread[10];
static int destructor_count;

// base of range of values to be put in per-thread locations
static char* value_base;

static void TestThread(int offset) {
  void** my_locations[ABSL_ARRAYSIZE(per_thread)];
  for (int i = 0; i != ABSL_ARRAYSIZE(per_thread); i++) {
    my_locations[i] = PerThread::Data(per_thread[i]);
    EXPECT_THAT(*my_locations[i], IsNull());
    EXPECT_EQ(*my_locations[i], PerThread::GetData(per_thread[i]));
    *my_locations[i] = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(value_base) + offset + i);
    EXPECT_EQ(*my_locations[i], PerThread::GetData(per_thread[i]));
  }
  for (int i = 0; i != ABSL_ARRAYSIZE(per_thread); i++) {
    EXPECT_EQ(my_locations[i], PerThread::Data(per_thread[i]));
    EXPECT_EQ(*my_locations[i],
              reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(value_base) +
                                      offset + i));
    EXPECT_EQ(*my_locations[i], PerThread::GetData(per_thread[i]));
  }
}

static void Destructor(void* v) {
  CHECK_NE(v, nullptr);
  CHECK_GE(reinterpret_cast<char*>(v), value_base) << v;
  CHECK_LT(reinterpret_cast<char*>(v),
           reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(value_base) +
                                   ABSL_ARRAYSIZE(per_thread)))
      << v;
  destructor_count++;
}

// Run a thread, testing its use of the per-thread values
// The values put in the thread's per-thread data are offset by "base".
static void RunThread(int base) {
  value_base = reinterpret_cast<char*>(static_cast<uintptr_t>(base));
  std::thread t(TestThread, 0);
  TestThread(100);  // run TestThread() in parent too
  t.join();
}

TEST(PerThread, Test) {
  // Create several per-thread locations.
  for (int i = 0; i != ABSL_ARRAYSIZE(per_thread); i++) {
    PerThread::Allocate(&per_thread[i], Destructor);
  }

  RunThread(0);

  // destructor will not be called for entry 0 because value is 0
  EXPECT_EQ(destructor_count, ABSL_ARRAYSIZE(per_thread) - 1);

  // zero the values in the main thread
  for (int i = 0; i != ABSL_ARRAYSIZE(per_thread); i++) {
    *PerThread::Data(per_thread[i]) = nullptr;
  }

  // Run the test again.
  RunThread(ABSL_ARRAYSIZE(per_thread));

  // destructor_count should have increased by ABSL_ARRAYSIZE(per_thread)
  EXPECT_EQ(destructor_count, 2 * ABSL_ARRAYSIZE(per_thread) - 1);

  // zero the values in the main thread
  for (int i = 0; i != ABSL_ARRAYSIZE(per_thread); i++) {
    *PerThread::Data(per_thread[i]) = nullptr;
  }
}

#if defined(__APPLE__)
// The original depth of recursion causes a bus error for darwin_x86_64. By
// reducing these constants, I suspect some of the original integrity of the
// test has been lost here (since the code under test is stack sensitive).
static constexpr int kMoreStackFramesThanPageCache = 1100;
static constexpr int kStackFramesInPageCache = 400;
#elif defined(__Fuchsia__)
// Similar to the error on darwin, the Fuchsia emulator test page faults with
// the original recursion depth.
// TODO: Reexamine why this doesn't pass.
static constexpr int kMoreStackFramesThanPageCache = 1000;
static constexpr int kStackFramesInPageCache = 400;
#else
static constexpr int kMoreStackFramesThanPageCache = 2000;
static constexpr int kStackFramesInPageCache = 1000;
#endif

// This test verifies that we see the same per thread data block from both the
// cache and pthread_getspecific in the per-thread destructor, where the
// pthread API normally zeroes out this value.

ABSL_CONST_INIT static PerThread::Key deep_ref_key{PerThread::kInvalid};

struct PerThreadInfo {
  void** address;
  PerThread::Key key;
};

// Recursively checks the integrity of PerThread::GetData() for a range of
// thread stack space that is some multiple of 'count'.
static void CheckKeyInDestructor(int count, const PerThreadInfo* info) {
  CHECK_EQ(PerThread::Data(info->key), info->address);
  if (!count) {
    return;
  }
  CheckKeyInDestructor(count - 1, info);
}

static void KeyReferencingDestructor(void* v) {
  std::unique_ptr<PerThreadInfo> info =
      absl::WrapUnique(reinterpret_cast<PerThreadInfo*>(v));

  // Go beyond what we have stored in the page cache to force us to pull
  // data from the pthread API.
  CheckKeyInDestructor(kMoreStackFramesThanPageCache, info.get());
}

static void TestThread2() {
  PerThread::Allocate(&deep_ref_key, KeyReferencingDestructor);

  // Create a new info object and store the per-thread's address and key in it.
  PerThreadInfo* info = new PerThreadInfo();
  void** var = PerThread::Data(deep_ref_key);
  info->address = var;
  info->key = deep_ref_key.load(std::memory_order_relaxed);

  // Store our info object in the per-thread storage.
  *var = info;

  // Fill the page cache with references to the current thread data.
  CheckKeyInDestructor(kStackFramesInPageCache, info);
}

TEST(PerThread, KeyRefsInDestructor) {
  std::thread t(TestThread2);
  t.join();
}

#if GTEST_GOOGLE3_MODE_
ABSL_CONST_INIT static PerThread::Key benchmark_key{PerThread::kInvalid};

static void BM_PerThreadData(benchmark::State& state) {
  PerThread::Allocate(&benchmark_key, free);

  void* last = nullptr;
  for (auto _ : state) {
    last = PerThread::Data(benchmark_key);
    int x ABSL_ATTRIBUTE_UNUSED = *reinterpret_cast<int*>(last);
  }
  CHECK(state.iterations() <= 0 || last != nullptr);
}
BENCHMARK(BM_PerThreadData)->ThreadPerCpu();
#endif

}  // namespace
