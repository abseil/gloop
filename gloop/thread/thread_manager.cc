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

// Implementation of ThreadManager, which is effectively an auto-sizing
// threadpool with an arbitrary number of feeder queues that may have limits
// or may be unbounded.

// Each ThreadManager instance is composed of a few independent pools to avoid
// lock contention.  Work (Closures) is given to one of these pools arbitrarily.

// Closures are given to the ThreadManager by a ManagedQueue. No more than N
// closures from a given ManagedQueue will be allowed to run in the
// ThreadManager at any given moment, where N is configurable for each
// ManagedQueue. Max queue size and watchdog timeout are also configurable for
// individual queues.

// A single overseer thread for the address space oversees all ThreadManager
// instances.  It is responsible for creating new threads and killing old ones
// (e.g., if they become idle for long periods). The overseer is created when
// the first ThreadManager is used, and never dies; there is never more than one
// in the process.  Worker threads are never moved from one pool to another
// (except by killing one and starting another).

// The overseer invokes ThreadManagerPolicy objects to determine if the
// number of threads should be increased or decreased. This is indicated in the
// ThreadManagerAction object returned by a policy. The ThreadManagerAction
// also affects the time between policy invocations, as specified by delay_ms.

// Data structures:
// ThreadManager::Rep
//   The internal representation of a ThreadManager. It contains one or more
//   (and always a power of 2) TMPools.   All existing ThreadManager::Reps
//   are references by the vector tm_vec so they can be found and managed
//   by a single overseer thread.
// TMPool
//   An independent thread pool, with a queue of work and a pool of threads
//   that automatically shrinks and grows.
// TMThread
//   Represents a thread in a pool.
// ManagedQueue::Rep
//   The internal representation of a ManagedQueue. It points to the
//   ThreadManager::Rep, and sends work to it.  It has a queue of work
//   (possibly of finite size) that holds work that cannot be given to a pool
//   because the ThreadManager is running too many threads on behalf of
//   this queue already.
// TMWork
//   An item of work (a closure) plus a pointer to the queue it was given to.
//   This is the unit that is queued in TMPool.

#include "gloop/thread/thread_manager.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/log_severity.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/callback.h"
#include "gloop/base/per-thread-sem.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/cpu_subcontainer.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/python_stack_size.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_manager_policy.h"
#include "gloop/thread/wait_state.h"
#include "gloop/thread/watchdog.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/functional/with_context.h"

ABSL_FLAG(int32_t, threadmanager_overseer_watchdog_s,
          IsDebuggerAttached() ? 100000000 : 120,
          "die if threadmanager overseer doesn't wake in this many secs");
ABSL_FLAG(int32_t, threadmanager_default_manager_pools, 0,
          "number of thread pools in the default threadmanager, 0 = auto");
ABSL_FLAG(bool, threadmanager_experimental_use_cpu_subcontainer, false,
          "if true use CPU subcontainer for threads created by this thread "
          "manager. This is an *experimental* feature limited to PD team "
          "only at this time (see b/194822211). Note that in order to use "
          "CPU subcontainer the --use_thread_subcontainers flag also needs "
          "to be enabled");
ABSL_DECLARE_FLAG(bool, use_thread_subcontainers);

// TODO: When the threadmanager overseer thread times out, its
// watchdogs will be triggered and causes the process to quit. This timeout
// can be a false alarm in some cases such as system clock jump during
// the boot time (b/239443956). In such situation we like to disable the
// watchdog for the overseer thread to prevent the process from quitting.
ABSL_FLAG(bool, threadmanager_overseer_enable_watchdog, true,
          "if true, the overseer thread of threadmanager uses a watchdog "
          "to detect its liveness. If false then the watchdog is disabled "
          "for the overseer thread. This is a *temporary* feature for "
          "addressing some overseer thread timeout issues (see b/243388788). "
          "Default value is true.");

ABSL_FLAG(bool, threadmanager_eager_gc_threads, true,
          "If true, threadmanager will eagerly garbage collect idle threads. "
          "This contrasts with the historical behavior of removing at most "
          "one thread per GC cycle and keeping 5 threads around always. "
          "NOTE: This flag is only intended for short-term rollbacks in case "
          "of an issue and will be enabled for all users then removed soon. ");

namespace thread {

// Max number of independent pools per ThreadManager.  This number should be
// increased if the number of CPUs in a box increases.
static const int kTMLogMaxPools = 4;  // 4 allows for 16 pools
static const int kTMMaxPools = 1 << kTMLogMaxPools;

// Threads idle for more than this many milliseconds get garbage collected.
static const int kTMIdleThresholdMS = 20 * 1000;

static const int kTMIdleKeep = 5;    // Idle threads to keep in a pool.
static const int kTMIdleRemove = 1;  // Max idle threads to remove from a pool
                                     // at a time.

// The overseer dallies this long for started threads to become active before
// checking a pool again.
static const int kTMStartupMS = 2;

// The overseer doesn't believe requests from the policy module to delay thread
// creation for more than this.
static const int kTMMaxDelayMS = 1 * 1000;

// The overseer wakes up after this many milliseconds even if not woken earlier.
static const int kTMOverseerSleepMS = 1000;

// Delay when overseer repeatedly is woken but finds nothing to do
static const int kTMOverseerIdleSleepMS = 50;

struct TMWork {                      // An entry in a queue of work
  absl::AnyInvocable<void() &&> cb;  // closure to run
  ManagedQueue::Rep* q_rep;          // internal queue on which work arrived
  uint32_t q_id;  // id of *q_rep (handles reuse of q_rep's address)
  // For efficiency we may sometimes not update the count of running items.
  // "counted" is true iff this work item has been added into the
  // "queue_running" count of q_rep/q_id.
  bool counted;
};

struct TMThread {
  Thread* t;          // The Thread; read-only after creation.
  bool die;           // Whether thread should die; under TMPool::pool_mu.
  bool on_idle_list;  // Whether in TMPool::idle_threads;
                      // under TMPool::pool_mu
  absl::CondVar not_idle_cv;  // Signalled when on_idle_list goes false.
  int64_t idle_since_ms;  // When the thread was last seen idle, or zero if the
                          // thread has recently done something; UNIX time in
                          // ms; under TMPool::pool_mu.
  TMWork work;  // work being done by this thread; under TMPool::pool_mu
};

struct ManagedQueue::Rep {
  // queue_external is given to CurrentExecutor() so it's always valid, even if
  // the original ManagedQueue is deleted.
  Rep() : queue_external(this) {}
  ManagedQueue queue_external;

