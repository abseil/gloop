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

// Each domain contains a tree of Scheduler objects.  Each Scheduler manages
// a set of Schedulable objects.
//
// CAUTION: Implementations of Scheduler may not call on primitives that
// internally use user-level scheduling semantics.  This includes most google3
// synchronization interfaces, e.g.: Mutex, CondVar, Notification, etc.

#ifndef THIRD_PARTY_GLOOP_BASE_SCHEDULING_SCHEDULER_H_
#define THIRD_PARTY_GLOOP_BASE_SCHEDULING_SCHEDULER_H_

#include <atomic>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/internal/low_level_scheduling.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "gloop/base/spinlock.h"
#include "gloop/util/atomic_danger/atomic_danger.h"
#include "gloop/util/atomic_danger/refcount.h"

#ifdef BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED
#error "BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED cannot be overridden."
#elif defined(__cpp_aligned_new)
// Assert cache alignment if aligned allocations are enabled.  Note: this C++17
// feature is also implemented in the google3 C++11 production crosstool.
#define BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED 1
#define BASE_SCHEDULABLE_ALIGNAS alignas(ABSL_CACHELINE_SIZE)
#else
#define BASE_SCHEDULABLE_ALIGNAS
#endif

namespace base {
namespace scheduling {

class Domain;
class Schedulable;
class Scheduler;

// Schedulable
// -----------
// A Scheduler manages a set of slots.  Schedulables represent the work that may
// be scheduled into each slot.  This may either be execution or a runnable slot
// of a child scheduler (see Schedulable::Type).
//
// Each Schedulable belongs to a scheduler.  Schedulables are "managed" by their
// Scheduler and "manage" the work that they represent.  A Schedulable always
// identifies the Scheduler it is (exclusively) scheduled by as its "manager".
//
// A Schedulable has 3 possible states:
//    blocked:  Schedulable is not currently runnable, it is not eligible to
//              participate in any scheduling decisions until a future wake-up
//              returns it to a runnable state.
//    runnable: Schedulable is eligible to run, but is not currently mapped to a
//              slot.  Also referred to as "queued".
//    running:  Schedulable has been assigned to a Scheduler slot.
//              Recursively: This slot was assigned to a parent slot or !parent.
// A Schedulable is blocked iff schedulable->runnable_count < 0
//
// REQUIRES: Callers must use the base::scheduling::Downcalls interfaces
// for all interactions with Schedulables.

// We use a wrapper for the Schedulables representing a Scheduler's slots.
// This provides additional type-safety to differentiate between the
// Schedulables that a Scheduler manages, and the Schedulables managing it.
// Slots have equivalent lifetime to the schedulable they encapsulate.  While
// either may be freed, it is invalid to release both.
class Slot {
 public:
  // Must be a trivial object. Only default constructors/destructor.

  // The Scheduler that this Slot manages.
  Scheduler* managed_scheduler() const;

  // Data owned by the Scheduler that *this manages.  Accessor for the
  // underlying Schedulable::managed_arg1_.
  intptr_t managed_arg() const;
  void set_managed_arg(intptr_t new_arg1);

  // Equivalent to a null Schedulable.
  static Slot NullSlot();

  // Slots are word representable, useful for interactions with atomics.
  intptr_t AsWord() const;
  static Slot FromWord(intptr_t word);

  bool operator==(const Slot& rhs) const {
    return schedulable_ == rhs.schedulable_;
  }
  bool operator!=(const Slot& rhs) const {
    return schedulable_ != rhs.schedulable_;
  }

 private:
  // REQUIRES: schedulable->type == Schedulable::kChildSlot
  static Slot FromSchedulable(Schedulable* schedulable);

  Schedulable* schedulable_;  // The kChildSlot we encapsulate.

  friend class Scheduler;
  friend class Downcalls;
};

class BASE_SCHEDULABLE_ALIGNAS Schedulable {
 public:
  enum Type {
    kChildSlot = 1,  // This schedulable manages the slot of a child scheduler.
    kWorkItem = 2,   // This schedulable manages user execution (e.g. a
                     // thread or fiber) which may be run within the domain.
  };

  static Schedulable* GetBoundSchedulable(
      const absl::base_internal::ThreadIdentity* identity) {
    return reinterpret_cast<Schedulable*>(
        identity->scheduler_state.bound_schedulable.load(
            std::memory_order_relaxed));
  }

  // Construct a new Schedulable representing work scheduled by manager.
  // Always constructed in blocked state with no manager/managed flags_ set.
  Schedulable(Scheduler* manager, Type schedulable_type);

