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

#ifndef THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILING_TEST_UTIL_H_
#define THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILING_TEST_UTIL_H_

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/spinlock.h"
#include "gloop/base/sysinfo.h"

namespace lock_profiling_test {

// The tests cover both mutex locks and spin locks. Define an interface
// and appropriate implementations to abstract out the details of the
// type of lock being tested.
enum TestLockType { kMutexLock, kSpinLock };

// Base interface class for testing locks
class ABSL_LOCKABLE TestLockInterface {
 public:
  TestLockInterface() = default;
  virtual ~TestLockInterface() = default;

  // Method interface to lock a lock.
  virtual void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() = 0;

  // Method interface to unlock a lock.
  virtual void Unlock() ABSL_UNLOCK_FUNCTION() = 0;

  // Return the name of the lock and the unlock methods that we expect to find
  // at the root of all contending stacks.
  virtual std::string ExpectedLockMethodName() = 0;
  virtual std::string ExpectedUnlockMethodName() = 0;

  // Factory interface to return a new lock object matching the passed in type.
  // nullptr is returned if the passed in lock type does not match a known type.
  static TestLockInterface* New(enum TestLockType lock_type);

 private:
  TestLockInterface(const TestLockInterface&) = delete;
  TestLockInterface& operator=(const TestLockInterface&) = delete;
};

// Class to test a Google3 Mutex lock
class ABSL_LOCKABLE MutexTestLock : public TestLockInterface {
 public:
  MutexTestLock() = default;
  // Ensure we can find these function calls as physical frames when optimized.
  void ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL Lock()
      ABSL_EXCLUSIVE_LOCK_FUNCTION() override {
    mu_.lock();
  }
  // Ensure we can find these function calls as physical frames when optimized.
  void ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL Unlock()
      ABSL_UNLOCK_FUNCTION() override {
    mu_.unlock();
  }

  std::string ExpectedLockMethodName() override {
    return "lock_profiling_test::MutexTestLock::Lock";
  }

  std::string ExpectedUnlockMethodName() override {
    return "lock_profiling_test::MutexTestLock::Unlock";
  }

 private:
  absl::Mutex mu_;  // mutex to test

  MutexTestLock(const MutexTestLock&) = delete;
  MutexTestLock& operator=(const MutexTestLock&) = delete;
};

// Class to test a Google3 spinlock
class ABSL_LOCKABLE SpinTestLock : public TestLockInterface {
 public:
  SpinTestLock() = default;
  // Ensure we can find these function calls as physical frames when optimized.
  void ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL Lock()
      ABSL_EXCLUSIVE_LOCK_FUNCTION() override {
    spin_lock_.lock();
  }
  // Ensure we can find these function calls as physical frames when optimized.
  void ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL Unlock()
      ABSL_UNLOCK_FUNCTION() override {
    spin_lock_.unlock();
  }

  std::string ExpectedLockMethodName() override {
    return "lock_profiling_test::SpinTestLock::Lock";
  }

  std::string ExpectedUnlockMethodName() override {
    return "lock_profiling_test::SpinTestLock::Unlock";
  }

 private:
  SpinLock spin_lock_;  // spinlock to test

  SpinTestLock(const SpinTestLock&) = delete;
  SpinTestLock& operator=(const SpinTestLock&) = delete;
};

// The LockHolder class is responsible for the code that causes contention,
// and providing the stats tracking that contention.
class LockHolder {
 public:
  explicit LockHolder(enum TestLockType lock_type) {
    test_lock_.reset(TestLockInterface::New(lock_type));
    Reset();
  }
  virtual ~LockHolder() = default;

  void StartRuntime() {
    runtime_start_ = absl::Now();
    hold_time_accumulation_ = absl::ZeroDuration();
    hold_and_wait_time_accumulation_ = absl::ZeroDuration();
    Reset();
  }

  void Reset() {
    hold_time_ = absl::ZeroDuration();
    lock_invocation_count_ = 0;
    attempted_lock_acquisitions_.store(0, std::memory_order_relaxed);
  }

  // Capture the end time of the run.
  void StopRuntime() { runtime_ = absl::Now() - runtime_start_; }

  // Return the time it took for the threadpool to complete the work.
  absl::Duration GetRuntime() const { return runtime_; }

  // Return the total amount of time that the lock was held by the
  // worker threads.
  absl::Duration GetHoldTimeAccumulation() const {
    return hold_time_accumulation_;
  }

  // Return the total amount of time for which the lock waited for and was held
  // for by the worker threads.
  absl::Duration GetHoldAndWaitTimeAccumulation() const {
    return hold_and_wait_time_accumulation_;
  }

  // Return the number of times that the lock was locked.
  int64_t GetLockInvocationCount() const { return lock_invocation_count_; }

  // Return the name of the method which will appear in the call stack
  // for the type of lock used.
  const std::string ExpectedLockMethodName() const {
    return test_lock_->ExpectedLockMethodName();
  }
  const std::string ExpectedUnlockMethodName() const {
    return test_lock_->ExpectedUnlockMethodName();
  }

  // CauseLockContention() waits until all the threads are ready then
  // allows the threads through the lock, each thread holding the
  // lock for a brief period of time as specified by the hold_time argument.
  void CauseLockContention(int number_of_threads, absl::Duration hold_time);

