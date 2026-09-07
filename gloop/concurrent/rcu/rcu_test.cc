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

#include "gloop/concurrent/rcu/rcu.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/distributions.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/googleinit.h"
#include "gloop/base/signal_util_subtle.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/thread.h"
#include "gloop/util/random/mt_random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef ABSL_HAVE_LEAK_SANITIZER
#include <sanitizer/lsan_interface.h>
#endif

ABSL_FLAG(int32_t, reader_spin_ns, 1000,
          "how long to spin while locked in "
          "antagonist readers");

namespace {
namespace rcu = base::rcu;

REGISTER_MODULE_INITIALIZER(rcu_callbacks, { rcu::Domain::EnableCleanup(); });

class RcuTest : public ::testing::Test {
 protected:
  ~RcuTest() override {
    // TODO: validate that no reader locks held/similar
    delete v.Replace(nullptr, &d);
  }
  rcu::Domain d;
  rcu::Value<int> v{&d};
};

TEST_F(RcuTest, IndependentDomains) {
  rcu::Domain d1, d2;

  rcu::ReaderLockHolder h(&d1);

  // This should still complete
  d2.Synchronize();
}

// Verifies that a pinned/blocked domain does not starve other domains in the
// queue. See b/528371810 for context.
TEST_F(RcuTest, DomainSchedulerFairness) {
  rcu::Domain d1, d2;
  absl::Notification d1_callback_ran;
  std::atomic<bool> holder_released{false};

  // Permanently pin Domain d1 by holding an active reader lock on it.
  auto holder = std::make_unique<rcu::ReaderLockHolder>(&d1);

  // Register a dummy callback on d1.
  // Because d1 is pinned, this callback can never complete while the lock is
  // held. This forces d1 to remain stuck in RCU's active background domains
  // list. We verify that it does not run prematurely (before holder is
  // released).
  d1.Call([&d1_callback_ran, &holder_released]() {
    if (!holder_released.load(std::memory_order_acquire)) {
      ADD_FAILURE() << "d1 callback ran while domain was blocked!";
    }
    d1_callback_ran.Notify();
  });

  // Sleep for 100ms on the main thread to give the cleanup thread a good chance
  // to wake up, pop d1, and enter the spinning starvation loop before we
  // register any callbacks on d2.
  absl::SleepFor(absl::Milliseconds(100));

  // Register an asynchronous callback on Domain d2.
  absl::Notification callback_ran;
  d2.Call([&callback_ran]() { callback_ran.Notify(); });

  // Wait for the callback on d2 to be executed by the cleanup thread.
  // Verify Domain d2's callbacks were not starved by pinned Domain d1.
  callback_ran.WaitForNotification();

  // Verify d1 callback hasn't run yet (since d1 is still blocked)
  EXPECT_FALSE(d1_callback_ran.HasBeenNotified());

  // Unblock d1 to allow clean shutdown and verify it runs eventually.
  holder_released.store(true, std::memory_order_release);
  holder.reset();

  d1_callback_ran.WaitForNotification();
}

TEST_F(RcuTest, MultipleQueueCallsOnBlockedDomain) {
  rcu::Domain d1;
  const int kNumCalls = 10;
  absl::BlockingCounter counter(kNumCalls);

  // Block d1
  auto holder = std::make_unique<rcu::ReaderLockHolder>(&d1);

  // Call Call() multiple times while blocked.
  // We sleep between calls to allow the cleanup thread to pop the domain
  // and reset the queued_for_run_ flag, forcing duplicates in buggy code.
  for (int i = 0; i < kNumCalls; ++i) {
    d1.Call([&counter]() { counter.DecrementCount(); });
    absl::SleepFor(absl::Milliseconds(5));
  }

  // We expect that consecutive Call() invocations on a blocked domain do not
  // result in duplicate entries in RCU's active cleanup list.
  // Any duplicate queue requests should be merged, keeping the active queue
  // count (num_queues_) at exactly 1.
  // Wait for the cleanup thread to process the final pop and deduplicate
  // callbacks.
  while (rcu::DomainTestPeer::num_queues(d1) > 1) {
    absl::SleepFor(absl::Milliseconds(1));
  }
  EXPECT_EQ(rcu::DomainTestPeer::num_queues(d1), 1)
      << "Duplicates were queued! num_queues should be 1.";

  // Unblock d1
  holder.reset();

  // Wait for all callbacks to complete.
  counter.Wait();
}

TEST_F(RcuTest, SequentialCallsOnSameDomain) {
  rcu::Domain d1;
  absl::Notification cb1_ran;
  d1.Call([&cb1_ran]() { cb1_ran.Notify(); });
  cb1_ran.WaitForNotification();

  // Verify that after the first cleanup completes, in_cleanup_list_ is reset
  // so a subsequent Call() on the same domain executes successfully.
  absl::Notification cb2_ran;
  d1.Call([&cb2_ran]() { cb2_ran.Notify(); });
  cb2_ran.WaitForNotification();
}

TEST_F(RcuTest, CallIsAsync) {
  absl::Notification n;
  {
    rcu::ReaderLockHolder h(&d);
    d.Call([&n]() { n.Notify(); });
  }
  n.WaitForNotification();
}

TEST_F(RcuTest, Delete) {
  static const size_t kNumOps = 100 * 1000;
  for (int i = 0; i < kNumOps; ++i) {
    d.Free(v.ReplaceUnsynchronized(new int));
    std::unique_ptr<int> p(new int);
    d.Free(std::move(p));
    std::unique_ptr<const int> constp(new int);
    d.Free(std::move(constp));
  }
  d.Synchronize();
}

TEST_F(RcuTest, DeleteArray) {
  static const size_t kNumOps = 100 * 1000;
  for (int i = 0; i < kNumOps; ++i) {
    d.FreeArray(v.ReplaceUnsynchronized(new int[1024]));
    std::unique_ptr<int[]> p(new int[1024]);
    d.Free(std::move(p));
    std::unique_ptr<const int[]> constp(new int[1024]);
    d.Free(std::move(constp));
  }
  d.Synchronize();
  delete[] v.Replace(nullptr, &d);
}

// RCU callbacks are stored on a dynamic data structure that shrinks
// if persistently unused. Make sure that works OK.
TEST_F(RcuTest, ShrinkPile) {
  for (int i = 0; i < 16; ++i) {
    {
      rcu::ReaderLockHolder h(&d);
      for (int j = 0; j < 1000 * 1000; ++j) {
        d.CallRaw([](void*) {}, nullptr);
      }
    }
    for (int j = 0; j < 16; ++j) {
      d.Synchronize();
    }
  }
}

#ifdef ABSL_HAVE_LEAK_SANITIZER
TEST_F(RcuTest, Leaks) {
  {
    rcu::ReaderLockHolder h(&d);
    for (int i = 0; i < 100000; ++i) {
      d.Free(new int);
    }
    EXPECT_EQ(0, __lsan_do_recoverable_leak_check());
  }
  EXPECT_EQ(0, __lsan_do_recoverable_leak_check());
  d.Synchronize();
  EXPECT_EQ(0, __lsan_do_recoverable_leak_check());
}
#endif

// These are mostly tests that various patterns work OK with the annotalysis.
TEST_F(RcuTest, DomainHolder) {
  rcu::Value<int> v(&d);
  rcu::ReaderLockHolder h(&d);
  EXPECT_EQ(nullptr, v.Get(&d));
}

TEST_F(RcuTest, DomainRaw) {
  rcu::Value<int> v(&d);

  rcu::Token t = d.ReaderLock();
  EXPECT_EQ(nullptr, v.Get(&d));
  d.ReaderUnlock(t);
}

TEST_F(RcuTest, DomainReplace) {
  rcu::Value<int> v(&d);
  EXPECT_EQ(nullptr, v.Replace(nullptr, &d));
}

// Exercises the ReaderLockHolder's move constructor (must be exempted from
// thread-safety analysis but should be safe). An incorrect implementation may
// cause this test to hang.
void MoveReaderLockHolder(rcu::Domain* d) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  rcu::Value<int> v(d);
  rcu::ReaderLockHolder h(d);
  rcu::ReaderLockHolder j = std::move(h);
  EXPECT_EQ(nullptr, v.Get(d));
}