  std::string queue_name;             // name; read-only after init
  ManagedQueueOptions queue_options;  // options; read-only after init
  ThreadManager::Rep* parent_rep;     // Rep of parent ThreadManager;
                                      // read-only after init
  uint32_t queue_id;                  // unique id; read-only after init
                      // queue_id is used to distinguish the work of one queue
                      // from that of another, deleted queue that
                      // had the same address.
  absl::Mutex queue_mu;  // protects following fields.
  int queue_refcount;    // reference count of Queues, not counting
                         // queue_external; under queue_mu
                         // currently, queue_refcount is at most 1.
  int queue_running;     // work items given to a pool; under queue_mu
                         // used only if queue_options.thread_limit != INT_MAX
                         // or WaitUntilCompleted() is active.  A work item is
                         // counted here if its "counted" field is true.
  int add_after_count;   // AddAfter() work items, not yet added to queue;
                         // under queue_mu
  int queue_waiters;  // threads blocked adding to "queue_work"; under queue_mu
  absl::CondVar
      queue_cv;  // signalled when queue_waiters > 0 and either
                 // queue_work.size() falls below queue_options.queue_limit
                 // or queue_running falls below queue_options.thread_limit
  std::deque<absl::AnyInvocable<void() &&>>
      queue_work;  // queue of pending work; under queue_mu
};

typedef std::deque<TMWork> TMWorkQueue;  // a queue of work

struct TMPool {
  char padding[32];     // Padding to put each pool on a different cache line.
  absl::Mutex pool_mu;  // Protects fields:
                        // pool_queue, thread_set, idle_threads,
                        // modifications to delay_until_ms,
                        // exiting_threads, closures_run,
                        // created, created_at_last_exit, running, exiting,
                        // destroyed, dying, TMThread::on_idle_list,
                        // TMThread::on_idle_list, TMThread idle_since_ms,
                        // overseer_saw_idle;
                        // pool_mu > tm_mu.
  TMWorkQueue pool_queue;  // ManagedQueue of work; under pool_mu.
  absl::flat_hash_set<TMThread*>
      thread_set;                       // all threads in pool; under pool_mu.
  std::vector<TMThread*> idle_threads;  // List of idle_threads; under pool_mu.
  int64_t last_idle_check_ms;        // abs time of last check for idle threads.
  std::atomic<bool> overseer_woken;  // whether pool has woken overseer
  std::atomic<int64_t> delay_until_ms;
  // Millisecond delay overseer should impose after
  // adding a thread to this queue; under pool_mu.
  std::vector<TMThread*>
      exiting_threads;  // exiting and not joined; under pool_mu.
  absl::CondVar
      thread_exit_cv;  // Signalled when exiting or destroyed increases
  absl::CondVar
      pool_queue_empty_cv;  // Signalled when pool_queue becomes empty.

  int64_t closures_run;  // total closures run; under pool_mu
  uint32_t created;      // Count of threads ever created; under pool_mu.
  uint32_t created_at_last_exit;  // Count of threads created when last thread
                                  // exited; under pool_mu.
  uint32_t running;        // Count of threads that have ever started running;
                           // under pool_mu.
  uint32_t exiting;        // Count of threads that have ever started to exit;
                           // under pool_mu.
  uint32_t destroyed;      // Count of threads that have ever finished exiting;
                           // under pool_mu.
  bool dying;              // The destructor has been called; under pool_mu.
  bool overseer_saw_idle;  // The overseer saw no work to do.
  int desired_active_threads;  // Number of threads desired to be active
                               // based on policy.
  int kill_pending;            // Number of threads that will be killed
  int create_pending;          // Number of tasks waiting to create
  std::atomic<int> blocked;    // Count of blocked threads.
                               // Value is maintained by PerThreadSem.
  int32_t pool_index;          // index of pool within ThreadManager::Rep::pool.
                               // read-only after init
  std::string
      name_prefix;  // Manager prefix + pool number; read-only after init.
};

struct ThreadManager::Rep {
  int n_pools;  // number of pools in use in pool[]; power of 2;
                // read-only after init.
  int index;    // Index of this Rep in tm_vec; under tm_mu.
  thread::Options thread_options;  // Read-only after init
  ThreadManagerPolicy* policy;     // thread creation policy; read-only after
                                   // init.
  WatchdogCallback watchdog_callback;  // Read-only after init
  std::atomic<int64_t> rand;           // random number generator; not locked.
  absl::Mutex rep_mu;                  // protects refcount
  absl::CondVar refcount_cv;           // when refcount drops to 1
  int refcount;                        // number of child Queues; under rep_mu
  uint32_t next_q_id;                  // queue_id for next q_rep; under rep_mu
  thread::CpuSubContainer* subcontainer;  // Thread scheduling container if not
                                          // nullptr.
  TMPool pool[kTMMaxPools];  // Individually locked with TMPool::pool_mu.
};

namespace {

// Kernel thread creation can be slow. Therefore, we avoid starting threads
// in the middle of critical sections and instead accumulate new threads to
// start in ThreadStarter and actually start the threads after leaving the
// critical section.
class ThreadStarter final {
 public:
  explicit ThreadStarter(WatchDog* absl_nullable watchdog)
      : watchdog_(watchdog) {}

  ~ThreadStarter() { CHECK(started_); }

  void Add(Thread* thread) { threads_.push_back(thread); }

  void Start() {
    for (auto* thread : threads_) {
      thread->Start();
      if (watchdog_) watchdog_->Alive();
    }
    started_ = true;
  }

