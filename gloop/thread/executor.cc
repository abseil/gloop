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

#include "gloop/thread/executor.h"

#include <pthread.h>

#include <algorithm>  // max()
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/const_init.h"
#include "absl/base/log_severity.h"
#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/overload.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/callback.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/threadpool.h"
#include "gloop/thread/timedcall.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/functional/with_context.h"

#if !defined(DEBUG_MODE)
#if !defined(NDEBUG)
#define DEBUG_MODE 1
#else
#define DEBUG_MODE 0
#endif
#endif

ABSL_FLAG(int32_t, default_executor_threads, 0,
          "If > 0, the number of threads to use for the default "
          "executor.  Otherwise, one thread per physical cpu (max 32), "
          "with at least two threads in debug mode to force "
          "multi-threaded behavior.");

static bool DefaultExecutorEm2Default() {
  // Eventmanager uses epoll.h which only exists on linux. If it is ported to
  // a cross platform primitive abstraction, we can remove this.
#ifdef __linux__
  return true;
#else
  return false;
#endif
}

ABSL_FLAG(
    std::string, default_executor,
    DefaultExecutorEm2Default() ? "eventmanager2" : "threadpool",
    "Implementation to use for the process's default executor, as returned "
    "from thread::Executor::DefaultExecutor(). If the requested implementation "
    "is not known (e.g. a typo), the process will crash. Defaults to "
    "\"eventmanager2\". Supported values are \"threadpool\" and "
    "\"eventmanager2\". Explicit calls to "
    "thread::Executor::SetDefaultExecutor() override this flag.");

// TODO: delete this flag once all users have been migrated to
// AnyInvocable-accepting overloads of AddCancellable and AddCancellableAt.
ABSL_FLAG(
    bool, thread_executor_checkfail_on_permanent_callbacks, true,
    "If true (default), AddCancellable and AddCancellableAt will check if the "
    "provided Closure is permanent and abort the program if it is. ");

namespace thread_internal {

// Weak function that supplies the default EventManager2 executor. Overridden in
// net/eventmanager/eventmanager_default.cc.
ABSL_ATTRIBUTE_WEAK thread::Executor* DefaultEventManager2Executor() {
  return nullptr;
}

// Choose the number of threads for the default thread pool executor.
static int ChooseNumThreads() {
  // If the flag gives a particular number, use that directly.
  if (const int n = absl::GetFlag(FLAGS_default_executor_threads); n > 0) {
    return n;
  }

  // Otherwise we ask how much parallelism is available and use that number,
  // clamping to a reasonable range.
  //
  // In debug mode we ensure that there are at least two threads in order to
  // force tests and other binaries to be exposed to some parallelism.
  return std::clamp(base::AvailableCPUs(), DEBUG_MODE ? 2 : 1, 32);
}

// Function supplying the default thread pool executor. Not in anonymous
// namespace for testing.
thread::Executor* DefaultThreadPoolExecutor() {
#if THREAD_HAVE_ALTERNATE_THREAD_POOL
  // TODO: b/343761377 - this code should be unreachable, but emscripten
  // (and some others) have inconsistent "somewhat posix" configs. It is
  // understood that the effects of calling into anything thread::Executor
  // for these "mixed posix mode" configs are undefined. So we crash.
  LOG(FATAL) << "Illegal non POSIX use of thread::Executor";
  return nullptr;
#else
  static thread::Executor* thread_pool = []() -> thread::Executor* {
    ThreadPool* pool =
        new ThreadPool(ChooseNumThreads(),
                       ThreadPool::Options{.name_prefix = "DefaultExecutor"});
    return pool;
  }();
  return thread_pool;
#endif
}

// Return a pointer to the global object that should be used as the default
// executor. This never returns null.
static thread::Executor* ChooseDefaultExecutor() {
  // If we've been asked to use an internally-created thread pool as the default
  // executor, then do so.
  const std::string flag_value = absl::GetFlag(FLAGS_default_executor);
  if (flag_value == "threadpool") {
    VLOG(1) << "Using thread pool as default executor, as requested";
    return DefaultThreadPoolExecutor();
  }

  // If we've been asked to use EM2 then attempt to do so, falling back to the
  // thread pool if EM2 hasn't been linked in.
  if (flag_value == "eventmanager2") {
    if (thread::Executor* const em2 =
            thread_internal::DefaultEventManager2Executor()) {
      VLOG(1) << "Using the default EventManager2 as the default executor";
      return em2;
    }

    VLOG(1) << "EM2 not linked in; using thread pool as default executor";
    return DefaultThreadPoolExecutor();
  }

  LOG(FATAL) << "Unexpected value for --default_executor: \"" << flag_value
             << "\"";
}

}  // namespace thread_internal