TEST_F(RcuTest, MoveReaderLockHolder) { MoveReaderLockHolder(&d); }

// Exercises the ReaderLockHolder's move assignment operator (must be exempted
// from thread-safety analysis but should be safe). An incorrect implementation
// may cause this test to hang.
void MoveAssignReaderLockHolder(rcu::Domain* d) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  rcu::Value<int> v(d);
  rcu::ReaderLockHolder h(d), j(d);
  std::swap(h, h);
  j = std::move(h);
  EXPECT_EQ(nullptr, v.Get(d));
}

TEST_F(RcuTest, MoveAssignReaderLockHolder) { MoveAssignReaderLockHolder(&d); }

typedef RcuTest RcuDeathTest;

TEST_F(RcuDeathTest, MixingDomains) {
  rcu::Domain d1, d2;
  rcu::Value<int> v(&d1);

  {
    rcu::ReaderLockHolder h(&d1);
    v.Get(&d1);
  }
  {
    rcu::ReaderLockHolder h(&d2);
    EXPECT_DEBUG_DEATH(v.Get(&d2), "Value.*owned by.*used by");
  }
  EXPECT_DEBUG_DEATH(v.Replace(nullptr, &d2), "Value.*owned by.*used by");
}

// Ensure the RCU cleanup thread shows up in threadz, which indicates
// //thread knows about it and will report it in the appropriate
// places.
TEST_F(RcuTest, Threadz) {
  class StringStackWriter : public ThreadStackWriter {
   public:
    explicit StringStackWriter(std::string* s) : s_(s) {}
    ~StringStackWriter() override = default;
    void Write(const char* data, int data_length) override {
      absl::string_view v(data, data_length);
      absl::StrAppend(s_, v);
    }

   private:
    std::string* s_;
  };

  // We've already created RCU objects in the test fixture, so it
  // should be there.
  std::string threadz;
  StringStackWriter ssw(&threadz);
  Thread_ExtractStacks(&ssw);
  EXPECT_THAT(threadz, testing::HasSubstr("rcu_callback_thread"));
}