 private:
  WatchDog* absl_nullable const watchdog_;
  std::vector<Thread*> threads_;
  bool started_ = false;
};

}  // namespace

// Protects tm_overseer_wakeup_requested.
ABSL_CONST_INIT static absl::Mutex tm_overseer_wakeup_mu(absl::kConstInit);
static bool tm_overseer_wakeup_requested ABSL_GUARDED_BY(tm_overseer_wakeup_mu);

// Protects tm_vec and init of tm_overseer, tm_num_cpus;
// tm_mu < TMPool::pool_mu.
ABSL_CONST_INIT static absl::Mutex tm_mu(absl::kConstInit);

static void TMOverseer();  // Overseer thread.

// TM instances; under tm_mu.
static std::vector<ThreadManager::Rep*>& TMVec() {
  static absl::NoDestructor<std::vector<ThreadManager::Rep*>> tm_vec;
  [[maybe_unused]] static bool need_init = [] {
    thread::Options overseer_options;
    overseer_options.set_stack_size(128 * 1024);
    Thread* t = new ClosureThread(overseer_options, "thread_manager_overseer",
                                  &TMOverseer);
    t->Start();
    return false;
  }();
  return *tm_vec;
}

// Default function for finding the number of CPUs.  Under tm_mu
static int (*tm_num_cpus)() = &::base::AvailableCPUs;

// Protects qu_set; qu_mu < queue_mu
ABSL_CONST_INIT static absl::Mutex qu_mu(absl::kConstInit);
static absl::flat_hash_set<ManagedQueue*>* qu_set ABSL_GUARDED_BY(qu_mu);

// Forward declarations.
static void TMQueueRepDelete(ThreadManager::Rep* rep, ManagedQueue::Rep* q_rep);

// Return whether *q_rep should be deleted.
// L >= q_rep->queue_mu
static inline bool ShouldDeleteQueueRep(ManagedQueue::Rep* q_rep) {
  // No need to check queue_waiters here because queue_waiters != 0 implies
  // queue_refcount != 0:
  DCHECK(q_rep->queue_refcount != 0 || q_rep->queue_waiters == 0);
  return (q_rep->queue_running == 0 && q_rep->queue_refcount == 0 &&
          q_rep->queue_work.empty() && q_rep->add_after_count == 0);
}

// If queue work->q_rep has more work to be done, place that work in work->cb
// and return true.   Otherwise return false.
// L < q_rep->queue_mu
static bool TMMoreWorkFromQueue(TMWork* work) {
  ManagedQueue::Rep* q_rep = work->q_rep;
  bool more_work = false;
  bool del_q_rep = false;
  q_rep->queue_mu.lock();
  if (q_rep->queue_running <= q_rep->queue_options.thread_limit &&
      !q_rep->queue_work.empty()) {
    more_work = true;
    work->cb = std::move(q_rep->queue_work.front());
    q_rep->queue_work.pop_front();
  } else {  // one fewer closure running from this queue
    q_rep->queue_running--;
    DCHECK_GE(q_rep->queue_running, 0);
    del_q_rep = ShouldDeleteQueueRep(q_rep);
  }
  if (q_rep->queue_waiters != 0) {  // need to wake anyone?
    if (q_rep->queue_running < q_rep->queue_options.thread_limit ||
        static_cast<int64_t>(q_rep->queue_work.size()) <
            q_rep->queue_options.queue_limit) {
      q_rep->queue_cv.Signal();
    }
  }
  q_rep->queue_mu.unlock();
  if (del_q_rep) {  // q_rep has no references, running threads, or work
    TMQueueRepDelete(q_rep->parent_rep, q_rep);
  }
  return more_work;
}

// Compute the number of idle threads. This is
// approximate since it doesn't consider "die" threads.
// L>=pool->pool_mu
static int TMIdleThreads(TMPool* pool) { return pool->idle_threads.size(); }

// Compute the number of active threads. This is
// approximate since it doesn't consider "die" threads.
// L >= pool->pool_mu
static int TMActiveThreads(TMPool* pool) {
  int idle = TMIdleThreads(pool);
  // Note that all idle threads are counted as blocked, and that blocked
  // is advisory only.
  int num_active =
      std::max(0, static_cast<int32_t>(pool->running - pool->exiting) -
                      pool->blocked.load(std::memory_order_relaxed));
  DVLOG(3) << "TMActive entry. pool=" << pool << ". run=" << pool->running
           << " idle=" << idle
           << " blocked=" << pool->blocked.load(std::memory_order_relaxed)
           << " exiting=" << pool->exiting << " dying=" << pool->dying
           << " num_active=" << num_active;
  return num_active;
}

// The body of a worker thread.
// L={}
static void TMWorker(ThreadManager::Rep* rep, TMPool* pool, TMThread* self) {
  VLOG(1) << "TMWorker entry. self=" << self << " rep=" << rep
          << " name_prefix=" << pool->name_prefix;
  Executor** exec_ptr = Executor::CurrentExecutorPointerInternal();
  int watchdog_timeout = INT_MAX;
  WatchDog watchdog("threadpool worker " + pool->name_prefix, watchdog_timeout);
  if (rep->watchdog_callback != nullptr) {
    // WatchDog takes ownership of this callback, it is re-wrapped to avoid
    // premature deletion of rep->watchdog_callback should the watchdog fire.
    watchdog.SetCallback(::util::functional::ToPermanentCallback(
        ::util::functional::FromCallback(rep->watchdog_callback)));
  }
  watchdog.Disable();
  pool->pool_mu.lock();
  pool->create_pending = std::max(0, pool->create_pending - 1);
  pool->running++;
  // Set the blocked thread counter after acquiring pool_mu to prevent
  // blocked from exceeding the number of running threads.
  absl::synchronization_internal::PerThreadSem::SetThreadBlockedCounter(
      &pool->blocked);
  while (!self->die) {
    if (pool->pool_queue.empty()) {
      WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);

      pool->idle_threads.push_back(self);
      self->idle_since_ms = 0;
      self->on_idle_list = true;
      while (self->on_idle_list) {
        DVLOG(2) << "TMWorker on_idle_list. self=" << self;
        self->not_idle_cv.Wait(&pool->pool_mu);
      }
    } else {  // work to do
      TMWork work = std::move(pool->pool_queue.front());
      pool->pool_queue.pop_front();
      // Keep a copy of work in thread struct so it can be seen by
      // WaitUntilComplete(). Don't copy work.cb, as we execute it here.
      self->work.q_id = work.q_id;
      self->work.counted = work.counted;
      self->work.q_rep = work.q_rep;
      if (work.cb == nullptr) {
        self->die = true;  // thread dies when given a null callback
        DCHECK_GE(pool->kill_pending, 1);
        pool->kill_pending--;  // Will die shortly
      } else {                 // a real callback to run
        if (pool->dying && pool->pool_queue.empty()) {
          // If this ThreadManager is being deleted, let the shutdown
          // code know that a pool's queue has drained.  It will
          // sleep again if other queues are non-empty until they
          // also generate signals.
          pool->pool_queue_empty_cv.SignalAll();
        }
        pool->pool_mu.unlock();
        *exec_ptr = &work.q_rep->queue_external;
        int time_limit_s = work.q_rep->queue_options.time_limit_s;
        if (time_limit_s != INT_MAX) {
          if (watchdog_timeout != time_limit_s) {
            watchdog_timeout = time_limit_s;
            watchdog.SetTimeout(watchdog_timeout);
          }
          watchdog.Alive();
          std::move(work.cb)();
          work.cb = nullptr;
          watchdog.Disable();
        } else {
          std::move(work.cb)();
          work.cb = nullptr;
        }
        // We do not count running items for unlimited queues unless somebody
        // is waiting for pending work to complete.  If the running item is
        // being counted, we have to use TMMoreWorkFromQueue() which decrements
        // the running count, and possibly also gives us more work to place on
        // pool_queue.  If the running item is not being counted, we can skip
        // TMMoreWorkFromQueue() for two reasons:
        //    (a) There is no running count to adjust
        //    (b) The queue is guaranteed to be unlimited, and therefore it
        //        should contain no pending work.  (Any added work would be
        //        immediately moved to the pool_queue in TMQueueAdd()).
        bool more_work = false;
        if (work.counted) {
          more_work = TMMoreWorkFromQueue(&work);
          pool->pool_mu.lock();
        } else {
          // Grab pool lock and recheck self->work.counted since another thread
          // may be concurrently turning on counting.
          pool->pool_mu.lock();
          if (self->work.counted) {
            // This queue doesn't normally maintain queue_running, but some
            // thread is calling WaitUntilComplete() and has set "counted" on
            // this work item.
            pool->pool_mu.unlock();
            more_work = TMMoreWorkFromQueue(&work);
            pool->pool_mu.lock();
          }
        }
        self->work.q_rep = nullptr;
        pool->closures_run++;
        if (more_work) {
          pool->pool_queue.push_back(std::move(work));
        }
      }
    }
  }
  *exec_ptr = nullptr;  // threadlocal destructors shouldn't see a pointer
  pool->exiting++;
  pool->exiting_threads.push_back(self);
  pool->thread_exit_cv.SignalAll();  // let shutdown code know about the exit
  VLOG(1) << "TMWorker exit. TMThread=" << self << " rep=" << pool->name_prefix;
  pool->pool_mu.unlock();
  // The pool can be deleted right after pool->pool_mu.Unlock() above.
}

// TMThread along with its TMPool; used by TMTakeExitingThreads, etc.
struct TMThreadWithPool {
  TMThreadWithPool(TMThread* thread, TMPool* pool)
      : thread(thread), pool(pool) {}

  TMThread* thread;
  TMPool* pool;
};

// Remove exiting threads from pool and append them to *exiting_threads; the
// caller should then call TMDestroyExitingThreads, after releasing locks.
// L >= pool->pool_mu
static void TMTakeExitingThreads(
    TMPool* pool, std::vector<TMThreadWithPool>* exiting_threads) {
  while (!pool->exiting_threads.empty()) {
    TMThread* thread = pool->exiting_threads.back();
    exiting_threads->push_back(TMThreadWithPool(thread, pool));
    pool->thread_set.erase(thread);
    pool->exiting_threads.pop_back();
  }
}

// Join and destroy all threads in *exiting_threads, which should have been
// returned previously by TMTakeExitingThreads.
//
// Separating this from TMTakeExitingThreads avoids contention on pool_mu, etc.
// since there can be delay between a thread declaring it's ready to exit and
// when it actually finishes exiting (and destroys all its thread locals, etc.).
//
// L={}
static void TMDestroyExitingThreads(
    std::vector<TMThreadWithPool>* exiting_threads, WatchDog* watchdog) {
  int num_in_pool = 0;
  for (size_t i = 0; i != exiting_threads->size(); i++) {
    TMThread* thread = (*exiting_threads)[i].thread;
    TMPool* pool = (*exiting_threads)[i].pool;
    thread->t->Join();
    if (watchdog) {
      // Join() might block, so give ourselves the best chance at not expiring
      // our watchdog.
      watchdog->Alive();
    }
    delete thread->t;
    delete thread;
    num_in_pool++;
    // Update pool state when we're done with this pool.
    if (i + 1 == exiting_threads->size() ||
        (*exiting_threads)[i + 1].pool != pool) {
      pool->pool_mu.lock();
      pool->destroyed += num_in_pool;
      pool->thread_exit_cv.SignalAll();  // let shutdown code know
      pool->pool_mu.unlock();
      num_in_pool = 0;
    }
  }
  exiting_threads->clear();
}