namespace thread {

static absl::once_flag module_init;
ABSL_CONST_INIT static absl::Mutex set_lock(absl::kConstInit);
static Executor* original_executor = nullptr;
static std::atomic<Executor*> default_executor = nullptr;

static void InitModule() {
  // Prevent concurrent calls to SetDefaultExecutor from racing with us below.
  absl::MutexLock l(set_lock);

  // If SetDefaultExecutor has already been called then there's nothing for us
  // to do.
  if (default_executor != nullptr) {
    return;
  }

  default_executor = thread_internal::ChooseDefaultExecutor();
  original_executor = default_executor;
}

Executor::~Executor() {}

// Default (and unoptimized) implementation of ScheduleMany()
// for backward compatibility.
void Executor::ScheduleMany(
    absl::Span<absl::AnyInvocable<void() &&>> callbacks) {
  for (auto& callback : callbacks) {
    Schedule(std::move(callback));
  }
}

Executor* Executor::DefaultExecutor() {
  absl::call_once(module_init, InitModule);
  return default_executor.load(std::memory_order_acquire);
}

void Executor::SetDefaultExecutor(Executor* executor) {
  absl::MutexLock l(set_lock);
  default_executor.store(executor, std::memory_order_release);
  // Leave original_executor alone so that it contains a pointer to
  // any thread-pool we may have created in InitModule.  This
  // should make the heap-checker happy because it will find a pointer
  // to that pool and won't consider it leaked.
}

void Executor::Delay(absl::Duration delay,
                     absl::AnyInvocable<void() &&> callback) {
  TimedCall::RunAt(
      base::ToWallTime(absl::Now() + std::max(absl::ZeroDuration(), delay)),
      util::functional::WithCurrentContext(std::move(callback)));
}

void Executor::DelayUntil(absl::Time when,
                          absl::AnyInvocable<void() &&> callback) {
  TimedCall::RunAt(base::ToWallTime(when),
                   util::functional::WithCurrentContext(std::move(callback)));
}

// --------------------- Support for CurrentExecutor() ----------------
ABSL_CONST_INIT static thread_local Executor* cur_executor = nullptr;

// Return a pointer to where an executor thread will save the
// executor for which it is working.
Executor** Executor::CurrentExecutorPointerInternal() { return &cur_executor; }

// Return a pointer to the current thread's thread executor, or nullptr
Executor* Executor::CurrentExecutor() {
  return *CurrentExecutorPointerInternal();
}

// --------------------- Support for NewInlineExecutor() ----------------

class InlineExecutor : public Executor {
 public:
  explicit InlineExecutor(bool synchronized) : synchronized_(synchronized) {}
  InlineExecutor() : synchronized_(false) {}

  // This type is neither copyable nor movable.
  InlineExecutor(const InlineExecutor&) = delete;
  InlineExecutor& operator=(const InlineExecutor&) = delete;