// Tests that ConstInitType domains don't detonate if the process
// terminates while they're doing work.

class ChattyDomain {
 public:
  ChattyDomain() {
    // Fake constant initialization.
    d_ = new rcu::Domain(absl::kConstInit);
  }
  ~ChattyDomain() {
    if (!d_) return;
    // Run the destructor without giving the memory back to malloc to
    // scribble on (i.e. like what would happen to a global static
    // object at program death.)
    d_->~Domain();
    absl::SleepFor(absl::Milliseconds(100));
    LOG(FATAL) << "ChattyDomainStopped";
  }
  rcu::Domain* d_;
};

// This is wildly in violation of style guide about static globals,
// but I don't care (we're testing safety here, and this is the only way.)
// The above class creates an effectively linker-initialized Domain
auto* chatty = new ChattyDomain;  // NOLINT(google3-runtime-global-variables)

TEST_F(RcuDeathTest, CleanShutdown) {
  // This will trigger cleanup of <chatty>, which will trigger
  // ~Domain, which should go through OK, leading to the LOG(FATAL)
  auto f = []() {
    delete chatty;
    chatty = new ChattyDomain;
    {
      rcu::ReaderLockHolder l(chatty->d_);
      for (int i = 0; i < 500; ++i) {
        chatty->d_->CallRaw(
            +[](void*) { absl::SleepFor(absl::Milliseconds(1)); }, nullptr);
      }
    }
    // We'll now spend ~500msec cleaning up the above, while we try to die.
    exit(0);
  };

  EXPECT_DEATH(f(), "ChattyDomainStopped");
}

ABSL_CONST_INIT static rcu::Domain benchmark_d(
    absl::kConstInit);  // NOLINT(google3-runtime-global-variables)
ABSL_CONST_INIT static rcu::Value<
    int>  // NOLINT(google3-runtime-global-variables)
    benchmark_p(&benchmark_d);