// Create a new worker for queue *pool.
// L >= pool->pool_mu
static void TMCreateWorker(ThreadManager::Rep* rep, TMPool* pool,
                           ThreadStarter* new_threads) {
  TMThread* t = new TMThread;
  t->t = new ClosureThread(rep->thread_options, pool->name_prefix,
                           absl::bind_front(TMWorker, rep, pool, t));
  if (rep->subcontainer != nullptr) {
    t->t->SetInitialCpuSubContainer(rep->subcontainer);
  }
  t->die = false;
  t->on_idle_list = false;
  t->idle_since_ms = 0;
  t->work.cb = nullptr;
  t->work.q_rep = nullptr;
  t->work.q_id = 0;
  t->work.counted = false;
  t->t->SetJoinable(true);
  new_threads->Add(t->t);
  pool->created++;
  pool->create_pending++;
  pool->thread_set.insert(t);
  VLOG(3) << "TMCreateWorker exit. t=" << t;
}

// Kill a single indexed thread
// L >= pool->pool_mu
static void TMKillIdleThread(TMPool* pool, int ti) {
  TMThread* thread_to_kill = pool->idle_threads[ti];
  VLOG(3) << "TMKillIdleThread entry. thread_to_kill=" << thread_to_kill;
  // move last element to here.
  pool->idle_threads[ti] = pool->idle_threads.back();
  pool->idle_threads.pop_back();
  thread_to_kill->die = true;
  thread_to_kill->on_idle_list = false;
  thread_to_kill->not_idle_cv.SignalAll();
  pool->delay_until_ms.store(
      0, std::memory_order_relaxed);  // don't delay for this queue
  pool->created_at_last_exit = pool->created;
}

// Kill threads in *pool that have been idle awhile.
// The check is performed at most once per second per pool.
// L >= pool->pool_mu
static void TMKillIdleThreads(TMPool* pool, int64_t now_ms) {
  pool->pool_mu.AssertHeld();
  if (pool->last_idle_check_ms + 1000 < now_ms) {
    pool->last_idle_check_ms = now_ms;
    int32_t removed_threads = 0;
    for (size_t ti = 0; ti != pool->idle_threads.size(); ti++) {
      TMThread* t = pool->idle_threads[ti];
      if (t->idle_since_ms + kTMIdleThresholdMS <= now_ms) {
        if (t->idle_since_ms == 0) {
          t->idle_since_ms = now_ms;
        } else if ((absl::GetFlag(FLAGS_threadmanager_eager_gc_threads)) ||
                   (removed_threads < kTMIdleRemove &&
                    pool->idle_threads.size() > kTMIdleKeep)) {
          // thread has been idle a long time, we have other idle threads, and
          // we haven't killed too many; kill it
          TMKillIdleThread(pool, ti);
          removed_threads++;
          ti--;  // iterate with this index again
        }
      }
    }
  }
  DVLOG(5) << "TMKillIdleThreads exit.";
}

// Run the thread creation policy for queue *pool in pool *rep.
// The current time is now_ms.   The result is place in *result.
// L >= pool->pool_mu
static void TMRunCreationPolicy(ThreadManager::Rep* rep, TMPool* pool,
                                int64_t now_ms,
                                ThreadManagerAction* action /*OUT*/) {
  DVLOG(3) << "TMRunCreationPolicy entry. pool=" << pool;
  pool->pool_mu.AssertHeld();
  ThreadManagerState state;
  state.pool_count = rep->n_pools;
  state.pool_index = pool->pool_index;
  state.time_ms = now_ms;
  state.closures_run = pool->closures_run;
  state.blocked = pool->blocked.load(std::memory_order_relaxed);
  state.queue_length = pool->pool_queue.size();
  state.threads = pool->created - pool->exiting;
  state.active = TMActiveThreads(pool);
  state.idle = TMIdleThreads(pool);
  state.threads_since_last_exit = pool->created - pool->created_at_last_exit;
  state.kill_pending = pool->kill_pending;
  state.create_pending = pool->create_pending;
  // Run policy and bound its result.
  action->create = true;
  action->delay_ms = 20;  // default value
  action->desired_threads = -1;
  rep->policy->Eval(state, action);
  CHECK_GE(action->delay_ms, 0);
  CHECK_EQ(action->desired_threads >= 0 && action->create, false);
  bool fixed_thread_policy = action->desired_threads >= 0;
  bool boolean_policy = !fixed_thread_policy;
  if (action->delay_ms < 1 && boolean_policy) {
    action->delay_ms = 1;
  } else if (action->delay_ms > kTMMaxDelayMS) {
    action->delay_ms = kTMMaxDelayMS;
  }
  DVLOG(3) << "TMRunCreationPolicy exit. pool=" << pool
           << " action.create=" << action->create
           << " action.delay_ms=" << action->delay_ms
           << " action.desired_threads=" << action->desired_threads;
}

// Add the (cb, q_rep) tuple to *pool, indicating that *q_rep has queued *cb to
// be run.  Return whether the overseer thread should be woken to create a new
// thread.   Flags:
enum { kAddAtBeginning = 0x1 };  // put work at start of queue, not end
enum { kCountWork = 0x2 };       // count the work in queue_running
// L >= pool->pool_mu
static bool TMAddToPool(absl::AnyInvocable<void() &&> cb,
                        ManagedQueue::Rep* q_rep, TMPool* pool, int flags) {
  bool wake_overseer = false;
  bool was_no_work = pool->pool_queue.empty();
  TMWork work;
  work.cb = std::move(cb);
  work.q_rep = q_rep;
  if (q_rep == nullptr) {
    work.q_id = 0;
  } else {
    work.q_id = q_rep->queue_id;
  }
  work.counted = ((flags & kCountWork) != 0);
  if ((flags & kAddAtBeginning) != 0) {
    pool->pool_queue.push_front(std::move(work));
  } else {
    pool->pool_queue.push_back(std::move(work));
  }
  if (!pool->idle_threads.empty()) {  //  an idle thread exists
    TMThread* t = pool->idle_threads.back();
    pool->idle_threads.pop_back();
    t->on_idle_list = false;
    t->not_idle_cv.SignalAll();
  } else {  // wake overseer if pool_queue transitioned from empty to non-empty
            // there are no idle threads and none just starting.
    wake_overseer =
        (was_no_work &&
         (pool->running == pool->created - pool->exiting));  // No idle threads
  }
  return wake_overseer;
}

static void TMQueueKillThreads(TMPool* pool, int to_kill) {
  DCHECK_GE(to_kill, 0);
  if (to_kill > 0) {
    pool->kill_pending += to_kill;
    for (int i = 0; i < to_kill; i++) {
      TMAddToPool(nullptr, nullptr, pool, kAddAtBeginning);
    }
  }
}