  // Overridden to ensure more efficient execution of the callback; there is no
  // need to create/destroy a Closure (and associated execution overhead).
  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    // Make sure that Executor::CurrentExecutor() returns this Executor
    // when it is executing the closure.
    thread::Executor** current_executor =
        thread::Executor::CurrentExecutorPointerInternal();
    // Add() can be called from different threads and nest, so we must
    // save and restore the current Executor pointer.
    thread::Executor* saved_executor = *current_executor;
    *current_executor = this;
    if (synchronized_) {
      absl::MutexLock l(mu_);
      std::move(callback)();
    } else {
      std::move(callback)();
    }
    *current_executor = saved_executor;
  }

  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    DelayUntil(when, [this, callback = std::move(callback)]() mutable {
      Schedule(std::move(callback));
    });
  }

  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    Schedule(std::move(callback));
    return true;
  }

  int num_pending_closures() const override { return 0; }

 private:
  absl::Mutex mu_;
  const bool synchronized_;
};

Executor* NewInlineExecutor() { return new InlineExecutor(false); }
Executor* NewSynchronizedInlineExecutor() { return new InlineExecutor(true); }
Executor* SingletonInlineExecutor() {
  static auto* const singleton = new InlineExecutor();
  return singleton;
}

bool InlineExecutorInternal::IsInlineExecutor(const Executor* e) {
  return dynamic_cast<const InlineExecutor*>(e) != nullptr;
}

// --------------------- Cancellation support ----------------
//
// Every cancellable closure is wrapped inside a CancelWrapper object.
// We keep a hash table that maps from a handle to the state of the
// corresponding closure. The hash table is sharded to reduce lock contention.

// We store a mapping from handle to closure state in a sharded hash
// table.  Bottom eight bits of handle key are the shard number. The
// remaining bits are used as an index into the per-shard hash table.
static const unsigned int kNumShardsBits = 8;
static const unsigned int kNumShards = 1 << kNumShardsBits;

// Helper class to allow cancellation code to access handle internals.
class ExecutorInternal {
 public:
  static void Decode(ExecutorHandle h, int* shard_number, uint64_t* key) {
    *shard_number = h.key_ & (kNumShards - 1);
    *key = h.key_ >> kNumShardsBits;
  }

  static void Encode(int shard_number, uint64_t key, ExecutorHandle* h) {
    h->key_ = (key << kNumShardsBits) | shard_number;
  }
};

namespace {

struct Shard;  // Per-shard state

// A closure that has already been cancelled.
struct Cancelled {};

// A closure that hasn't yet been cancelled or started running.
struct Unstarted {
  Closure* closure;
};

// A closure that has already started running.
struct Started {};

// The state of a closure that can be cancelled if it hasn't yet started
// running.
using ClosureState = std::variant<Cancelled, Unstarted, Started>;

// An object that wraps a closure that can be cancelled using Cancel, that
// provides a cancellation-aware operator() and destructor.
//
// Its contents are protected by shard->mu. It cannot be copied or moved.
class CancelWrapper {
 public:
  CancelWrapper(Closure* closure, ExecutorHandle* handle);

  ~CancelWrapper();

  // We don't support copying or moving because CancelWrapper's destructor
  // interacts with the global map of cancellable callbacks.
  CancelWrapper(const CancelWrapper&) = delete;
  CancelWrapper(CancelWrapper&&) = delete;
  CancelWrapper& operator=(const CancelWrapper&) = delete;
  CancelWrapper& operator=(CancelWrapper&&) = delete;

  void operator()() &&;

 private:
  // The shard of the global map of cancellable callbacks this object is
  // assigned to.
  Shard* shard_;

  // Key in shard->table
  uint64_t shard_key_;