#define READER_BENCHMARK(Name, HolderType, Val, Domain)                \
  static void Name##ReadBenchSetup(const benchmark::State& state) {    \
    Val.Replace(new int, &Domain);                                     \
  }                                                                    \
                                                                       \
  static void Name##ReadBenchTeardown(const benchmark::State& state) { \
    delete Val.Replace(nullptr, &Domain);                              \
  }                                                                    \
                                                                       \
  static void BM_##Name##_Read(benchmark::State& state) {              \
    for (auto s : state) {                                             \
      HolderType h(&Domain);                                           \
      CHECK(Val.Get(&Domain) != nullptr);                              \
    }                                                                  \
  }                                                                    \
                                                                       \
  BENCHMARK(BM_##Name##_Read)                                          \
      ->Setup(Name##ReadBenchSetup)                                    \
      ->Teardown(Name##ReadBenchTeardown)                              \
      ->ThreadRange(1, NumCPUs());

thread::Fiber* updater;

#define READERS_AGAINST_UPDATE_BENCHMARK(Name, HolderType, Val, Domain)       \
  static void Name##_ReadersAgainstUpdateBenchSetup(                          \
      const benchmark::State& state) {                                        \
    Val.Replace(new int(-1), &Domain);                                        \
    struct Helper {                                                           \
      static void Work() {                                                    \
        int count = 0;                                                        \
        while (!thread::Fiber::Current()->Cancelled()) {                      \
          delete Val.Replace(new int(count++), &Domain);                      \
        }                                                                     \
      }                                                                       \
    };                                                                        \
    updater = thread::NewTree(thread::TreeOptions(), Helper::Work).release(); \
  }                                                                           \
                                                                              \
  static void Name##_ReadersAgainstUpdateBenchTeardown(                       \
      const benchmark::State& state) {                                        \
    updater->Cancel();                                                        \
    updater->Join();                                                          \
    delete updater;                                                           \
    delete Val.Replace(nullptr, &Domain);                                     \
  }                                                                           \
                                                                              \
  static void BM_##Name##_ReadersAgainstUpdate(benchmark::State& state) {     \
    for (auto s : state) {                                                    \
      HolderType h(&Domain);                                                  \
      CHECK(Val.Get(&Domain) != nullptr);                                     \
    }                                                                         \
  }                                                                           \
                                                                              \
  BENCHMARK(BM_##Name##_ReadersAgainstUpdate)                                 \
      ->Setup(Name##_ReadersAgainstUpdateBenchSetup)                          \
      ->Teardown(Name##_ReadersAgainstUpdateBenchTeardown)                    \
      ->ThreadRange(1, NumCPUs() - 1);

std::vector<std::unique_ptr<thread::Fiber> >* readers;

#define UPDATE_AGAINST_READERS_BENCHMARK(Name, HolderType, Val, Domain)   \
  static void Name##_UpdateAgainstReadersBenchSetup(                      \
      const benchmark::State& state) {                                    \
    int nreaders = state.range(0);                                        \
    readers = new std::vector<std::unique_ptr<thread::Fiber> >;           \
    struct Helper {                                                       \
      static void Work() {                                                \
        while (!thread::Fiber::Current()->Cancelled()) {                  \
          absl::Time stop =                                               \
              absl::Now() +                                               \
              absl::Nanoseconds(absl::GetFlag(FLAGS_reader_spin_ns));     \
          HolderType h(&Domain);                                          \
          CHECK(nullptr == Val.Get(&Domain));                             \
          while (absl::Now() < stop) { /*spin*/                           \
          }                                                               \
        }                                                                 \
      }                                                                   \
    };                                                                    \
    for (int i = 0; i < nreaders; ++i) {                                  \
      readers->emplace_back(                                              \
          thread::NewTree(thread::TreeOptions(), Helper::Work));          \
    }                                                                     \
  }                                                                       \
                                                                          \
  static void Name##_UpdateAgainstReadersBenchTeardown(                   \
      const benchmark::State& state) {                                    \
    for (auto& f : *readers) {                                            \
      f->Cancel();                                                        \
      f->Join();                                                          \
    }                                                                     \
    delete readers;                                                       \
  }                                                                       \
                                                                          \
  static void BM_##Name##_UpdateAgainstReaders(benchmark::State& state) { \
    for (auto s : state) { /* who cares what the value is */              \
      Val.Replace(nullptr, &Domain);                                      \
    }                                                                     \
  }                                                                       \
                                                                          \
  BENCHMARK(BM_##Name##_UpdateAgainstReaders)                             \
      ->Setup(Name##_UpdateAgainstReadersBenchSetup)                      \
      ->Teardown(Name##_UpdateAgainstReadersBenchTeardown)                \
      ->Range(0, NumCPUs() - 1);

#define ALL_BENCHMARKS(Name, HolderType, Val, Domain)              \
  READER_BENCHMARK(Name, HolderType, Val, Domain);                 \
  READERS_AGAINST_UPDATE_BENCHMARK(Name, HolderType, Val, Domain); \
  UPDATE_AGAINST_READERS_BENCHMARK(Name, HolderType, Val, Domain);

ALL_BENCHMARKS(rcu, rcu::ReaderLockHolder, benchmark_p, benchmark_d);

static void BM_Call(benchmark::State& state) {
  for (auto s : state) {
    benchmark_d.Call([]() {});
  }
}
BENCHMARK(BM_Call)
    ->ThreadRange(1, NumCPUs() - 1)
    ->Teardown(+[](const benchmark::State&) { benchmark_d.Synchronize(); });

static void BM_CallRaw(benchmark::State& state) {
  for (auto s : state) {
    benchmark_d.CallRaw(+[](void*) {}, nullptr);
  }
}
BENCHMARK(BM_CallRaw)
    ->ThreadRange(1, NumCPUs() - 1)
    ->Teardown(+[](const benchmark::State&) { benchmark_d.Synchronize(); });

static void BM_lifetime(benchmark::State& state) {
  for (auto s : state) {
    rcu::Domain d;
    d.Synchronize();
  }
}
BENCHMARK(BM_lifetime)->UseRealTime()->ThreadRange(1, NumCPUs() - 1);

}  // namespace