// Perform oversight for a single pool associated with *rep.
// In particular, if it looks like threads should be added
// all the policy module and do what it recommends.
// If threads have been idle for a long time, delete some.
// Exited threads are appended to *exiting_threads, and the
// caller should pass them to TMDestroyExitingThreads after
// releasing locks to avoid delays due to slow thread exits.
// Returns the time (in milliseconds from the epoch) that
// indicates when this pool should be checked again.
// Requires that now_ms be the current time in milliseconds.
// Called by TMOverseer()
// L >= tm_mu,  L < pool->pool_mu
static int64_t TMOverseePoolInterval(
    ThreadManager::Rep* rep, TMPool* pool, WatchDog* watchdog, int64_t now_ms,
    bool* ignore_wakeup_requests,
    std::vector<TMThreadWithPool>* exiting_threads) {
  int64_t pool_delay_until_ms =
      pool->delay_until_ms.load(std::memory_order_relaxed);
  if (pool_delay_until_ms <= now_ms) {
    ThreadStarter new_threads(watchdog);
    pool->pool_mu.lock();
    pool->overseer_woken.store(false, std::memory_order_relaxed);
    if (pool->pool_queue.empty() || !pool->idle_threads.empty()) {
      // no work to do, or we already have threads to do it
      TMKillIdleThreads(pool, now_ms);
      pool_delay_until_ms = 0;
      if (pool->overseer_saw_idle) {  // delay if it keeps happening
        pool_delay_until_ms = now_ms + kTMOverseerIdleSleepMS;
      }
      pool->overseer_saw_idle = true;
    } else {
      pool->overseer_saw_idle = false;
      if (pool->running == pool->created) {  // work to do, but no idle or
                                             // starting threads
        ThreadManagerAction action;
        TMRunCreationPolicy(rep, pool, now_ms, &action);
        pool_delay_until_ms = now_ms + action.delay_ms;
        if (action.create) {
          TMCreateWorker(rep, pool, &new_threads);
          *ignore_wakeup_requests = true;  // so we won't be woken prematurely
        } else if (action.desired_threads >= 0) {
          int32_t current_threads =
              static_cast<int32_t>(pool->created - pool->exiting) +
              pool->create_pending - pool->kill_pending;
          for (int i = current_threads; i < action.desired_threads; i++) {
            TMCreateWorker(rep, pool, &new_threads);
          }
          if (current_threads - action.desired_threads > 0) {
            TMQueueKillThreads(pool, current_threads - action.desired_threads);
          }
          *ignore_wakeup_requests = true;  // so we won't be woken prematurely
        }
      } else {
        // We have work and have created a thread that has not yet been
        // scheduled.  We wait for it rather than creating another thread.
        pool_delay_until_ms = now_ms + kTMStartupMS;
        *ignore_wakeup_requests = true;  // so we won't be woken prematurely
      }
    }
    TMTakeExitingThreads(pool, exiting_threads);
    pool->delay_until_ms.store(pool_delay_until_ms, std::memory_order_relaxed);
    pool->pool_mu.unlock();
    new_threads.Start();
  }
  return pool_delay_until_ms;
}

// Overseer's watchdog timeout routine.  Print a message about the likely cause
// before calling the watchdog's default TimedOut() routine.
static void TMOverseerTimeout(WatchDog* w) {
  const char* suggestion = "";
  LOG(ERROR) << "ThreadManager overseer was not scheduled for " << w->timeout()
             << " seconds." << suggestion;
  w->TimedOut();
}

constexpr double SanitizerSlowdown() {
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
  return 50.0;
#else
  return 1.0;
#endif
}

// Body of the overseer thread, which starts and kills threads as required.
// It calls TMOverseePool() for each pool to process.
// L={}
// Requires tm_mu < pool_mu
static void TMOverseer() {
  VLOG(3) << "TMOverseer entry.";
  std::unique_ptr<WatchDog> watchdog = nullptr;
  if (absl::GetFlag(FLAGS_threadmanager_overseer_enable_watchdog)) {
    watchdog = std::make_unique<WatchDog>("ThreadManager overseer",
                                          WatchDog::DefaultTimeout());
    watchdog->SetCallback(
        ::util::functional::ToPermanentCallback(&TMOverseerTimeout));
  }
  std::vector<TMThreadWithPool> exiting_threads;
  for (;;) {
    // watchdog checks that overseer doesn't die. The timeout must
    // exceed kTMOverseerSleepMS ms, the overseer's max sleep time.
    //
    // The timeout is continually reset here to respect changes to
    // FLAGS_threadmanager_overseer_watchdog_s and because some programs may
    // start a thread manager before flags are parsed.
    if (watchdog) {
      absl::Duration watchdog_timeout =
          absl::Seconds(
              absl::GetFlag(FLAGS_threadmanager_overseer_watchdog_s)) *
              SanitizerSlowdown() +
          absl::Milliseconds(kTMOverseerSleepMS);
      watchdog->SetTimeoutDuration(watchdog_timeout);
    }
    tm_mu.lock();
    int64_t now_ms = absl::ToUnixMillis(absl::Now());
    int64_t delay_until_ms = now_ms + kTMOverseerSleepMS;
    bool ignore_wakeup_requests = false;
    for (ThreadManager::Rep* rep : TMVec()) {
      DVLOG(5) << "TMOverseer for_Rep. repi=" << (&rep - &TMVec()[0]);
      for (TMPool& pool : absl::MakeSpan(rep->pool, rep->n_pools)) {
        DVLOG(5) << "TMOverseer. Check policy for pool=" << (&pool - rep->pool);
        int64_t pool_delay_until_ms =
            TMOverseePoolInterval(rep, &pool, watchdog.get(), now_ms,
                                  &ignore_wakeup_requests, &exiting_threads);
        if (pool_delay_until_ms != 0 && pool_delay_until_ms < delay_until_ms) {
          delay_until_ms = pool_delay_until_ms;
        }
      }
    }
    tm_mu.unlock();
    // Destroy any exiting threads now rather than delaying until after the
    // overseer sleep below.
    TMDestroyExitingThreads(&exiting_threads, watchdog.get());
    DCHECK(exiting_threads.empty());
    absl::Time deadline = absl::FromUnixMillis(delay_until_ms);
    if (watchdog) {
      watchdog->Alive();
    }

    WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
    if (ignore_wakeup_requests) {
      absl::SleepFor(deadline - absl::Now());
      // Clear any ignored wakeup request, since the overseer is waking up now.
      tm_overseer_wakeup_mu.lock();
      tm_overseer_wakeup_requested = false;
      tm_overseer_wakeup_mu.unlock();
    } else {
      // Wait until a wakeup request or a timeout.
      tm_overseer_wakeup_mu.LockWhenWithDeadline(
          absl::Condition(&tm_overseer_wakeup_requested), deadline);
      tm_overseer_wakeup_requested = false;
      tm_overseer_wakeup_mu.unlock();
    }
    if (watchdog) {
      watchdog->Alive();
    }
  }
}

// Wake the overseer thread.
// L < tm_overseer_wakeup_mu
static void TMWakeOverseer() {
  tm_overseer_wakeup_mu.lock();
  tm_overseer_wakeup_requested = true;
  tm_overseer_wakeup_mu.unlock();
}

// Register *rep with the overseer; start the overseer if necessary.
// L < tm_mu
static void TMEnsureRegisteredWithOverseer(ThreadManager::Rep* rep) {
  tm_mu.lock();
  if (rep->index == -1) {
    rep->index = TMVec().size();
    TMVec().push_back(rep);
  }
  tm_mu.unlock();
}