  // CAUTION: This destructor is explicitly non-virtual (POD-type).
  // REQUIRES: DeleteSchedulable() must always be used to release a schedulable.
  ~Schedulable() {}

  const Type type;
  std::atomic<int32_t>
      runnable_count;        // A schedulable is runnable iff count >= 0.
  Scheduler* const manager;  // Responsible for all scheduling of *this.

  // When running, the slot managing "manager" into which *this was scheduled.
  // e.g. managing_slot->managed.scheduler == manager, "this" last returned
  // by manager->ScheduleManaged(managing_slot, ...);
  // REQUIRES: Only valid for currently running schedulables.
  Slot managing_slot;

  // A reference to the resource that this schedulable manages.
  union {
    Scheduler* scheduler;  // meaningful when type == kChildSlot
    void* work;            // meaningful when type == kWorkItem
    int64_t raw64;         // "managed" always has 8-bytes available.
  } managed;

  // Returns whether "flag_index" is set.  Acquire versus {clear,set}_flag().
  bool is_flag_set(int flag_index) const;

  // Set, or clear, the bit at "flag_index".  Returns true if this modified
  // flags_, false if the specified flag already had the desired setting.
  // Acquire versus {clear,set}_{*}_flag().  Release versus is_flag_set().
  bool set_manager_flag(int flag_index);
  bool clear_manager_flag(int flag_index);
  bool set_managed_flag(int flag_index);
  bool clear_managed_flag(int flag_index);

  // STRONGLY DISCOURAGED.
  //
  // Store the lower 16 bits of manager_short in the lower 16 bits of
  // this->flags_. If you use the two methods below, do not use
  // set/clear_manager_flag API above.
  ABSL_DEPRECATED("Do not use bit flags for composite state.")
  void set_manager_short(int32_t manager_short);
  inline int32_t manager_short() const {
    return 0xFFFF & flags_.load(std::memory_order_acquire);
  }

  inline intptr_t managed_arg() const { return managed_arg1_; }

  void set_managed_arg(intptr_t arg);

  // Extra fields reserved for use by manager.  This may be used to associate
  // additional state (e.g. virtual runtime, deadlines, etc.).
  void* manager_ptr1;
  uint64_t manager_num1;
  int manager_int1;

 private:
  // Bits 24-31 of "flags_" are reserved for implementation details.
  enum ImplFlags {
    kFiber = 24,
  };

  // Set, or clear, the bit at "flag_index".  Returns true if this modified
  // flags_, false if the specified flag already had the desired setting.
  // Acquire versus {clear,set}_flag().  Release versus is_flag_set().
  bool set_flag(int flag_index);
  bool clear_flag(int flag_index);

  void set_managed_arg_internal(intptr_t arg, int32_t internal_flag_index);

  friend void InternalAttachFiber(Schedulable* s, void* fiber);
  friend void InternalDetachFiber(void* fiber);
  friend bool IsFiberAttached(const Schedulable* s);

  // 32 single bit flags_ are available, these may be used by the
  // manager/managed implementations to to describe state such as whether an
  // underlying thread has been bound.
  //
  // Flags  0..16 are reserved for manager.
  // Flags 17..23 are reserved for managed.
  // Flags 24..31 are reserved for implementation internals.
  //
  // REQUIRES: Modifications must be atomic.
  std::atomic<int32_t> flags_;

  // Extra field reserved for use by managed.
  intptr_t managed_arg1_;
};
static_assert(sizeof(Schedulable) <= ABSL_CACHELINE_SIZE,
              "schedulable_larger_than_cacheline");

// Deallocate an existing schedulable.  It must have been allocated by
// Scheduler::NewManagedSchedulable()
//
// REQUIRES: schedulable must be blocked
// REQUIRES: No potential outstanding references (e.g. a wakeup) may exist.
void DeleteSchedulable(Schedulable* schedulable);

// Scheduler
// ---------
// A Scheduler makes scheduling decisions between the Schedulables it
// owns.  Scheduler is an abstract class; subclasses contain real
// scheduling logic.  A single scheduler will interact only with
// schedulables that it owns (although these may themselves represent
// child schedulers); all schedulables within a scheduling hierarchy
// will always belong to the same domain.
//
// TODO: Add {Increase,Decrease}Concurrency() APIs.
class Scheduler {
 public:
  // Construct the root-level scheduler for the specified "domain".
  //
  // The schedules it allocates to manage itself will be "root" schedulables for
  // this domain.  A root schedulable is a Domain-level slot, representing
  // allowed concurrency.
  //
  // Initializes with zero external references.  Orphan() must be called when no
  // longer needed.
  //
  // REQUIRES: num_slots() == domain->max_concurrency()
  // REQUIRES: domain->root_scheduler() == nullptr
  explicit Scheduler(Domain* absl_nonnull domain);

