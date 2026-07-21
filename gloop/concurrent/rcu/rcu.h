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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_RCU_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_RCU_H_

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "gloop/base/percpu.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"
#include "gloop/concurrent/rcu/llist.h"
#include "gloop/concurrent/rcu/pile.h"

namespace base {

// 99.9% of users want `rcu::View`:
// https://github.com/abseil/gloop/tree/main/gloop/concurrent/rcu/view.h `View`
// is a easy to use, zero-overhead wrapper for the technology here.
//
// RCU implements an extremely scalable form of reader-writer lock.  Reader
// locks are extremely cheap and should scale linearly to any number of readers;
// in particular, they don't use any atomic instructions or other
// synchronization. They aren't even slowed by the existence of writers.
//
// The disadvantage is that the "writer lock" can't actually modify
// protected data; instead, you must atomically publish a new version
// of the data, then wait until all previous readers have finished.
//
// A simple example of valid usage:
// ABSL_CONST_INIT rcu::Domain d(absl::kConstInit);
// ABSL_CONST_INIT rcu::Value<std::map<int, string>> name(&d);
// string GetName(int id) {  // Fast, constant time read access.
//   rcu::ReaderLockHolder l(&d);
//   auto iter = name.Get(&d)->find(id);
//   return iter->second;
// }
//
// All new readers will immediately see <n>.
// Returns only when all readers who could have seen an older value
// have released their read-lock. Might sleep.
// void UpdateName(int id, const string &n) {
//   std::map<int, string> *old_names, *new_names;
//   new_names = new std::map<int, string>;
//   ABSL_CONST_INIT static Mutex writer_lock(absl::kConstInit);
//   // we still need to serialize changes to the map
//   MutexLock h(&writer_lock);
//   {
//      rcu::ReaderLockHolder h(&d);
//      old_names = name.Get(&d);
//      *new_names = *old_names;
//      (*new_names)[id] = n;
//   }
//   // it's important to drop the reader lock before updating
//   name.Replace(new_names, &d);
//   delete old_names;
// }
//
// For dramatically more detailed documentation, see the linux kernel RCU
// documentation; our interface matches theirs closely (though not exactly);
// our implementation differs (since it's done in userspace) but has similar
// scaling properties.
//
// Note that this API is intended to become the globally accepted
// google3 implementation of this idiom (replacing all other
// contenders), but it is still under development. WE RESERVE THE
// RIGHT TO CHANGE API DETAILS.
namespace rcu {

class Domain;
class ReaderLockHolder;

// an opaque token for tracking reader state. Do not look at the
// implementation.
struct Token {
  Token() = delete;
  // copy, assign are fine
 private:
  ::base::subtle::percpu::Handle h;
  // Tokens may only be created by ReaderLock
  explicit Token(::base::subtle::percpu::Handle hand) : h(hand) {}

  void DoUnlock();
  friend Domain;
  friend ReaderLockHolder;
  friend Token DummyToken();
};

// ReaderLockHolder and Value are the simplest part of the RCU API; you are
// highly encouraged to use them wherever possible.
// For the raw API (and precise semantics) see Domain below.

// a RAII RCU reader lock.
// REQUIRES: must be destructed by the creating thread, including moved-from
// states.
class ABSL_SCOPED_LOCKABLE ReaderLockHolder {
 public:
  explicit ReaderLockHolder(Domain* d) ABSL_SHARED_LOCK_FUNCTION(d);
  ReaderLockHolder(ReaderLockHolder&& other) noexcept
      : t_(std::exchange(other.t_,
                         Token(::base::subtle::percpu::NullHandle()))) {}
  ReaderLockHolder& operator=(ReaderLockHolder&& other) noexcept {
    if (this != &other) {
      t_.DoUnlock();
      t_ = std::exchange(other.t_, Token(::base::subtle::percpu::NullHandle()));
    }
    return *this;
  }

  ~ReaderLockHolder() ABSL_UNLOCK_FUNCTION();

  // Consume a previously constructed lock token (avoid this if possible.)
  // Takes ownership of the lock token; do not pass it to Unlock after this.
  explicit ReaderLockHolder(Token t);

  // disable bad constructors:
  ReaderLockHolder(const ReaderLockHolder& rhs) = delete;
  ReaderLockHolder& operator=(const ReaderLockHolder& rhs) = delete;

