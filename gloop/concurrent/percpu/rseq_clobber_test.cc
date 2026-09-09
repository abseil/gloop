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

#include "absl/time/clock.h"
#ifdef __linux__

#include <sched.h>

#include <cstddef>
#include <cstdint>
#include <thread>  // NOLINT
#include <vector>

#include "absl/time/time.h"
#include "gloop/concurrent/percpu/counting_mutex.h"
#include "gtest/gtest.h"

namespace concurrent {
namespace {

#if defined(__x86_64__) || defined(__aarch64__)

// RSEQ_CLOBBER_TEST_DEFINE_VAR and RSEQ_CLOBBER_TEST_CHECK_VAR are used to
// declare a bunch of local variables to have maximum register pressure / use.
// Simplified the code looks as follows:
//
//   int64_t x1 = 1;
//   int64_t x2 = 2;
//   ...
//   int64_t xn = n;
//
//   FunctionPotentiallyGlobberingRegisters();
//
//   if (x1 != 1) goto fail;
//   if (x2 != 2) goto fail;
//   ...
//   if (xn != n) goto fail;
//
// We define the var using an `asm volatile ("..." ::: "memory")` block:
// The `volatile` and "memory" clobber both make the compiler blind to the
// actual assembly code preventing it from optimizing the code and prevent the
// code from being reordered, causing maximum register pressure on the code
// block: the compiler will always strive to minimize spills.
//
// If the function / inlined function code under test clobbers registers it
// should not be clobbering, then this should result in direct failures, as the
// declared vars will most likely be actively bound to these registers.
#if defined(__x86_64__)
#define RSEQ_CLOBBER_TEST_DEFINE_VAR(n) \
  int64_t x##n;                         \
  asm volatile("mov %[c], %[r]" : [r] "=r"(x##n) : [c] "n"(n) : "memory")
#elif defined(__aarch64__)
#define RSEQ_CLOBBER_TEST_DEFINE_VAR(n) \
  int64_t x##n;                         \
  asm volatile("mov %[r], %[c]" : [r] "=r"(x##n) : [c] "n"(n) : "memory")
#endif

#define RSEQ_CLOBBER_TEST_CHECK_VAR(n) \
  if (x##n != n) goto fail

#define RSEQ_CLOBBER_TEST_DEFINE_VARS() \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(1);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(2);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(3);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(4);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(5);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(6);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(7);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(8);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(9);      \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(10);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(11);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(12);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(13);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(14);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(15);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(16);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(17);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(18);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(19);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(20);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(21);     \
  RSEQ_CLOBBER_TEST_DEFINE_VAR(22);

#define RSEQ_CLOBBER_TEST_CHECK_VARS() \
  RSEQ_CLOBBER_TEST_CHECK_VAR(1);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(2);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(3);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(4);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(5);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(6);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(7);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(8);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(9);      \
  RSEQ_CLOBBER_TEST_CHECK_VAR(10);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(11);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(12);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(13);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(14);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(15);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(16);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(17);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(18);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(19);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(20);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(21);     \
  RSEQ_CLOBBER_TEST_CHECK_VAR(22);

TEST(ClobberRegisterTest, DeclareLargeTextSections) {
  // Create two text sections of 128 MB in size.
  // We can then sandwich .text.unlikely containing the RSEQ restart /
  // trampoline sequence in between these sections, which will force the linker
  // to inject a thunk that may clobber x16 or x17 registers according to the
  // ARMv8 ABI. We add the "R" attribute to make sure the linker does not
  // garbage collect these sections.
  constexpr size_t kMaxArmv8BranchDistance = 128 << 20;  // 128 MB
  asm(".pushsection .text_large1, \"axR\"\n"
      ".space %c[space]\n"
      ".popsection\n"
      ".pushsection .text_large2, \"axR\"\n"
      ".space %c[space]\n"
      ".popsection\n"
      :
      : [space] "n"(kMaxArmv8BranchDistance));
}

TEST(ClobberRegisterTest, CountingMutex) {
  CountingMutex mutex;
  constexpr int kNumThreads = 32;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&mutex] {
      absl::Time end_time = absl::Now() + absl::Seconds(2);
      while (absl::Now() < end_time) {
        RSEQ_CLOBBER_TEST_DEFINE_VARS();

        mutex.lock_shared();
        mutex.unlock_shared();

        RSEQ_CLOBBER_TEST_CHECK_VARS();
        continue;
      fail:
        FAIL() << "Some register got clobbered";
        return;
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(ClobberRegisterTest, NewDelete) {
  constexpr int kNumThreads = 32;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([] {
      absl::Time end_time = absl::Now() + absl::Seconds(2);
      while (absl::Now() < end_time) {
        void* p;
        {
          RSEQ_CLOBBER_TEST_DEFINE_VARS();
          p = ::operator new(1024);
          RSEQ_CLOBBER_TEST_CHECK_VARS();
        }
        // Leak `p`
        asm volatile("" : "+r"(p));
        {
          RSEQ_CLOBBER_TEST_DEFINE_VARS();
#if defined(__cpp_sized_deallocation)
          ::operator delete(p, 1024);
#else
          ::operator delete(p);
#endif
          RSEQ_CLOBBER_TEST_CHECK_VARS();
        }
        continue;
      fail:
        FAIL() << "Some register got clobbered";
        return;
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

#endif

}  // namespace
}  // namespace concurrent

#endif  // __linux__