  // Construct a child scheduler of "parent". "parent" will allocate,
  // own, and manage the Schedulables that control this scheduler.
  //
  // This Scheduler will schedule up to "num_slots" concurrent entities.
  // Implementations are responsible for creating the schedulables which
  // correspond to these slots; managing *this and managed by "parent".
  //
  // Initializes with zero external references.  Orphan() must be called when no
  // longer needed.
  //
  // REQUIRES: slots <= parent->num_slots()
  Scheduler(Scheduler* parent, int slots);

  // The domain that this scheduler belongs to.  All schedulers in a tree
  // will have the same domain.
  inline Domain* absl_nonnull domain() const { return &domain_; }

  // The depth of this scheduler in the scheduler tree.  The root scheduler
  // has a depth of zero.
  inline int depth() const { return depth_; }

  // Allocates and returns a new schedulable, managed by this scheduler.
  // The returned schedulable will be:
  //  (a) in a blocked state (schedulable->runnable_count < 0).
  //  (b) managed by this scheduler (schedulable->manager == this)
  //  (c) of the passed Schedulable::Type.
  //  (d) Cache-aligned.
  //
  // ** All implementations must satisfy these requirements.  No other
  // assumptions may be made regarding the new object's contents. **
  //
  // The default implementation takes a single reference on *this.
  //
  // REQUIRES: DeleteSchedulable() must always be used to free Schedulables.
  virtual Schedulable* NewManagedSchedulable(Schedulable::Type type);

  // Mark this scheduler as no longer in use. Once all references taken by Ref()
  // have been released (by matched calls to Unref()), *this will be
  // automatically deleted.
  //
  // REQUIRES: Once called, *this may only be referenced by clients holding
  // outstanding references (via Ref()).
  void Orphan();

  // Schedulers are reference counted.  Schedulers are not considered for
  // release until Orphan() has been called.  An orphaned scheduler will be
  // automatically deleted once there are no outstanding references.
  //
  // The following references are always taken/released by the base class.
  //  (a) Every live child scheduler holds a reference on its parent.
  //  (b) Child schedulers automatically release their parent reference when
  //      finished.
  //
  // The base-class (over-ridable) implementation also specifies that:
  // - NewManagedSchedulable() and DeleteManagedSchedulable() acquire and
  //   release a reference respectively.
  //
  // Implementations must guarantee that Release()'s preconditions have been met
  // if Orphan() has been called and there are no outstanding references.

  // Acquires a single reference on *this.
  // REQUIRES: At least one other reference must be held if *this is orphaned.
  inline void Ref();

  // Releases a single reference on *this.  Invokes Release() when the last
  // reference is removed.
  // REQUIRES: Must pair with a previous call to Ref().
  inline void Unref();

  Scheduler* parent() const { return parent_; }
  int num_slots() const { return num_slots_; }

  // Always initialized to zero.
  // REQUIRES: May only be called by parent()
  intptr_t parent_arg1() const { return parent_arg1_; }
  void set_parent_arg1(intptr_t value) { parent_arg1_ = value; }

 protected:
  // Implementations may extend the destructor, however, objects may ONLY be
  // freed by Release().
  //
  // Releases a single reference on *parent (for non-root schedulers).
  //
  // REQUIRES: May only be called by Release().
  virtual ~Scheduler();

  // Allocates a new slot managing *this.
  // REQUIRES: No more than num_slots() may be outstanding.
  Slot NewManagingSlot();

  // Release a slot managing *this, previously allocated by NewManagingSlot().
  // REQUIRES: Slot manages *this.
  // REQUIRES: Equivalent preconditions to DeleteSchedulable();
  void DeleteManagingSlot(Slot slot);

  // May be called by implementations to wake an idle slot managing *this.
  // REQUIRES: "slot" must be idle, slot.managed_scheduler == this.
  void ScheduleAdditionalSlot(Slot slot);

  // The interfaces below may not be invoked by clients directly.  Instead,
  // use base::scheduling::Downcalls.