 private:
  std::unique_ptr<TestLockInterface> test_lock_;
  int64_t lock_invocation_count_ = 0;
  absl::Time runtime_start_ = absl::InfiniteFuture();
  absl::Duration hold_time_ = absl::ZeroDuration();
  absl::Duration runtime_ = absl::ZeroDuration();
  std::atomic<int> attempted_lock_acquisitions_ = 0;
  absl::Duration hold_time_accumulation_ = absl::ZeroDuration();
  absl::Duration hold_and_wait_time_accumulation_ = absl::ZeroDuration();

  LockHolder(const LockHolder&) = delete;
  LockHolder& operator=(const LockHolder&) = delete;
};

// The tests have two kinds of call stack, a simple call stack where
// the lock is directly called, and code to produce a more complex call stack.
// The more complex call stack produces a different version for every thread
// and these different versions potentially generate contention and
// dropped data. This allows us to test what happens at high thread counts.

enum CallstackType { kSimpleCallstack, kContextCallstack, kComplexCallstack };

// Interface class for handling different kinds of callstacks.
class CallstackInterface {
 public:
  CallstackInterface() = default;
  virtual ~CallstackInterface() = default;
  virtual void CallLockHolder(int number_of_threads, absl::Duration hold_time,
                              LockHolder* lockHolder) = 0;
  // Factory function to generate callstack objects of the appropriate type.
  // Factory interface to return a new callstack object matching the passed
  // in type. nullptr is returned if the passed in lock type does not match
  // a known type.
  static CallstackInterface* New(enum CallstackType callstack_type);

 private:
  CallstackInterface(const CallstackInterface&) = delete;
  CallstackInterface& operator=(const CallstackInterface&) = delete;
};

// Subclass of CallstackInterface that has a simple callstack.
class SimpleCallstack : public CallstackInterface {
 public:
  SimpleCallstack() = default;
  void CallLockHolder(int number_of_threads, absl::Duration hold_time,
                      LockHolder* lockHolder) override {
    lockHolder->CauseLockContention(number_of_threads, hold_time);
  }
};

// Subclass of CallstackInterface that has a simple callstack with Context.
class ContextCallstack : public CallstackInterface {
 public:
  ContextCallstack() = default;
  void CallLockHolder(int number_of_threads, absl::Duration hold_time,
                      LockHolder* lockHolder) override {
    lockHolder->CauseLockContention(number_of_threads, hold_time);
  }
};

// Subclass of CallstackInterface that generates multiple complex callstacks.
class ComplexCallstack : public CallstackInterface {
 public:
  ComplexCallstack() = default;
  void CallLockHolder(int number_of_threads, absl::Duration hold_time,
                      LockHolder* lockHolder) override {
    pid_t tid = GetTID();
    // Stack depth of 6 produces up to 4^6 = 4096 different stacks
    // The table size is 1024, so we are certain to get some evictions.
    MakeCall(tid, 6, number_of_threads, hold_time, lockHolder);
  }

 private:
  // Code to make a call stack which contains a number of distinguishable
  // frames. This allows the generation of a large number of different
  // shallow stacks. We use these to test what happens when we have
  // a large number of contending stack traces.
  // Common code for determining which function to call next. Notice that the
  // code is marked as inline to reduce the number of frames on the call stack.
  ABSL_ATTRIBUTE_ALWAYS_INLINE int MakeCall(pid_t tid, int depth, int nthreads,
                                            absl::Duration hold_time,
                                            LockHolder* lockHolder) {
    if (depth < 0) {
      lockHolder->CauseLockContention(nthreads, hold_time);
      return 0;
    }
    // Pick one of four directions based on the bits extracted
    // from the thread id.
    int direction = (tid >> (depth << 1)) & 3;
    switch (direction) {
      case 0:
        return ChooseStack0(tid, depth - 1, nthreads, hold_time, lockHolder);
      case 1:
        return ChooseStack1(tid, depth - 1, nthreads, hold_time, lockHolder);
      case 2:
        return ChooseStack2(tid, depth - 1, nthreads, hold_time, lockHolder);
      case 3:
        return ChooseStack3(tid, depth - 1, nthreads, hold_time, lockHolder);
    }
    return 0;
  }

  // Four different routines to insert into call stack. They all adjust the
  // return value to avoid tail call optimization. Each routine also tweaks
  // the input tid to avoid the compiler combining them into a single version.
  ABSL_ATTRIBUTE_NOINLINE int ChooseStack0(pid_t tid, int depth, int nthreads,
                                           absl::Duration hold_time,
                                           LockHolder* lockHolder) {
    return MakeCall(tid ^ (1 << (0 + 5)), depth, nthreads, hold_time,
                    lockHolder) +
           1;
  }

  ABSL_ATTRIBUTE_NOINLINE int ChooseStack1(pid_t tid, int depth, int nthreads,
                                           absl::Duration hold_time,
                                           LockHolder* lockHolder) {
    return MakeCall(tid ^ (1 << (1 + 5)), depth, nthreads, hold_time,
                    lockHolder) +
           1;
  }

  ABSL_ATTRIBUTE_NOINLINE int ChooseStack2(pid_t tid, int depth, int nthreads,
                                           absl::Duration hold_time,
                                           LockHolder* lockHolder) {
    return MakeCall(tid ^ (1 << (2 + 5)), depth, nthreads, hold_time,
                    lockHolder) +
           1;
  }

  ABSL_ATTRIBUTE_NOINLINE int ChooseStack3(pid_t tid, int depth, int nthreads,
                                           absl::Duration hold_time,
                                           LockHolder* lockHolder) {
    return MakeCall(tid ^ (1 << (3 + 5)), depth, nthreads, hold_time,
                    lockHolder) +
           1;
  }
};

}  // namespace lock_profiling_test

#endif  // THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILING_TEST_UTIL_H_
