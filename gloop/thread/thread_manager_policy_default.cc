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

// Implementation of the default thread-creation policy of ThreadManager

#include <algorithm>  // for max()
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "gloop/thread/thread_manager_policy.h"

namespace thread {

// The default ThreadManager policy.
class ThreadManagerPolicyDefault : public ThreadManagerPolicy {
 public:
  // The default policy needs to know the number of CPUs it should be using.
  // Normally, callers will use numcpus == &NumCPUs.
  explicit ThreadManagerPolicyDefault(int (*num_cpus)());

  // This type is neither copyable nor movable.
  ThreadManagerPolicyDefault(const ThreadManagerPolicyDefault&) = delete;
  ThreadManagerPolicyDefault& operator=(const ThreadManagerPolicyDefault&) =
      delete;

  virtual ~ThreadManagerPolicyDefault();
  virtual void Eval(const ThreadManagerState& state,
                    ThreadManagerAction* action);

 private:
  int (*num_cpus_)();           // returns number of number of CPUs to assume.
  enum { kMaxEpochMS = 1280 };  // max length of a measurement epoch
  // No finishing closures for this long may suggest deadlock even if some
  // threads appear active
  enum { kDeadlockDetectIntervalMS = 2000 };
  struct PerPoolHistory {
    PerPoolHistory()
        : epoch_length_ms(10),
          hi_water_mark(0),
          fast_create_up_to(0),
          zero_rate_since_ms(0) {}
    int32_t epoch_length_ms;     // current length of a measurement epoch
    int32_t hi_water_mark;       // max number of threads seen
    int32_t fast_create_up_to;   // willing to create thereads to this value
                                 // with little prompting
    int64_t zero_rate_since_ms;  // if rate==0, it's been zero since this time
    // Each element of history[] represents the state at some time.
    // We compare new data against history[1], which is some state recordsed
    // more than epochs ago.
    struct PerPoolHistoryEntry {
      PerPoolHistoryEntry()
          : rate(0), time_ms(0), closures_run(0), threads(0) {}
      int64_t rate;          // rate of closure completion per 1000s
      int64_t time_ms;       // time of sample in ms
      int64_t closures_run;  // total closures run
      int threads;           // threads in pool
    } history[2];
  };
  std::vector<PerPoolHistory> per_pool_;  // state per pool within ThreadManager
};

ThreadManagerPolicy* DefaultThreadManagerPolicy(int (*num_cpus)()) {
  return new ThreadManagerPolicyDefault(num_cpus);
}

ThreadManagerPolicyDefault::ThreadManagerPolicyDefault(int (*num_cpus)())
    : num_cpus_(num_cpus) {}

ThreadManagerPolicyDefault::~ThreadManagerPolicyDefault() {}

// Return a debug string containing various fields from state, with the rate
// and message msg[].
static std::string StateSummary(const ThreadManagerState& state, int64_t rate,
                                const char* msg) {
  return absl::StrFormat("%d/%d  queue %d  threads %d  rate %d  %s\n",
                         state.pool_index, state.pool_count, state.queue_length,
                         state.threads, static_cast<int64_t>(rate), msg);
}

// Default heuristic for thread creation
// L >= internal lock in ThreadManager
void ThreadManagerPolicyDefault::Eval(const ThreadManagerState& state,
                                      ThreadManagerAction* action) {
  if (static_cast<int64_t>(this->per_pool_.size()) <
      state.pool_count) {  // Create per-pool entries.
    this->per_pool_.resize(state.pool_count);
  }
  // Use entry for correct pool.
  PerPoolHistory* per_pool_history = &this->per_pool_[state.pool_index];
  absl::Span<PerPoolHistory::PerPoolHistoryEntry> history =
      per_pool_history->history;

  // Theory:
  //   We compute a rate of progress---the rate at which
  //   the queue is finishing closures.  We add more threads
  //   if adding threads seems to increase the rate, or if removing threads
  //   seems to decrease the rate.  (Threads are removed by another mechanism
  //   when idle for a long time.)
  //   In addition, when we create, we damp the creation rate by always adding
  //   a delay proportional to the number of threads created recently---we
  //   change the coefficient depending on how important it is to create
  //   threads rapidly.
  //
  //   We always add at least one thread per CPU we expect this pool to serve,
  //   and we add a thread if the rate falls to zero, with no idle or active
  //   threads.

  // Compute rate at which closures are completing per 1000s.
  int64_t closures_run = state.closures_run - history[1].closures_run;
  int64_t time_ms = state.time_ms - history[1].time_ms;
  int64_t rate0 = 0;
  if (time_ms > 0) {
    rate0 = (closures_run * 1000000) / time_ms;
  }
  int64_t rate1 = history[1].rate;  // get historical rate
  int64_t rate1_delta =
      rate1 >> 8;  // treat 1/8th of total as significant change

  if (state.threads > per_pool_history->hi_water_mark ||
      per_pool_history->fast_create_up_to == 0) {
    per_pool_history->hi_water_mark = state.threads;
    // We create threads fast up to close to a previous high water mark,
    // and up to two times the number of CPUs per pool.
    per_pool_history->fast_create_up_to = std::max(
        per_pool_history->hi_water_mark -
            (per_pool_history->hi_water_mark >> 3),
        (2 * (*this->num_cpus_)() + state.pool_count - 1) / state.pool_count);
  }

  action->create = false;  // we use desired_threads
  // By default do nothing.
  action->desired_threads =
      state.threads + state.create_pending - state.kill_pending;
  action->delay_ms = 200;                           // reconsider in 0.2s
  if ((rate0 == 0 && rate1 != 0) ||                 // transition to zero rate
      per_pool_history->zero_rate_since_ms == 0) {  // not yet initialized
    per_pool_history->zero_rate_since_ms = state.time_ms;
  }
  if (state.queue_length == 0) {  // nothing waiting---do nothing
    VLOG(3) << StateSummary(state, rate0, "nothing waiting; no new threads");
  } else if (state.threads < per_pool_history->fast_create_up_to) {
    action->desired_threads++;
    action->delay_ms = 1;  // allow fast thread creation
    VLOG(3) << StateSummary(state, rate0, "fast thread creation");
  } else if (state.threads > history[1].threads &&
             rate0 > rate1 + rate1_delta) {
    // Added threads, and it helped.  Add.
    action->desired_threads++;
    action->delay_ms = state.threads_since_last_exit >> 4;  // fast creation
    VLOG(3) << StateSummary(state, rate0, "adding threads increases rate");
  } else if (state.threads < history[1].threads &&
             rate0 < rate1 - rate1_delta) {
    // Removed threads and we slowed down.  Add.
    action->desired_threads++;
    // Throttle thread creation.  Assumption is we're not far from equilibrium.
    action->delay_ms = state.threads_since_last_exit << 4;
    VLOG(3) << StateSummary(state, rate0, "removing threads decreases rate");
  } else if (rate0 == 0 && state.idle == 0 && state.create_pending == 0) {
    // No closures flowing, no idle threads.
    if (state.active == 0) {  // nothing running
      action->desired_threads++;
      // fast thread creation
      action->delay_ms = state.threads_since_last_exit >> 4;
      VLOG(3) << StateSummary(state, rate0, "likely deadlock---create thread");
    } else if (per_pool_history->zero_rate_since_ms +
                   kDeadlockDetectIntervalMS <
               state.time_ms) {
      // Nothing has changed in kDeadlockDetectIntervalMS; assume deadlock
      action->desired_threads++;
      // fairly fast thread creation
      action->delay_ms = state.threads_since_last_exit >> 3;
      VLOG(3) << StateSummary(state, rate0,
                              "potential deadlock---create thread");
    } else {
      VLOG(3) << StateSummary(state, rate0,
                              "potential deadlock---no action yet");
    }
  } else {
    VLOG(3) << StateSummary(state, rate0, " no action");
  }

  if (state.time_ms - history[0].time_ms > per_pool_history->epoch_length_ms) {
    // history[0] is old enough to use as history[1] next time
    history[1] = history[0];
    history[0].rate = rate0;
    history[0].time_ms = state.time_ms;
    history[0].closures_run = state.closures_run;
    history[0].threads = state.threads;
    // Use short epochs at first, until we've been running a little while
    if (per_pool_history->epoch_length_ms * 2 < kMaxEpochMS) {
      per_pool_history->epoch_length_ms *= 2;
    }
  }
}

namespace {
class EagerThreadManagerPolicyClass : public thread::ThreadManagerPolicy {
 public:
  explicit EagerThreadManagerPolicyClass(int max_threads)
      : max_threads_(max_threads) {}

  // This type is neither copyable nor movable.
  EagerThreadManagerPolicyClass(const EagerThreadManagerPolicyClass&) = delete;
  EagerThreadManagerPolicyClass& operator=(
      const EagerThreadManagerPolicyClass&) = delete;
  virtual void Eval(const thread::ThreadManagerState& state,
                    thread::ThreadManagerAction* result) {
    int max_threads_per_pool;
    if (this->max_threads_ >= INT_MAX - state.pool_count + 1) {
      max_threads_per_pool = this->max_threads_ / state.pool_count;
    } else {
      max_threads_per_pool =
          (this->max_threads_ + state.pool_count - 1) / state.pool_count;
    }
    result->create = (state.threads < max_threads_per_pool);
    result->delay_ms = 0;
  }

 private:
  int max_threads_;
};
}  // anonymous namespace

ThreadManagerPolicy* EagerThreadManagerPolicy(int max_threads) {
  return new EagerThreadManagerPolicyClass(max_threads);
}

}  // namespace thread