  // Wake "schedulable".
  //
  // Returns a slot managing this scheduler when this wake-up allows an
  // additional slot belonging to this scheduler to be run (up to num_slots()).
  // Otherwise returns Slot::NullSlot().
  //
  // Implementations must guarantee correctness of:
  //   ScheduleManaged(..., prev, false) and StopRunning(..., prev, false)
  // in the presence of a concurrent Wake(prev).
  //
  // REQUIRES: schedulable->manager == this
  virtual Slot Wake(Schedulable* schedulable) = 0;

  // Choose a schedulable to under "managing_slot".
  //
  // Returns null if there no available runnable schedulables.
  //
  // Implementations must guarantee that:
  // (a) Wake(schedulable), StopRunning(..., schedulable, ...), or
  //     ScheduleManaged(..., schedulable, ...)
  // (b) Same schedulable returned by ScheduleManaged(...)
  // Always specify a Release/Acquire pair (respectively) for "schedulable".
  //
  // REQUIRES: managing_slot != Slot::NullSlot &&
  //           managing_slot.managed_scheduler() == this
  // REQUIRES: If prev != null: prev->managing_slot == managing_slot
  // REQUIRES: If prev == null: runnable == false
  virtual Schedulable* ScheduleManaged(Slot managing_slot, Schedulable* prev,
                                       bool runnable) = 0;

  // Cease running "current", which was previously returned by
  // ScheduleManaged(managing_slot, ...), without scheduling a new schedulable
  // in its place.  If "runnable" is true then "current" will be re-queued for
  // future selection by ScheduleManaged().
  //
  // Returns true if "managing_slot" is still runnable.
  // Returns false if concurrency within the domain has decreased, requiring
  // that "managing_slot" is also de-scheduled as it may no longer run.
  //
  // CAUTION: StopRunning(..., true) may return false.
  // REQUIRES: Domain::CurrentThreadSchedulable() == current
  // REQUIRES: current->managing_slot == managing_slot
  virtual bool StopRunning(Slot managing_slot, Schedulable* current,
                           bool runnable) = 0;

 private:
  // Frees this scheduler.  Called automatically when all references have been
  // released.
  //
  // Release() guarantees: when called during a scheduling decision, the
  // lifetime of *this will be protected until scheduling is complete.
  // e.g. It may be safely called from within ScheduleManaged() etc.
  //
  // REQUIRES: No schedulables managed by this scheduler may exist.
  // REQUIRES: All slots managing this scheduler must be blocked.
  void Release();

  // Delete a schedulable previously allocated by this scheduler.
  // Implementations must ensure the appropriate ~Schedulable() is called.
  //
  // The default implementation releases a single reference on *this.
  //
  // REQUIRES: May only be called by DeleteSchedulable().
  // REQUIRES: schedulable->manager == this
  // REQUIRES: schedulable is not running or queued.
  virtual void DeleteManagedSchedulable(Schedulable* schedulable);

  // Implementations may override this to coordinate with Orphan().  Occurs
  // before the constructor reference is released.
  virtual void NotifyOrphaned() {}

  Domain& domain_;           // Constant for all schedulers in a tree.
  Scheduler* const parent_;  // null for the root-scheduler in a domain.
  const int depth_;          // Indexed from 0 at the root-scheduler.
  const int num_slots_;      // Must be <= domain_->max_concurrency()
  atomic_danger::RefCount<int32_t> references_held_;
  std::atomic<bool> orphaned_;
  intptr_t parent_arg1_;

