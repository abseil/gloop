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

#include "gloop/util/callback/blocking_callback.h"

#include <limits.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/thread/thread.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/gtl/stl_util.h"
#include "gtest/gtest.h"

namespace {

void IncrementN(int* n) { *n += 1; }

// Waiter thread, used to simulate many threads waiting for blocking callback to
// finish.
class Waiter : public Thread {
 public:
  explicit Waiter(BlockingClosure* closure) : Thread(), closure_(closure) {
    SetJoinable(true);
    Start();
  }

  // This type is neither copyable nor movable.
  Waiter(const Waiter&) = delete;
  Waiter& operator=(const Waiter&) = delete;
  ~Waiter() {}

  virtual void Run() { closure_->Wait(); }

 private:
  BlockingClosure* closure_;
};

// Basic test of the case where the callback is a no-op.
TEST(BlockingCallbackTest, Noop) {
  BlockingClosure* done = new BlockingClosure;
  ClosureThread thread(::util::functional::FromCallbackWithOwnership(done));
  thread.Start();
  done->Wait();
  done->Wait();  // should return immediately
}

TEST(BlockingCallbackTest, WaitWithTimeout) {
  BlockingClosure* done = new BlockingClosure;
  ASSERT_FALSE(done->WaitWithTimeout(absl::ZeroDuration()));
  ASSERT_FALSE(done->WaitWithTimeout(absl::Milliseconds(10)));
  ClosureThread thread(::util::functional::FromCallbackWithOwnership(done));
  thread.Start();
  ASSERT_TRUE(done->WaitWithTimeout(absl::Minutes(1)));
  ASSERT_TRUE(done->WaitWithTimeout(absl::Milliseconds(10)));  // immediate
}

// Verify that everything works properly even if many waiters are present.
TEST(BlockingCallbackTest, ManyWaiters) {
  BlockingClosure done;
  std::vector<Waiter*> threads;
  for (int thread = 0; thread < 10; ++thread) {
    threads.push_back(new Waiter(&done));
  }
  // Run the callback and verify that all waiter threads exit.
  done.Run();
  for (std::vector<Waiter*>::iterator thread = threads.begin();
       thread != threads.end(); ++thread) {
    (*thread)->Join();
  }
  gtl::STLDeleteElements(&threads);
}

void CallNumTimes(BlockingClosure* closure, int N) {
  for (int i = 0; i < N; ++i) closure->Run();
}

TEST(BlockingCallbackTest, WaitForNum) {
  const int kCalls = 10;

  BlockingClosure done;
  ClosureThread thread(absl::bind_front(CallNumTimes, &done, kCalls));
  thread.Start();

  // Will block until "thread" has called "done" kCalls times
  done.WaitForNum(kCalls);
  EXPECT_LE(kCalls, done.num_called());
}

TEST(BlockingCallbackTest, WaitForNumCalled) {
  const int kCalls = 10;

  BlockingClosure done;
  ClosureThread thread(absl::bind_front(CallNumTimes, &done, kCalls));
  thread.Start();

  // Will block until "thread" has called "done" kCalls times
  EXPECT_TRUE(done.WaitForNumCalled(kCalls, absl::InfiniteDuration()));
  EXPECT_LE(kCalls, done.num_called());
}

TEST(BlockingCallbackTest, WaitForNumCalledDeadlineExpiration) {
  const int kCalls = 10;
  BlockingClosure done;

  // No calls to the closure; this expires
  done.WaitForNumCalled(kCalls, absl::Milliseconds(100));

  ClosureThread thread(absl::bind_front(CallNumTimes, &done, kCalls - 1));
  thread.Start();

  // (kCalls - 1) calls to the closure; this expires
  EXPECT_FALSE(done.WaitForNumCalled(kCalls, absl::Milliseconds(100)));

  // Call the closure 2 more times... so we are up to (kCalls + 1) calls
  done.Run();
  done.Run();

  // Now, wait again (this should return immediately)
  EXPECT_TRUE(done.WaitForNumCalled(kCalls, absl::Milliseconds(100)));
}

void WaitForNumCalls(BlockingClosure* closure, int N) {
  EXPECT_TRUE(closure->WaitForNumCalled(N, absl::InfiniteDuration()));
}

TEST(BlockingCallbackTest, MultipleWaitersForNumCalled) {
  const int kNumThreads = 10;
  BlockingClosure done;

  // Create kNumThreads threads blocked on "done" being called once
  std::vector<ClosureThread*> threads;
  for (int thread = 0; thread < kNumThreads; ++thread) {
    auto c = absl::bind_front(WaitForNumCalls, &done, 1 /* calls */);
    ClosureThread* ct = new ClosureThread(std::move(c));
    ct->SetJoinable(true);
    ct->Start();
    threads.push_back(ct);
  }

  // Actually call the Closure, unblocking all of the threads
  done.Run();

  for (int thread = 0; thread < kNumThreads; ++thread) {
    threads[thread]->Join();
  }

  gtl::STLDeleteElements(&threads);
}

// Verify that the inner callback is properly invoked, and that it has properly
// returned before Wait() returns.
TEST(BlockingCallbackTest, InnerCallback) {
  int n = 0;
  std::unique_ptr<Closure> increment_callback(
      ::util::functional::ToPermanentCallback([&n] { IncrementN(&n); }));
  BlockingClosure* done = new BlockingClosure(increment_callback.get());
  ClosureThread thread(::util::functional::FromCallbackWithOwnership(done));
  thread.Start();
  done->Wait();
  EXPECT_EQ(1, n);
  // Reset and run again.
  done->Reset();
  done->Run();
  done->Wait();
  EXPECT_EQ(2, n);
}

// Verify that closure is not destroyed if it's not called.
TEST(BlockingCallbackTest, ClosureNotCalled) {
  Closure* inner_callback = util::functional::ToCallback([] {});
  BlockingClosure* done = new BlockingClosure(inner_callback);
  delete done;
  // Should SEGV if the pointer has been deleted.
  inner_callback->Run();
}

// Verify that a NULL closure is still valid
TEST(BlockingCallbackTest, NullClosure) {
  BlockingClosure bc(nullptr);
  EXPECT_TRUE(bc.IsRepeatable());

  bc.Run();  // just test that we don't crash
}

}  // namespace
