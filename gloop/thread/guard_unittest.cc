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

// Unit test for thread guards.
//
// Surprisingly, I do this without death tests in the guard area.
// It is awkward to trap SIGSEGV in a safe fashion so instead I
// make system calls in the guard area and look for EFAULT returns.
//
// This relies on the stack growing down so watch out if you use an
// HPPA or something.  See stack_incr in GuardTestThread::DoStackTest.

#include <limits.h>
#include <pthread.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/base/address_is_readable.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gtest/gtest.h"

namespace {

struct GuardSpec {
  absl::string_view name;
  std::function<size_t(size_t, size_t)> calculator;
};

const GuardSpec kOneByte = {"OneByte",
                            [](size_t, size_t) -> size_t { return 1; }};

const GuardSpec kOnePage = {
    "OnePage", [](size_t, size_t pagesize) -> size_t { return pagesize; }};

const GuardSpec kHalfStack = {"HalfStack",
                              [](size_t mult, size_t pagesize) -> size_t {
                                // Would fail with NPTL if Thread didn't
                                // compensate.
                                return (mult / 2) * pagesize;
                              }};

// Thread which tests the size of its stack.
class GuardTestThread : public Thread {
 public:
  GuardTestThread(size_t stack_size, size_t guard_size)
      : Thread(thread::Options()
                   .set_stack_size(stack_size)
                   .set_guard_size(guard_size)
                   .set_joinable(true),
               "guardtest") {}

  void Run() override { DoStackTest(); }

  size_t detected_stack_size() const { return detected_stack_size_; }
  size_t detected_guard_size() const { return detected_guard_size_; }

 private:
  void DoStackTest() {
    const uintptr_t stack_addr =
        reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

    // Change this to 1 if on a machine where stack grows up.
    constexpr intptr_t stack_incr = -1;

    /* There may an arbitrary amount of extra stack, but in these tests
       we assume that there will always be at least a bit of a guard. */
    intptr_t stack_size = 0;
    while (base::AddressIsReadable(
        reinterpret_cast<const void*>(stack_addr + stack_size))) {
      stack_size += stack_incr;
    }

    intptr_t guard_size = stack_incr;
    const intptr_t req_guard_size =
        static_cast<intptr_t>(options().guard_size());

    while (!base::AddressIsReadable(reinterpret_cast<const void*>(
               stack_addr + stack_size + guard_size)) &&
           (guard_size * stack_incr) < req_guard_size) {
      guard_size += stack_incr;
    }

    /* normalize back to positive.  */
    detected_stack_size_ = static_cast<size_t>(stack_size * stack_incr);
    detected_guard_size_ = static_cast<size_t>(guard_size * stack_incr);
  }

  size_t detected_stack_size_ = 0;
  size_t detected_guard_size_ = 0;
};

class GuardTest : public testing::TestWithParam<std::tuple<size_t, GuardSpec>> {
};

TEST_P(GuardTest, StackAndGuardSizesAreOk) {
  const auto& [stack_multiplier, guard_spec] = GetParam();
  const int64_t sysconf_pagesize = sysconf(_SC_PAGESIZE);
  ASSERT_GT(sysconf_pagesize, 0);
  const size_t pagesize = static_cast<size_t>(sysconf_pagesize);

  const size_t stack_size = stack_multiplier * pagesize;
  const size_t guard_size = guard_spec.calculator(stack_multiplier, pagesize);

  ASSERT_GT(guard_size, 0u);

  GuardTestThread thread(stack_size, guard_size);
  thread.Start();
  thread.Join();

  // "Stuff" on the stack may have already consumed up to approximately
  // PTHREAD_STACK_MIN bytes of space.
  constexpr size_t kStackMin = 16384u;
  ASSERT_GT(stack_size, kStackMin);
  const size_t expected_stack_size = stack_size - kStackMin;

  VLOG(1) << "detected available stack size " << thread.detected_stack_size()
          << ", guard size >= " << thread.detected_guard_size();

  EXPECT_GE(thread.detected_stack_size(), expected_stack_size);
  EXPECT_GE(thread.detected_guard_size(), guard_size);
}

INSTANTIATE_TEST_SUITE_P(
    , GuardTest,
    testing::Combine(testing::Values(16, 256),
                     testing::Values(kOneByte, kOnePage, kHalfStack)),
    [](const testing::TestParamInfo<std::tuple<size_t, GuardSpec>>& info) {
      return absl::StrCat("StackMultiplier_", std::get<0>(info.param),
                          "_GuardType_", std::get<1>(info.param).name);
    });

}  // namespace