 private:
  Token t_;
};

// Useful lock annotations for RCU.
#define RCU_READ_LOCK_EXCLUDED(d) ABSL_LOCKS_EXCLUDED(d)
#define RCU_READ_LOCK_REQUIRED(d) ABSL_SHARED_LOCKS_REQUIRED(d)

// a pointer to RCU-protected data; the interface prevents unlocked
// usage and handles memory ordering.  Values are uniquely associated
// with a single Domain at construction.
template <typename T>
class Value {
 public:
  constexpr explicit Value(Domain* d);
  constexpr Value(T* value, Domain* d);

  // REQUIRES: Get(d) == NULL
  ~Value();

  // Returns the current value held by *this, which is guaranteed to
  // be valid for as long as this thread holds a reader lock. This is
  // an Acquire versus Replace().  Note that multiple calls to Get(),
  // even within the same reader lock, are _not_ guaranteed to return
  // the same T; callers wanting to operate on a consistent object
  // should reuse the return value from one call.  (That said, any and
  // all return values are safe to access.)  REQUIRES: d = this
  // value's domain from the constructor
  const T* Get(Domain* d) const RCU_READ_LOCK_REQUIRED(d);

  // Returns true if the current contained pointer is null.
  //
  // This does not require a reader lock, and so is significantly faster
  // than calling Get() and checking the result. However, it is only useful
  // for fast paths in the case that a value is usually null.
  bool IsNull(Domain* d) const;

  // Update the value returned by Get. Blocks until the reader lock
  // held by any thread which might have seen the old value has been released.
  // (See Synchronize() for more details.)
  // REQUIRES: d = this value's domain from the constructor
  T* Replace(T* new_value, Domain* d) RCU_READ_LOCK_EXCLUDED(d);
  // advanced APIs:

  // As Replace(), but the old value may still be in use by previous
  // Get()s until Synchronize() is called. (This allows amortization
  // of Synchronize() over updates of more than one pointer, and is also
  // safe to call when the associated Domain's read-lock is held.)
  T* ReplaceUnsynchronized(T* new_value);

  // As ReplaceUnsynchronized, but *only* replace the old value if it is equal
  // to <old_value> (returning true iff that is the case.) Otherwise do nothing.
  bool TryReplaceUnsynchronized(const T* old_value, T* new_value);

  Value(const Value& rhs) = delete;
  Value& operator=(const Value& rhs) = delete;

 private:
  std::atomic<T*> value_;
#ifndef NDEBUG
  Domain* domain_check_;
#endif

  void CheckSingleDomain(Domain* d) const;
};

void DomainInit();

class ABSL_LOCKABLE Domain : public ::base::LListEntry<Domain> {
 public:
  // Domains allocated on the stack or heap should use this constructor.
  Domain();

  // Domains that live in static or global storage should use this constructor.
  // All other Domains should use the default constructor.
  constexpr explicit Domain(absl::ConstInitType)
      : phase_handle_(0),
        reader_counts_{::base::subtle::percpu::Handle{}},
        cleanup_phase_(0),
        current_phase_(0),
        callbacks_(),
        queued_for_run_(false),
        num_queues_(0),
        in_cleanup_list_(false),
        destruct_(false) {}

  // It is unsafe to race ~Domain() with any use (including
  // ReaderLocks) of the Domain. In particular, all callbacks
  // queued with Call or similar must have *completed* before
  // invoking this.  (Synchronize helps here.)
  ~Domain();

  // The region between a ReaderLock and its matching ReaderUnlock defines
  // a "reader critical section.", which may be nested.
  //
  // These functions are async signal safe if and only if
  // https://github.com/abseil/gloop/tree/main/gloop/base:percpu is
  // async-signal-safe in your configuration; see comment in rcu.cc.
  //
  // REQUIRES: Lock and Unlock must occur within the same thread.
  Token ReaderLock() ABSL_SHARED_LOCK_FUNCTION();
  void ReaderUnlock(Token t) ABSL_UNLOCK_FUNCTION();

  // Synchronize waits for extant readers to finish. More precisely,
  // we have the following guarantee (for this domain's RCSes):
  //
  // - Any memory access done after a Synchronize() is _not_ visible to
  //   any part of a reader critical section R, unless every memory
  //   access done by this thread before Synchronize was also visible to
  //   all of R.
  //
  // Typically, Synchronize() is called after publishing new value(s)
  // (e.g. with Replace); after it returns, we guarantee that no
  // reader critical sections will access the old value. This allows
  // the caller to immediately release or reallocate the underlying
  // memory, without additional locking or memory barriers.
  //
  // REQUIRES: Reader lock MUST NOT be held.
  void Synchronize() ABSL_LOCKS_EXCLUDED(this);