// Called by the constructor
// L < tm_mu
static ThreadManager::Rep* TMRepNew(absl::string_view name_prefix,
                                    const ManagerOptions& options) {
  ThreadManager::Rep* rep = new ThreadManager::Rep;
  rep->n_pools = 1;
  if (options.n_pools >= 1) {
    // round up n_pools to power of two no greater than kTMMaxPools
    for (rep->n_pools = 1;
         rep->n_pools < options.n_pools && rep->n_pools < kTMMaxPools;
         rep->n_pools <<= 1) {
    }
  }
  rep->index = -1;  // -1 means not known to overseer; no queue created yet
  rep->rand.store(reinterpret_cast<uintptr_t>(rep),
                  std::memory_order_relaxed);  // init arbitrarily
  rep->thread_options = options.thread_options;
  rep->watchdog_callback = options.get_watchdog_callback();
  if (options.policy != nullptr) {
    rep->policy = options.policy;
  } else {
    tm_mu.lock();
    int (*num_cpus)() = tm_num_cpus;
    tm_mu.unlock();
    rep->policy = DefaultThreadManagerPolicy(num_cpus);
  }
  rep->refcount = 0;
  rep->next_q_id = 0;
  rep->subcontainer = nullptr;
  if (absl::GetFlag(FLAGS_threadmanager_experimental_use_cpu_subcontainer)) {
    if (!absl::GetFlag(FLAGS_use_thread_subcontainers)) {
      LOG(WARNING) << "Attempt to use CPU subcontainers but the "
                   << "--use_thread_subcontainers flag is not set";

    } else {
      rep->subcontainer = thread::CpuSubContainer::Create(
          options.thread_options, std::string(name_prefix));
    }
  }
  for (int pool_i = 0; pool_i != rep->n_pools; pool_i++) {
    TMPool* pool = &rep->pool[pool_i];
    pool->last_idle_check_ms = 0;
    pool->delay_until_ms.store(0, std::memory_order_relaxed);
    pool->overseer_woken.store(false, std::memory_order_relaxed);
    pool->overseer_saw_idle = false;
    pool->closures_run = 0;
    pool->blocked.store(0, std::memory_order_relaxed);
    pool->created = 0;
    pool->created_at_last_exit = 0;
    pool->running = 0;
    pool->exiting = 0;
    pool->destroyed = 0;
    pool->dying = false;
    pool->desired_active_threads = -1;  // Disables specifying active
                                        // threads unless enabled by
                                        // policy
    pool->kill_pending = 0;
    pool->create_pending = 0;
    pool->pool_index = pool_i;
    pool->name_prefix = absl::StrFormat("%s-%d", name_prefix, pool_i);
  }
  return rep;
}

// Common destructor code.
// Delete *rep; called when rep->refcount falls to zero.
// L < rep->pool[*].pool_mu, tm_mu
static void TMRepDelete(ThreadManager::Rep* rep) {
  DCHECK_EQ(rep->refcount, 0);
  // Let the work queues drain
  for (int pool_i = 0; pool_i != rep->n_pools; pool_i++) {
    TMPool* pool = &rep->pool[pool_i];
    pool->pool_mu.lock();
    pool->dying = true;
    while (!pool->pool_queue.empty()) {
      pool->pool_queue_empty_cv.Wait(&pool->pool_mu);
    }
    pool->pool_mu.unlock();
  }
  // Stop the overseer creating more threads.
  tm_mu.lock();
  int repi = rep->index;
  if (repi != -1) {  // if queues were created
    CHECK(rep == TMVec()[repi]);
    TMVec()[repi] = TMVec().back();
    TMVec()[repi]->index = repi;
    TMVec().pop_back();
  }  // else no queues were created; never registered with overseer
  tm_mu.unlock();
  // Tell all the threads to die; then wait for them to do so.
  std::vector<TMThreadWithPool> exiting_threads;
  for (int pool_i = 0; pool_i != rep->n_pools; pool_i++) {
    TMPool* pool = &rep->pool[pool_i];
    pool->pool_mu.lock();
    // For each created but not yet exiting thread.
    if (pool->created - pool->exiting > 0) {
      // BTW. It's ok to send more null closures than we need.
      // Any extras will simply be deleted---they have no allocated memory
      // associated with them aside from the deque's metadata.
      TMQueueKillThreads(pool, pool->created - pool->exiting);
    }
    while (pool->exiting != pool->created) {
      pool->thread_exit_cv.Wait(&pool->pool_mu);
    }
    TMTakeExitingThreads(pool, &exiting_threads);
    pool->pool_mu.unlock();
  }
  TMDestroyExitingThreads(&exiting_threads, nullptr);
  // It's possible the overseer thread is also destroying some threads from this
  // instance, so wait until any remaining threads are destroyed.
  for (int pool_i = 0; pool_i != rep->n_pools; pool_i++) {
    TMPool* pool = &rep->pool[pool_i];
    pool->pool_mu.lock();
    while (pool->destroyed != pool->created) {
      pool->thread_exit_cv.Wait(&pool->pool_mu);
    }
    pool->pool_mu.unlock();
  }

  delete rep->policy;
  delete rep->subcontainer;
  delete rep;
}

// Return a pointer to a randomly-chosen pool within the thread manager.
// L=*
static TMPool* TMRandomPool(ThreadManager::Rep* rep) {
  int pool_i = 0;
  if (rep->n_pools != 1) {
    DCHECK_EQ((rep->n_pools & (rep->n_pools - 1)), 0)
        << "rep->n_pools is " << rep->n_pools
        << " but should be a power of two.";
    // We use a cheap random number generator without locking.
    // The parameters are stolen from lrand48().
    uint64_t x =
        rep->rand.load(std::memory_order_relaxed) * 0x5deece66dULL + 11;
    rep->rand.store(x, std::memory_order_relaxed);
    pool_i = (x >> 32) & (rep->n_pools - 1);
  }
  return &rep->pool[pool_i];
}

namespace {                   // flags to TMQueueAdd()
enum { kAddBlock = 0x1 };     // if necessary, block until add is possible
enum { kDecAddAfter = 0x2 };  // decrement the add_after_count
}  // namespace

// Check if we need to wake the overseer thread after adding to the pool.
// pool->pool_mu is locked.
static bool TMNeedWakeOverseer(TMPool* pool) {
  // The test against pool->delay_until_ms ensures that we don't wake
  // the overseer unless there's some chance it will do something for us.
  // The test against pool->overseer_woken squashes multiple wakeups.
  if (!pool->overseer_woken.load(std::memory_order_relaxed) &&
      pool->delay_until_ms.load(std::memory_order_relaxed) <=
          ToUnixMillis(absl::Now())) {
    pool->overseer_woken.store(true, std::memory_order_relaxed);
    return true;
  }
  return false;
}

// Add *cb if a queue size of queue_limit would not be exceeded.  If (flags &
// kAddBlock) != 0, the routine waits until queue_limit would not be exceeded.
// If (flags & kDecAddAfter) != 0, the routine decrements
// q_rep->add_after_count.  Requires that (flags & kDecAddAfter)==0 if
// q_rep->queue_options.thread_limit == INT_MAX.  Return whether *cb was added
// to the queue.
// L < rep->pool[*].pool_mu, q_rep->queue_mu, tm_mu
static bool TMQueueAdd(ThreadManager::Rep* rep,
                       absl::AnyInvocable<void() &&> cb,
                       ManagedQueue::Rep* q_rep, int queue_limit, int flags) {
  DVLOG(3) << "TMQueueAdd entry.";
  CHECK(cb != nullptr);
  // Store the current context, per the Schedule contract.
  cb = util::functional::WithCurrentContext(std::move(cb));
  bool added = false;
  bool wake_overseer = false;
  TMPool* pool = nullptr;
  do {
    if (q_rep->queue_options.thread_limit == INT_MAX &&
        (flags & kDecAddAfter) == 0) {
      // Optimize case where the number of running threads is unlimited:
      // acquire only the pool lock, rather than both the queue lock and the
      // pool lock.   This is possible by not maintaining queue_running
      // on such queues unless WaitUntilComplete() is called.
      // We omit this optimization for work added with AddAfter().  In that
      // case, the queue_running count is used to tell when to delete
      // q_rep if the queue has been deleted by the client.
      pool = TMRandomPool(rep);
      pool->pool_mu.lock();
      wake_overseer = TMAddToPool(std::move(cb), q_rep, pool, 0) &&
                      TMNeedWakeOverseer(pool);
      pool->pool_mu.unlock();
      // The pool can be deleted right after pool->pool_mu.Unlock() above.
      pool = nullptr;
      added = true;
    } else {
      q_rep->queue_mu.lock();
      if ((flags & kDecAddAfter) != 0) {
        q_rep->add_after_count--;
      }
      if (q_rep->queue_running < q_rep->queue_options.thread_limit) {
        // we may add to the pool---put work there directly
        CHECK(q_rep->queue_work.empty());
        q_rep->queue_running++;
        q_rep->queue_mu.unlock();
        pool = TMRandomPool(rep);
        pool->pool_mu.lock();
        wake_overseer = TMAddToPool(std::move(cb), q_rep, pool, kCountWork) &&
                        TMNeedWakeOverseer(pool);
        pool->pool_mu.unlock();
        // The pool can be deleted right after pool->pool_mu.Unlock() above.
        pool = nullptr;
        added = true;
      } else if (static_cast<int64_t>(q_rep->queue_work.size()) <
                 queue_limit) {                      // room in queue
        q_rep->queue_work.push_back(std::move(cb));  // append work to queue
        q_rep->queue_mu.unlock();
        added = true;
      } else {
        if ((flags & kAddBlock) != 0) {  // wait till we can dispose of work
          q_rep->queue_waiters++;
          q_rep->queue_cv.Wait(&q_rep->queue_mu);
          q_rep->queue_waiters--;
        }
        q_rep->queue_mu.unlock();
      }
    }
  } while ((flags & kAddBlock) != 0 && !added);
  if (wake_overseer) {
    TMWakeOverseer();
  }
  DVLOG(3) << "TMQueueAdd exit.";
  return added;
}

