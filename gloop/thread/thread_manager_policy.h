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

#ifndef THIRD_PARTY_GLOOP_THREAD_THREAD_MANAGER_POLICY_H_
#define THIRD_PARTY_GLOOP_THREAD_THREAD_MANAGER_POLICY_H_
// A ThreadManagerPolicy is used by a ThreadManager to control when new threads
// are added to its thread pools.  There is a default policy that most users
// should use, returned by DefaultThreadManagerPolicy().

// Data structures:
// class ThreadManagerPolicy
//    An abstract virtual class that may be implemented by the client to
//    provide a non-default thread-creation policy.
// struct ThreadManagerState
//    Passed to ThreadManagerPolicy::Eval() to indicate the internal state of
//    the ThreadManager.
// struct ThreadManagerAction
//    Returned by ThreadManagerPolicy::Eval() to express a policy decision.

// Thread manager policies are invoked based on one of two invocation
// rules.
//   time-based: Under this invocation rule, thread manager policies
//     are called based on timer expirations. The default timer value is
//     kTMOverseerSleepMS. However, if the ThreadManagerPolicy returns
//     a ThreadManagerAction.delay_ms > 0, then the next invocation of
//     the policy occurs in the specified number of milliseconds.
//   overload event (default): This invocation rule is event based.
//     This rule is selected by calling SetOverloadEvent(true). Under
//     this rule, a policy is only called when there are no idle
//     threads and there are waiting closures.
// For now, we allow both invocation rules. However, after we have conducted
// policy tests, it may be that just the time-based rule suffices, and so this
// interface will be deprecated. To reduce the test matrix, the invocation rules
// are mutually exclusive (although there is no inherent reason to have them be
// mutually exclusive).

// ThreadManagerAction allows for specifying two disjoint types of actions.
//   incremental-increase: By setting ThreadManagerAction.create
//     to true, this causes the creation of a single thread. Note that the
//     number of threads will decrease once the work queue goes idle
//     since idle threads are killed (after a suitable delay).
//   fixed-threads: This is specified by setting
//     ThreadManagerAction.desired_threads to a positive integer. If this is
//     greater than the current number of threads (accounting for pending
//     actions that impact the number of threads), then the number of threads
//     is increased. Similarly, it can result in decreasing the number of
//     threads if the request is larger than the current number of threads.
// For now, we allow both actions. However, after we have conducted
// policy tests, it may be that fixed threads suffice, and so this
// interface will be deprecated. To reduce the test matrix, the types of actions
// are mutually exclusive (although there is no inherent reason to have them be
// mutually exclusive).

#include <cstdint>

namespace thread {
class ThreadManagerPolicy;  // forward;

// Return an instance of the default ThreadManager policy.  This policy tries
// to create threads when they are needed to use available CPUs, and when doing
// so will increase the rate at which work will be done.
// The default policy needs to know the number of CPUs it should try to consume.
// The function NumCPUs() in base/sysinfo.h is an acceptable argument.
// The returned pointer should be passed via the options argument to the
// ThreadManager constructor, which will take ownership of it.
ThreadManagerPolicy* DefaultThreadManagerPolicy(int (*num_cpus)());

// Return an instance of the "eager" ThreadManager policy.  It creates a thread
// every time work is available and no thread is waiting to receive it, up to
// max_threads threads (rounded up to the next multiple of the number of pools
// in the ThreadManager).  Use INT_MAX for an unbounded number.
// BEWARE:  This policy can easily cause the programme to fail due to lack of
// threads or memory if many long-running Closures are queued.
ThreadManagerPolicy* EagerThreadManagerPolicy(int max_threads);

// Argument to ThreadManagerPolicy::Eval
struct ThreadManagerState {
  ThreadManagerState()
      : pool_count(0),
        pool_index(0),
        time_ms(0),
        closures_run(0),
        queue_length(0),
        threads(0),
        active(0),
        kill_pending(0),
        create_pending(0),
        idle(0),
        blocked(0),
        threads_since_last_exit(0) {}
  int32_t pool_count;    // number of pools in ThreadManager
  int32_t pool_index;    // which pool this data corresponds to [0, pool_count)
  int64_t time_ms;       // time of this state (UNIX time in ms)
  int64_t closures_run;  // total closures run in queue since beginning of time
  int queue_length;      // current queue length
  int threads;           // Count of pool's threads
                         // threads = active + kill_pending + create_pending
                         //   + blocked; note that idle are counted as blocked
  int active;            // count of queue's active threads
  int kill_pending;      // number of threads to be killed
  int create_pending;    // number of threads to be created
  int idle;              // count of idle threads
  int blocked;           // queue's threads currently blocked (approximate)
  int threads_since_last_exit;  // threads created for queue since
                                // last thread was destroyed.
};

// Result from ThreadManagerPolicy::Eval
struct ThreadManagerAction {
  bool create;          // whether to create a thread
                        // set to false to disable the action
                        // must be set to false if desired_threads > 0
  int desired_threads;  // desired number of active threads in current pool
                        // -1 disables the action
  int delay_ms;         // millisecond delay before next creation attempt
};

class ThreadManagerPolicy {
 public:
  // This type is neither copyable nor movable.
  ThreadManagerPolicy(const ThreadManagerPolicy&) = delete;
  ThreadManagerPolicy& operator=(const ThreadManagerPolicy&) = delete;

  virtual ~ThreadManagerPolicy();

  // If a pointer to an ThreadManagerPolicy is passed to
  // ThreadManager::SetPolicy(), the method policy->Eval(state, &result)
  // will be called whenever a a decision must be made about whether to start a
  // new thread because there is work and no idle threads.  It should fill in
  // the fields of result with an indication of whether to create a thread, and
  // the number of milliseconds to delay before any attempt will be made to
  // create another thread (Eval() will not be called again on a given queue
  // until that delay has expired, though it may be called for other queues
  // within the same threadpool).  It may be called with an internal lock held;
  // therefore, it should return quickly, it may not block, and it may not call
  // back into this interface.  The ThreadManagerState struct contains
  // information about the state of just pool "pool_index" of the "pool_count"
  // pools within the ThreadManager, rather than the entire ThreadManager.
  virtual void Eval(const ThreadManagerState& state,
                    ThreadManagerAction* result) = 0;

 protected:
  ThreadManagerPolicy();
};

}  // namespace thread
#endif  // THIRD_PARTY_GLOOP_THREAD_THREAD_MANAGER_POLICY_H_
