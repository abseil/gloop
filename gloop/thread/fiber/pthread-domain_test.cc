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

#include "gloop/thread/fiber/pthread-domain.h"

#include <memory>

#if defined(__linux__)
#include <pthread.h>
#include <sys/prctl.h>
#endif

#include <string>
#include <tuple>

#include "absl/synchronization/notification.h"
#include "gloop/base/scheduling/domain-test.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gtest/gtest.h"

namespace base {
namespace scheduling {

INSTANTIATE_TEST_SUITE_P(
    PthreadDomainTest, DomainTest,
    ::testing::Values(std::make_tuple(thread::NewPthreadDomain, true)));

#if defined(__linux__)
namespace {

// Returns the name of the current thread.
std::string CurrentThreadName() {
  char buf[16] = {};
  EXPECT_EQ(prctl(PR_GET_NAME, buf), 0);
  return buf;
}

}  // namespace

// A fiber that starts on a new thread of the pthread domain gives the thread
// its name.
TEST(PthreadDomainFiberNameTest, PropagatesFiberNameToPthread) {
  Domain* domain = thread::NewPthreadDomain("test_pdom", 1);
  Scheduler* scheduler = thread::NewRootFIFOScheduler(domain);

  std::string actual_name;
  std::unique_ptr<thread::Fiber> named_fiber = thread::NewTree(
      thread::TreeOptions().set_scheduler(scheduler).set_fiber_options(
          thread::FiberOptions().SetInternedName("CustomFiberName")),
      [&actual_name] { actual_name = CurrentThreadName(); });
  named_fiber->Join();
  EXPECT_EQ(actual_name, "CustomFiberName");

  scheduler->Orphan();
  delete domain;
}

// When a fiber finishes while other fibers are queued, the pthread domain
// recycles its thread for the next queued fiber instead of starting a new one.
// The recycled thread takes the name of the fiber it now runs, or the default
// name if that fiber is unnamed.
TEST(PthreadDomainFiberNameTest, RenamesRecycledThread) {
  Domain* domain = thread::NewPthreadDomain("test_pdom", 1);
  Scheduler* scheduler = thread::NewRootFIFOScheduler(domain);

  // With a concurrency of 1, `named` and `unnamed` are queued while `first`
  // runs and then run on its thread, one after the other.
  absl::Notification queued;
  pthread_t first_thread;
  std::unique_ptr<thread::Fiber> first = thread::NewTree(
      thread::TreeOptions().set_scheduler(scheduler).set_fiber_options(
          thread::FiberOptions().SetInternedName("FirstFiber")),
      [&] {
        first_thread = pthread_self();
        while (!queued.HasBeenNotified()) {
        }
      });
  pthread_t named_thread;
  std::string named_name;
  std::unique_ptr<thread::Fiber> named = thread::NewTree(
      thread::TreeOptions().set_scheduler(scheduler).set_fiber_options(
          thread::FiberOptions().SetInternedName("NamedFiber")),
      [&] {
        named_thread = pthread_self();
        named_name = CurrentThreadName();
      });
  pthread_t unnamed_thread;
  std::string unnamed_name;
  std::unique_ptr<thread::Fiber> unnamed =
      thread::NewTree(thread::TreeOptions().set_scheduler(scheduler), [&] {
        unnamed_thread = pthread_self();
        unnamed_name = CurrentThreadName();
      });
  queued.Notify();
  first->Join();
  named->Join();
  unnamed->Join();

  // Both queued fibers ran on the recycled thread of `first`, and each saw its
  // own name rather than the name of the fiber that ran before it.
  EXPECT_TRUE(pthread_equal(named_thread, first_thread));
  EXPECT_TRUE(pthread_equal(unnamed_thread, first_thread));
  EXPECT_EQ(named_name, "NamedFiber");
  EXPECT_EQ(unnamed_name, "test_pdom-PDoma");

  scheduler->Orphan();
  delete domain;
}
#endif  // defined(__linux__)

}  // namespace scheduling
}  // namespace base