// Remove *q_rep and its reference from *rep.
// L < rep->rep_mu, tm_mu, pool_mu
static void TMQueueRepDelete(ThreadManager::Rep* rep,
                             ManagedQueue::Rep* q_rep) {
  VLOG(3) << "TMQueueRepDelete entry.";
  rep->rep_mu.lock();
  rep->refcount--;
  CHECK_GE(rep->refcount, 0);
  if (rep->refcount == 0) {
    rep->refcount_cv.SignalAll();
  }
  rep->rep_mu.unlock();
  delete q_rep;
  VLOG(3) << "TMQueueRepDelete exit.";
}

// Decrement the reference count of *q_rep.  If the refcount falls to
// zero and the queue has no queued or outstanding work, delete *q_rep.
// L < q_rep->queue_mu, q_rep->parent_rep->rep_mu, tm_mu, pool_mu
static void TMQueueRepUnref(ManagedQueue::Rep* q_rep) {
  q_rep->queue_mu.lock();
  q_rep->queue_refcount--;
  CHECK_GE(q_rep->queue_refcount, 0);
  bool del = ShouldDeleteQueueRep(q_rep);
  q_rep->queue_mu.unlock();
  if (del) {  // discard *q_rep
    TMQueueRepDelete(q_rep->parent_rep, q_rep);
  }
}

// -----------------------------------------------------------------------
// public interface

ThreadManager::ThreadManager(absl::string_view name_prefix,
                             const ManagerOptions& options)
    : rep_(TMRepNew(name_prefix, options)) {}

// L < tm_mu, this->rep_->pool[*].pool_mu, this->rep->rep_mu
ThreadManager::~ThreadManager() {
  VLOG(3) << "~ThreadManager entry";
  ThreadManager::Rep* rep = this->rep_;
  rep->rep_mu.lock();
  while (rep->refcount != 0) {  // wait until all references go away
    rep->refcount_cv.Wait(&rep->rep_mu);
  }
  rep->rep_mu.unlock();
  TMRepDelete(rep);
  VLOG(3) << "~ThreadManager exit. Rep=" << rep;
}

// L < this->rep_->rep_mu, tm_mu
ManagedQueue* ThreadManager::NewQueue(
    absl::string_view name, const ManagedQueueOptions& queue_options) {
  ThreadManager::Rep* rep = this->rep_;
  TMEnsureRegisteredWithOverseer(rep);  // need an overseer to hand out a queue
  rep->rep_mu.lock();
  ManagedQueue::Rep* q_rep;
  q_rep = new ManagedQueue::Rep;
  q_rep->queue_name = std::string(name);
  q_rep->queue_options = queue_options;
  q_rep->parent_rep = rep;
  q_rep->queue_id = rep->next_q_id++;
  q_rep->queue_refcount = 1;
  q_rep->queue_running = 0;
  q_rep->add_after_count = 0;
  q_rep->queue_waiters = 0;
  rep->refcount++;
  rep->rep_mu.unlock();
  // queue_options is read-only after this point
  return new ManagedQueue(q_rep);
}

std::vector<ManagedQueueStats> ThreadManager::QueueStats()
    ABSL_LOCKS_EXCLUDED(qu_mu) {
  std::vector<ManagedQueueStats> stats;
  qu_mu.lock();
  if (qu_set != nullptr) {
    for (ManagedQueue* q : *qu_set) {
      stats.push_back(q->Stats());
    }
  }
  qu_mu.unlock();
  return stats;
}

// The default ThreadManager and ManagedQueue

// The default ThreadManager and ManagedQueue are initilized under
// tm_default_once
static absl::once_flag tm_default_once;
static ThreadManager* tm_default_thread_manager;  // the default ThreadManager
static ManagedQueue* tm_default_queue;            // the default ManagedQueue

// Create the default thread manager and queue.  Called using GoogleOnceInit().
static void TMMakeDefault() {
  ManagerOptions manager_options;
  manager_options.n_pools =
      absl::GetFlag(FLAGS_threadmanager_default_manager_pools);
  tm_default_thread_manager =
      new ThreadManager("default_ThreadManager", manager_options);
  tm_default_queue = tm_default_thread_manager->NewQueue("default_queue",
                                                         ManagedQueueOptions());
}

ThreadManager* DefaultManager() {
  absl::call_once(tm_default_once, &TMMakeDefault);
  return tm_default_thread_manager;
}

ManagedQueue* DefaultQueue() {
  absl::call_once(tm_default_once, &TMMakeDefault);
  return tm_default_queue;
}

// Set the default version of NumCPUs()
// L < tm_mu
void ThreadManager::SetDefaultNumCPUs(int (*num_cpus)()) {
  tm_mu.lock();
  tm_num_cpus = num_cpus;
  tm_mu.unlock();
}

// -----------------------------------------------------------------------
// The implementation of a ManagedQueue---the Executor handed out by the
// ThreadManager

// If *work came from queue *q_rep but was not counted in q_rep->q_running,
// count it now, and mark the work as counted.
// L < q_rep->queue_mu
static void TMCountWorkFromQueue(TMWork* work, ManagedQueue::Rep* q_rep) {
  // We must check the queue_id as well as checking that the address
  // q_rep is the same because the queue at work->q_rep could have
  // been deleted and another recreated at the same address.
  if (work->q_rep == q_rep && work->q_id == q_rep->queue_id && !work->counted) {
    work->counted = true;
    q_rep->queue_mu.lock();
    q_rep->queue_running++;
    q_rep->queue_mu.unlock();
  }
}

// For every pool in the thread manager associated with queue *q_rep, mark work
// in that pool associated with q_rep as "counted".  This is done when the
// client calls WaitUntilComplete(), or when *q_rep is about to be destroyed.
// L < this->q_rep_->parent_rep->pool[*]->pool_mu
static void TMCountAllWorkFromQueue(ManagedQueue::Rep* q_rep) {
  ThreadManager::Rep* parent_rep = q_rep->parent_rep;
  for (int pool_i = 0; pool_i != parent_rep->n_pools; pool_i++) {
    TMPool* pool = &parent_rep->pool[pool_i];
    pool->pool_mu.lock();
    // for every item of work in the pool's queue
    for (TMWorkQueue::iterator it = pool->pool_queue.begin();
         it != pool->pool_queue.end(); ++it) {
      TMCountWorkFromQueue(&*it, q_rep);
    }
    // for every item of work currently being processed by a thread
    for (auto it = pool->thread_set.begin(); it != pool->thread_set.end();
         ++it) {
      TMCountWorkFromQueue(&(*it)->work, q_rep);
    }
    pool->pool_mu.unlock();
  }
}