  // The state of the wrapped closure.
  ClosureState closure_state_;
};

typedef absl::flat_hash_map<uint64_t, ClosureState*> Table;
struct Shard {
  absl::Mutex mu;
  Table table;
  uint64_t next_key = 1;
};

static absl::once_flag once;
static Shard* shards;

// Info kept per thread
struct ThreadState {
  int shard;
  uint64_t rand;
  ThreadState() : shard(0), rand(reinterpret_cast<uintptr_t>(this)) {}
};
STATIC_THREAD_LOCAL(ThreadState, current_shard);

static void InitShards() { shards = new Shard[kNumShards]; }

// Updates the shard in the given thread state to a randomly chosen one,
// using the state's generator.
static void SwitchToRandomShard(ThreadState* const t) {
  // We use a cheap random number generator with parameters stolen from
  // lrand48().
  t->rand = t->rand * 0x5deece66dLL + 11;
  t->shard = (t->rand >> 32) & (kNumShards - 1);
}

CancelWrapper::CancelWrapper(Closure* const closure,
                             ExecutorHandle* const handle) {
  absl::call_once(once, InitShards);
  closure_state_ = Unstarted{.closure = closure};

  // Try using the last shard used by this thread and acquiring the lock
  // immediately.
  ThreadState* const ts = current_shard.pointer();
  shard_ = &shards[ts->shard];
  if (!shard_->mu.try_lock()) {
    // We couldn't acquire the last shard's lock immediately. Pick another shard
    // and wait as long as necessary.
    SwitchToRandomShard(ts);
    shard_ = &shards[ts->shard];
    shard_->mu.lock();
  }
  shard_->mu.AssertHeld();

  shard_key_ = shard_->next_key++;
  shard_->table[shard_key_] = &closure_state_;
  ExecutorInternal::Encode(ts->shard, shard_key_, handle);
  shard_->mu.unlock();
}

void CancelWrapper::operator()() && {
  // Delete ourselves once we finish running, following the mostly undocumented
  // convention for Closure and other legacy callback types (as done in other
  // libraries).
  absl::Cleanup delete_this = [&] { delete this; };

  shard_->mu.lock();
  Unstarted* const us = std::get_if<Unstarted>(&closure_state_);

  // If the closure is not in the `Unstarted` state (i.e. it has been cancelled
  // or has already started), or if the scheduled closure was null, there's
  // nothing to run.
  if (us == nullptr || us->closure == nullptr) {
    shard_->mu.unlock();
    return;
  }

  // Prepare to run the closure after releasing the lock and update its state to
  // reflect that.
  Closure* closure = us->closure;
  closure_state_ = Started{};
  shard_->mu.unlock();

  closure->Run();
}

CancelWrapper::~CancelWrapper() {
  std::unique_ptr<Closure> to_delete;
  shard_->mu.lock();

  // Prepare to delete the closure if hasn't been cancelled or started yet, and
  // it's not repeatable.
  //
  // NOTE: Repeatable callbacks are not owned and their creator is responsible
  // for deleting them.
  if (Unstarted* const us = std::get_if<Unstarted>(&closure_state_);
      us != nullptr && !us->closure->IsRepeatable()) {
    to_delete = absl::WrapUnique(us->closure);
  }

  shard_->table.erase(shard_key_);
  shard_->mu.unlock();
}

}  // end anonymous namespace

namespace internal {

// This internal helper is just for tests to verify global state is cleaned up.
bool IsActiveExecutorHandle(ExecutorHandle handle) {
  int s;
  uint64_t shard_key;
  ExecutorInternal::Decode(handle, &s, &shard_key);
  if (shard_key == 0) return false;
  Shard* shard = &shards[s];
  absl::MutexLock l(shard->mu);
  return (shard->table.find(shard_key) != shard->table.end());
}

}  // namespace internal

// Returns a cancellation-aware callable that calls the supplied closure when
// called.
static auto MakeCancellableCallable(Closure* closure, ExecutorHandle* handle) {
  return [cw = std::make_unique<CancelWrapper>(closure, handle)]() mutable {
    // Transfer the responsibility for destroying the wrapper from the
    // std::unique_ptr to the wrapper's operator().
    CancelWrapper& to_call = *cw.release();
    std::move(to_call)();
  };
}