  // Asynchronous APIs (in several variants.)

  // Run a callback when all extant readers have completed.  More precisely:
  //
  // - Given sufficient time (and bounded readers), f() will be invoked
  // - Any memory access made by f() is _not_ visible to any part of a
  //   reader critical section R, unless every memory access done by
  //   this thread before Call() was also visible to all of R.
  //
  // Important: we make no guarantees about achievable throughput.
  // Enough uses of Call() (without some sort of backoff) *will*
  // lead to backlog and OOM.
  // REQUIRES: EnableCleanup must have been called.
  void Call(std::function<void()> f);

  // A helper for Call's most common purpose: Free(obj) invokes delete on obj
  // when all readers have cleared. Async-signal-safe.
  template <class T>
  void Free(T* obj);
  template <class T>
  void Free(std::unique_ptr<T> ptr);
  template <class T>
  void Free(std::unique_ptr<T[]> ptr);
  // As Free, but for arrays.  Don't blame me, blame delete[].
  template <class T>
  void FreeArray(T* obj);

  // Underlying implementation for Call and Free.  Async signal safe.
  // (With the same safety guarantees as Call) invokes func(data);
  // Guarantee (probably shouldn't need this in most code): if
  // CallRaw() happens-before Synchronize() is called, then func()
  // was called happens-before Synchronize() returns.
  // REQUIRES: EnableCleanup must have been called.
  void CallRaw(void (*func)(void*), void* data);
  Domain(const Domain& rhs) = delete;
  Domain& operator=(const Domain& rhs) = delete;

  // This function must be called at least once before any invocation
  // of Call/CallRaw/Free/FreeArray.  It is idempotent, and all calls
  // after the first are extremely cheap.  It can be safely called from
  // static initializers (the backgroud thread will be created during
  // GoogleInit).
  // Not signal safe.
  // TODO: eliminate need for this ASAP.
  static void EnableCleanup();

 private:
  // The handle from  `reader_counts_` corresponding to `current_phase_`.
  // Convert back to a percpu::Handle by using `base::percpu::HandleFromInt`.
  std::atomic<int64_t> phase_handle_;
  static constexpr int kNumPhases = 2;
  ::base::subtle::percpu::Handle reader_counts_[kNumPhases];
  size_t cleanup_phase_ ABSL_GUARDED_BY(update_lock_);
  size_t current_phase_ ABSL_GUARDED_BY(update_lock_);
  struct CallData {
    void (*func)(void*);
    void* data;
  };

  Pile<CallData> callbacks_[kNumPhases];
  std::atomic<bool> queued_for_run_;
  std::atomic<int32_t> num_queues_;

  // Set to true when the domain is enqueued in run_domain_callbacks_thread's
  // local work list (domains), and reset to false when callbacks complete.
  //
  // NOTE: This field is read and written exclusively by
  // run_domain_callbacks_thread, never accessed by client threads.
  bool in_cleanup_list_;

  SpinLock update_lock_;
  const bool destruct_;  // true if not using kConstInit

  static void DoEnableCleanup();
  friend class DomainInitHelper;
  void QueueCallback(CallData cd);
  void Enqueue();
  bool TryRunCallbacks(size_t* fences);
  bool TryRunCallbacksLocked(size_t* fences)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_lock_);
  bool TryAdvanceLocked(size_t* fences)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_lock_);
  bool TryCleanupLocked(size_t* fences)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_lock_);
  void RunCallbacksLocked(size_t phase)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_lock_);
  friend bool RunDomain(Domain* d);
  friend bool AddNewDomains(std::vector<Domain*>* domains);
  friend class DomainTestPeer;
  // Slow path is necessary for safe absl::kConstInit Domain (i.e. global RCU.)
  Token ReaderLockInit();
  // Slow path for !IsFastNoInit to trigger fast restartable sequence
  // initialization or use the slow path of per-CPU operations if fast
  // restartable sequences are unavailable.
  Token ReaderLockSlow(int64_t handle);
  void MaybeInitStaticHandlesLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_lock_);
};

class DomainTestPeer {
 public:
  static int32_t num_queues(const Domain& d) {
    return d.num_queues_.load(std::memory_order_acquire);
  }
};

namespace internal {

bool CleanupEnabled();

}  // namespace internal

// TODO: more helpers?

// end of public API
#include "gloop/concurrent/rcu/rcu-impl.inc"

}  // namespace rcu
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_RCU_H_