  friend class Downcalls;
  friend class ::absl::base_internal::SchedulingGuard;
  friend void DeleteSchedulable(Schedulable* schedulable);

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
};

//------------------------------------------------------------------------------
// End of public interfaces.
//------------------------------------------------------------------------------

inline bool Schedulable::set_flag(int flag_index) {
  const unsigned int flag_bit = 1 << flag_index;

  int32_t v;
  do {
    v = flags_.load(std::memory_order_acquire);
    if (v & flag_bit) {
      return false;
    }
  } while (v != atomic_danger::CompareAndSwap(&flags_, v, v | flag_bit,
                                              std::memory_order_release));
  return true;
}

inline bool Schedulable::clear_flag(int flag_index) {
  const unsigned int flag_bit = 1 << flag_index;

  int32_t v;
  do {
    v = flags_.load(std::memory_order_acquire);
    if (!(v & flag_bit)) {
      return false;
    }
  } while (v != atomic_danger::CompareAndSwap(&flags_, v, v & ~flag_bit,
                                              std::memory_order_release));
  return true;
}

inline void Schedulable::set_manager_short(int32_t manager_short) {
  ABSL_RAW_DCHECK(0 == (manager_short & ~0xFFFF), "manager flags out of range");
  int32_t new_flags;
  do {
    new_flags = flags_.load(std::memory_order_relaxed);
    if ((new_flags & 0xFFFF) == manager_short) return;
  } while (new_flags !=
           atomic_danger::CompareAndSwap(&flags_, new_flags,
                                         (new_flags & ~0xFFFF) | manager_short,
                                         std::memory_order_release));
}

inline bool Schedulable::set_manager_flag(int flag_index) {
  ABSL_RAW_DCHECK(flag_index < 16, "manager flag index out of range");
  return set_flag(flag_index);
}

inline bool Schedulable::clear_manager_flag(int flag_index) {
  ABSL_RAW_DCHECK(flag_index < 16, "manager flag index out of range");
  return clear_flag(flag_index);
}

inline bool Schedulable::set_managed_flag(int flag_index) {
  ABSL_RAW_DCHECK(flag_index >= 16 && flag_index < 24,
                  "managed flag index out of range");
  return set_flag(flag_index);
}

inline bool Schedulable::clear_managed_flag(int flag_index) {
  ABSL_RAW_DCHECK(flag_index >= 16 && flag_index < 24,
                  "managed flag index out of range");
  return clear_flag(flag_index);
}

inline bool Schedulable::is_flag_set(int flag_index) const {
  return flags_.load(std::memory_order_acquire) & (1 << flag_index);
}

inline void Schedulable::set_managed_arg(intptr_t arg) {
  managed_arg1_ = arg;
  clear_flag(Schedulable::kFiber);
}

inline void Schedulable::set_managed_arg_internal(intptr_t arg,
                                                  int32_t internal_flag_index) {
  ABSL_RAW_DCHECK(internal_flag_index >= 24,
                  "internal flag index out of range");
  managed_arg1_ = arg;
  set_flag(internal_flag_index);
}

inline void InternalAttachFiber(Schedulable* s, void* fiber) {
  s->set_managed_arg_internal(reinterpret_cast<intptr_t>(fiber),
                              Schedulable::kFiber);
}

// REQUIRES: must be called in the context of the running fiber/schedulable.
inline void InternalDetachFiber(void* fiber) {
  auto* identity = absl::base_internal::CurrentThreadIdentityIfPresent();
  if (identity != nullptr) {
    SpinLockHolder l(*identity->scheduler_state.association_lock());
    auto* schedulable = Schedulable::GetBoundSchedulable(identity);
    if (schedulable != nullptr) {
      ABSL_RAW_DCHECK(
          schedulable->type == base::scheduling::Schedulable::kWorkItem,
          "Running schedulable not a work item.");
      ABSL_RAW_DCHECK(IsFiberAttached(schedulable), "No fiber attached.");
      ABSL_RAW_DCHECK(
          schedulable->managed_arg1_ == reinterpret_cast<intptr_t>(fiber),
          "Detaching a wrong fiber.");
      schedulable->clear_flag(Schedulable::kFiber);
    }
  }
}

inline bool IsFiberAttached(const Schedulable* s) {
  return s->is_flag_set(Schedulable::kFiber);
}

// static
inline Slot Slot::FromSchedulable(Schedulable* schedulable) {
  ABSL_RAW_DCHECK(
      schedulable == nullptr || schedulable->type == Schedulable::kChildSlot,
      "not a slot");

  Slot s;
  s.schedulable_ = schedulable;
  return s;
}

inline Slot Slot::NullSlot() { return FromSchedulable(nullptr); }

inline intptr_t Slot::managed_arg() const {
  return schedulable_->managed_arg();
}

inline void Slot::set_managed_arg(intptr_t new_arg1) {
  schedulable_->set_managed_arg(new_arg1);
}

inline Scheduler* Slot::managed_scheduler() const {
  return schedulable_->managed.scheduler;
}

inline intptr_t Slot::AsWord() const {
  return reinterpret_cast<intptr_t>(schedulable_);
}

inline Slot Slot::FromWord(intptr_t word) {
  return FromSchedulable(reinterpret_cast<Schedulable*>(word));
}

void Scheduler::Ref() { references_held_.Inc(); }

void Scheduler::Unref() {
  if (references_held_.Dec()) {
    Release();
  }
}

}  // namespace scheduling
}  // namespace base

// b/495759467
namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
using Schedulable = ::base::scheduling::Schedulable;
}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // THIRD_PARTY_GLOOP_BASE_SCHEDULING_SCHEDULER_H_