void AddCancellable(Executor* executor, absl::Duration delay,
                    absl::AnyInvocable<void() &&> callback,
                    ExecutorHandle* handle) {
  AddCancellable(executor, delay,
                 util::functional::ToCallback<Closure>(std::move(callback)),
                 handle);
}

void AddCancellable(Executor* executor, absl::Duration delay, Closure* closure,
                    ExecutorHandle* handle) {
  if (ABSL_PREDICT_FALSE(closure->IsRepeatable())) {
    CHECK(
        !absl::GetFlag(FLAGS_thread_executor_checkfail_on_permanent_callbacks))
        << "AddCancellable only accepts non-permanent Callbacks. See "
           "http://b/494604538 for a workaround and leave a comment.";
  }

  executor->ScheduleAfterForMigration(delay,
                                      MakeCancellableCallable(closure, handle));
}

void AddCancellable(Executor* executor, absl::AnyInvocable<void() &&> callback,
                    ExecutorHandle* handle) {
  executor->Schedule(MakeCancellableCallable(
      util::functional::ToCallback(std::move(callback)), handle));
}

void AddCancellableAt(Executor* executor, absl::Time when,
                      absl::AnyInvocable<void() &&> callback,
                      ExecutorHandle* handle) {
  AddCancellableAt(executor, when,
                   util::functional::ToCallback<Closure>(std::move(callback)),
                   handle);
}

void AddCancellableAt(Executor* executor, absl::Time when, Closure* closure,
                      ExecutorHandle* handle) {
  if (ABSL_PREDICT_FALSE(closure->IsRepeatable())) {
    CHECK(
        !absl::GetFlag(FLAGS_thread_executor_checkfail_on_permanent_callbacks))
        << "AddCancellableAt only accepts non-permanent Callbacks. See "
           "http://b/494604538 for a workaround and leave a comment.";
  }

  executor->ScheduleAt(when, MakeCancellableCallable(closure, handle));
}

bool Cancel(ExecutorHandle handle, absl::Duration timeout, Closure** cb_ptr) {
  *cb_ptr = nullptr;
  int s;
  uint64_t shard_key;
  ExecutorInternal::Decode(handle, &s, &shard_key);
  if (shard_key == 0) return true;

  Shard* shard = &shards[s];
  absl::MutexLock lock(shard->mu);
  CHECK_LT(shard_key, shard->next_key);

  Table::iterator iter = shard->table.find(shard_key);

  // Has the closure either finished running already, or was never registered
  // to begin with?
  if (iter == shard->table.end()) {
    return true;
  }

  ClosureState* const closure_state = iter->second;

  return std::visit(
      absl::Overload{
          [](Cancelled) {
            // The closure has already been cancelled: there's nothing left for
            // us to do.
            return true;
          },
          [&](Unstarted us) {
            // The closure hasn't yet started running: cancel it.
            *cb_ptr = us.closure;
            *closure_state = Cancelled{};
            return true;
          },
          [&](Started) {
            // The closure has already started running: wait to see if it'll
            // finish before the timeout expires, if it hasn't already expired.
            if (timeout <= absl::ZeroDuration()) {
              return false;
            }

            // Wait until either the timeout expires, or the closure finishes
            // running and erases the entry from shard->table.
            auto finished_running = [shard, shard_key] {
              return shard->table.find(shard_key) == shard->table.end();
            };
            shard->mu.AwaitWithTimeout(absl::Condition(&finished_running),
                                       timeout);

            // Has the closure finished running before the timeout has expired?
            return shard->table.find(shard_key) == shard->table.end();
          },
      },
      *closure_state);
}

bool Cancel(ExecutorHandle handle, absl::Duration timeout) {
  Closure* c = nullptr;

  // If the closure was cancelled successfully, delete it before returning.
  absl::Cleanup d = [&] { delete c; };

  return Cancel(handle, timeout, &c);
}

}  // namespace thread