ManagedQueue::ManagedQueue(ManagedQueue::Rep* q_rep) : q_rep_(q_rep) {
  if (this != &this->q_rep_->queue_external) {
    qu_mu.lock();
    if (qu_set == nullptr) {
      qu_set = new absl::flat_hash_set<ManagedQueue*>;
    }
    qu_set->insert(this);
    qu_mu.unlock();
  }
}

// L < q_rep->queue_mu, q_rep->parent_rep->rep_mu, tm_mu, pool_mu
ManagedQueue::~ManagedQueue() {
  if (this != &this->q_rep_->queue_external) {  // Don't unref queue_external.
    qu_mu.lock();
    size_t erased = qu_set->erase(this);
    DCHECK_EQ(1, erased);
    qu_mu.unlock();

    // We use queue_running as a reference count to control the deletion
    // of the queue data structure.  However for infinite queues, we do
    // not update the running count to reduce cost.  Make sure that all
    // such updates are done before we unref the queue to prevent
    // uncounted work from dereferencing a deleted queue.
    TMCountAllWorkFromQueue(this->q_rep_);
    TMQueueRepUnref(this->q_rep_);  // compensates for ref inc in NewQueue().
  }
}

std::string ManagedQueue::name() const { return this->q_rep_->queue_name; }

ManagedQueueOptions ManagedQueue::queue_options() const {
  return this->q_rep_->queue_options;
}

// L < rep->pool[*].pool_mu, q_rep->queue_mu, tm_mu
void ManagedQueue::Schedule(absl::AnyInvocable<void() &&> callback) {
  ManagedQueue::Rep* q_rep = this->q_rep_;
  TMQueueAdd(q_rep->parent_rep, std::move(callback), q_rep,
             q_rep->queue_options.queue_limit, kAddBlock);
}

// L < rep->pool[*].pool_mu, q_rep->queue_mu, tm_mu
bool ManagedQueue::TrySchedule(absl::AnyInvocable<void() &&> callback) {
  ManagedQueue::Rep* q_rep = this->q_rep_;
  return TMQueueAdd(q_rep->parent_rep, std::move(callback), q_rep,
                    q_rep->queue_options.queue_limit, 0);
}

// TMQueueAdd(), without blocking or testing for the queue limit,
// then decrement add_after_count----used with AddAfter().
// L < rep->pool[*].pool_mu, q_rep->queue_mu, tm_mu
static void TMQueueAddAlways(ThreadManager::Rep* rep, Closure* cb,
                             ManagedQueue::Rep* q_rep) {
  // Add, and request that add_after_count be decremented.
  CHECK(TMQueueAdd(rep, util::functional::FromCallback(cb), q_rep, INT_MAX,
                   kDecAddAfter));
}

void ManagedQueue::ScheduleAt(absl::Time when,
                              absl::AnyInvocable<void() &&> callback) {
  Closure* cb = util::functional::ToCallback(std::move(callback));
  ManagedQueue::Rep* q_rep = this->q_rep_;
  // Use TMQueueAddAlways() to avoid blocking the TimedCall thread.
  auto delayed_cb =
      absl::bind_front(&TMQueueAddAlways, q_rep->parent_rep, cb, q_rep);
  q_rep->queue_mu.lock();
  q_rep->add_after_count++;
  q_rep->queue_mu.unlock();
  this->Delay(when - absl::Now(), delayed_cb);
}

// L < this->q_rep_->queue_mu
int ManagedQueue::num_pending_closures() const {
  ManagedQueue::Rep* q_rep = this->q_rep_;
  // The result is the sum from the ManagedQueue and the pool queues.
  // The ManagedQueue value can be read from a word.
  q_rep->queue_mu.lock();
  int num_on_queue = q_rep->queue_work.size();
  q_rep->queue_mu.unlock();
  // The pool queue values for this ManagedQueue must be obtained by looking at
  // the elements of the queues.
  for (int i = 0; i != q_rep->parent_rep->n_pools; i++) {
    TMPool* tm_pool = &q_rep->parent_rep->pool[i];
    tm_pool->pool_mu.lock();
    // Sample a few blocks of pool queue elements to avoid linear time search.
    enum { kRangeSize = 64 };    // Number of consecutive elements per block.
    enum { kSampleRanges = 4 };  // Number of blocks to sample.
    size_t queue_len = tm_pool->pool_queue.size();
    size_t pos = 0;      // position within deque
    size_t sampled = 0;  // entries sampled
    size_t found = 0;    // matching entries in sampled entries
    for (int range = 0; range != kSampleRanges && pos != queue_len; range++) {
      pos = std::max(pos, (queue_len * range) / kSampleRanges);
      size_t end_range = std::min(pos + kRangeSize, queue_len);
      sampled += end_range - pos;
      for (; pos != end_range; pos++) {
        found += (tm_pool->pool_queue[pos].q_rep == q_rep);
      }
    }
    tm_pool->pool_mu.unlock();
    if (found != 0) {  // avoid possible divide by zero
      double sampling_ratio =
          static_cast<double>(queue_len) / static_cast<double>(sampled);
      num_on_queue += static_cast<int>(found * sampling_ratio);
    }
  }
  return num_on_queue;
}

ManagedQueueStats ManagedQueue::Stats() const {
  ManagedQueueStats s;
  s.queue_name = this->name();

  this->q_rep_->queue_mu.lock();
  s.queue_running = this->q_rep_->queue_running;
  this->q_rep_->queue_mu.unlock();

  s.num_pending_closures = this->num_pending_closures();  // takes queue_mu
  return s;
}

// Return whether the queue *q_rep has any work that is not yet complete.
// L >= q_rep->queue_mu
static bool TMIsWorkComplete(ManagedQueue::Rep* q_rep) {
  return (q_rep->queue_running == 0 && q_rep->queue_waiters == 0 &&
          q_rep->queue_work.empty() && q_rep->add_after_count == 0);
}

// L < this->q_rep_->queue_mu, this->q_rep_->parent_rep->pool[*]->pool_mu
void ManagedQueue::WaitUntilComplete() {
  ManagedQueue::Rep* q_rep = this->q_rep_;
  // Normally, we just have to wait for TMIsWorkComplete() to return
  // true (see end of this routine).
  // However, if the queue has thread_limit==INT_MAX, the queue_running field
  // is not maintained.  In that case, we first wait until all AddAfter()
  // work has been added to the queue, then we lock every pool queue and mark
  // work that came from this queue as having its count maintained,
  // incrementing the queue_running count appropriately.
  // This allows the wait at the end to work in the expected way.
  if (q_rep->queue_options.thread_limit == INT_MAX) {
    q_rep->queue_mu.LockWhen(absl::Condition(&TMIsWorkComplete, q_rep));
    q_rep->queue_mu.unlock();
    TMCountAllWorkFromQueue(q_rep);
  }
  q_rep->queue_mu.LockWhen(absl::Condition(&TMIsWorkComplete, q_rep));
  q_rep->queue_mu.unlock();
}

// Return the ManagedQueue* that would be returned by CurrentExecutor
// if we were running in a Closure on this ManagedQueue.
// This is used to test that CurrentExecutor returns what it should.
ManagedQueue* ManagedQueue::current_executor_for_testing() const {
  return &this->q_rep_->queue_external;
}

// -----------------------------------------------------------------------
// Constructors for default options.

ManagerOptions::ManagerOptions()
    : n_pools(0), policy(nullptr), watchdog_callback(nullptr) {
#ifdef THREAD_MANAGER_UNOPTIMIZED_BUILD
  constexpr int kDefaultStackSize = 128 * 1024;
#else
  constexpr int kDefaultStackSize = 64 * 1024;  // Encourage smaller stacks.
#endif

  this->thread_options.set_stack_size(
      // Potentially increase the stack size if we are a Python binary.
      // <link>
      thread::python::MaybeAdjustStackSize(kDefaultStackSize, "ThreadManager"));
}

}  // namespace thread
