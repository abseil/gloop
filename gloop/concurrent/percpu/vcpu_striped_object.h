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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_STRIPED_OBJECT_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_STRIPED_OBJECT_H_

// VcpuStripedObject<T> is a flat-virtual-CPU-sharded container designed as a
// memory-thrifty alternative to concurrent::percpu::PerCpu<T>.
//
// Comparison & Tradeoffs vs. PerCpu<T>:
// ------------------------------------
// * Memory footprint:
//   - PerCpu<T> eagerly preallocates instances across all physical CPUs
//     (NumCPUs(), typically 128-256+ on modern servers) from a shared global
//     mmap handle pool.
//   - VcpuStripedObject shards by dense virtual CPU IDs. Untouched
//     objects consume only 16 bytes (pointer + mutex). On access,
//     storage is allocated dynamically for active vCPUs touched by the
//     application (e.g. 1-4 slots rather than 256).
// * Performance under concurrency:
//   - Achieves essentially the same ultra-low uncontended latency (~2.7 ns
//     via RSEQ fast path) and linear multi-threaded scaling as PerCpu<T>.
// * Iteration & aggregation speed:
//   - Iterating over VcpuStripedObject (range-based for or ForEach) is usually
//     significantly faster than PerCpu<T> because it only sweeps the few active
//     virtual CPU slots (e.g. 1-4 slots) rather than all 128-256+ physical CPU
//     regions.
//
// Usage:
// ------
// For default-constructible types:
//
//   concurrent::percpu::VcpuStripedObject<MyCounter> counter_;
//   ...
//   counter_.get()->Add(1);
//   for (const MyCounter& c : counter_) { ... }
//   // or: counter_.ForEach([](const MyCounter& c) { ... });
//
// For types requiring constructor arguments:
//
//   // Holds constructor arguments forwarded to each lazily-allocated slot:
//   concurrent::percpu::VcpuStripedObject<HeavyHitters, size_t> hh_{100};

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "gloop/concurrent/percpu/per_vcpu_spin_lock.h"
#include "tcmalloc/malloc_extension.h"

namespace concurrent {
namespace percpu {

template <typename T, typename... Args>
class VcpuStripedObject {
 public:
  // Deleter that releases a per-vCPU slot's spin-lock word.
  struct Unlocker;

  // A move-only smart pointer to one striped T instance, holding that
  // instance's slot lock for its lifetime. Keep its lifetime as short as
  // possible; acquiring two pointers on the same thread that resolve to the
  // same instance will deadlock.
  using pointer = std::unique_ptr<T, Unlocker>;

  template <typename ValueType>
  class IteratorImpl;

  // Move-only, single-pass input iterators over live striped T instances.
  // begin() snapshots the current table and starts a sweep: for its duration
  // the object is in slow mode and the iterator holds the exclusive lock on
  // the slot currently visited. Advancing (++it) releases that slot and
  // acquires the next. The sweep ends -- and fast mode is restored -- as soon
  // as the iterator steps past the last slot (becoming equal to end()) or is
  // destroyed, whichever comes first.
  //
  // Slots created by a promotion that happens after begin() are not visited
  // by that sweep.
  //
  // Keep loops short and focused; range-based for is preferred:
  //
  //   for (const T& val : striped_obj) {
  //     total += val;
  //   }
  //
  // NOTE: Holding an iterator while calling get(), begin(), or ForEach() on the
  // same object from the same thread will deadlock if the slots collide.
  using iterator = IteratorImpl<T>;
  using const_iterator = IteratorImpl<const T>;

  VcpuStripedObject() = default;

  template <size_t N = sizeof...(Args), std::enable_if_t<(N > 0), int> = 0>
  explicit VcpuStripedObject(Args... args) : ctor_args_(std::move(args)...) {}
  ~VcpuStripedObject();

  VcpuStripedObject(const VcpuStripedObject&) = delete;
  VcpuStripedObject(VcpuStripedObject&&) = delete;
  VcpuStripedObject& operator=(const VcpuStripedObject&) = delete;
  VcpuStripedObject& operator=(VcpuStripedObject&&) = delete;

  // Returns a locked pointer to the T instance for the current VCPU. Fast path
  // (~2-3 ns on x86_64) when uncontended; otherwise promotes table capacity or
  // waits on the local vCPU slot.
  //
  // NOTE: The returned pointer holds the lock; attempting to call 'get()' or
  // 'ForEach' again while holding this pointer will likely deadlock.
  //
  // NOTE: This function may allocate or block on a mutex; it is not
  // signal-safe.
  [[nodiscard]] pointer get()
      ABSL_ATTRIBUTE_LIFETIME_BOUND ABSL_NO_THREAD_SAFETY_ANALYSIS;

  [[nodiscard]] iterator begin() ABSL_ATTRIBUTE_LIFETIME_BOUND
      ABSL_LOCKS_EXCLUDED(promotion_mu_);
  [[nodiscard]] iterator end() ABSL_ATTRIBUTE_LIFETIME_BOUND;
  [[nodiscard]] const_iterator begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND
      ABSL_LOCKS_EXCLUDED(promotion_mu_);
  [[nodiscard]] const_iterator end() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  [[nodiscard]] const_iterator cbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND
      ABSL_LOCKS_EXCLUDED(promotion_mu_) {
    return begin();
  }
  [[nodiscard]] const_iterator cend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return end();
  }

  // Non-blocking variant of get(). Returns a null pointer instead of waiting
  // when the current vCPU's slot is held by another thread, is being visited
  // by a sweep, or when the slot table is being promoted concurrently.
  //
  // If `can_alloc` is true (default), promotes table capacity if the current
  // vCPU is beyond the current capacity, giving up (returning a null pointer)
  // if another thread is currently promoting. If `can_alloc` is false, returns
  // a null pointer without allocating or acquiring `promotion_mu_` when the
  // current vCPU slot has not yet been promoted (for use inside allocator or
  // synchronization profiling hooks where heap allocation or `absl::Mutex`
  // re-entrancy is unsafe).
  //
  // WARNING: Do not spin on `try_get()` in a retry loop. Callers that want to
  // wait for the slot must call `get()` (which backs off appropriately).
  [[nodiscard]] pointer try_get(bool can_alloc = true);

  // Iterates over every live 'T' instance, invoking fn(T&) with each
  // instance exclusively locked, one slot at a time. For the duration of the
  // sweep every slot is in slow mode (see per_vcpu_spin_lock.h), so a mutator
  // whose slot is not the one being visited proceeds by CAS rather than
  // waiting.
  //
  // Keep the visitor fast: the mutator on the slot being visited waits for it.
  //
  // Concurrent ForEach calls on the same object are permitted and do not block
  // one another. Slots created concurrently, by a first touch from a vCPU
  // beyond the current capacity, may not be visited by a sweep already in
  // progress.
  template <typename Fn>
  void ForEach(Fn&& fn) ABSL_LOCKS_EXCLUDED(promotion_mu_);

  // Same as above, but const-qualified.
  template <typename Fn>
  void ForEach(Fn&& fn) const ABSL_LOCKS_EXCLUDED(promotion_mu_);

  // Returns current promoted slot capacity. 0 when untouched.
  [[nodiscard]] size_t capacity() const {
    Table* absl_nullable t = table_.load(std::memory_order_acquire);
    return t != nullptr ? static_cast<size_t>(t->capacity) : 0;
  }
  [[nodiscard]] size_t size() const { return capacity(); }
  [[nodiscard]] bool empty() const { return capacity() == 0; }

 private:
  friend struct VcpuStripedObjectTestPeer;

  // --- Slot / Table layout -------------------------------------------------

  // Each slot is isolated to its own cache line to prevent false sharing
  // across distinct vCPUs.
  struct alignas(64) Slot {
    per_vcpu_internal::LockWord lock;
    alignas(T) unsigned char val_storage[sizeof(T)];

    T* absl_nonnull val_ptr() noexcept {
      return std::launder(reinterpret_cast<T*>(val_storage));
    }
    const T* absl_nonnull val_ptr() const noexcept {
      return std::launder(reinterpret_cast<const T*>(val_storage));
    }

    // Call immediately after acquiring `lock`, and immediately before releasing
    // it. In debug builds these assert that the slot is owned by at most one
    // thread at a time.
    //
    // This is the only check that can catch a violation. The RSEQ fast path
    // acquires `lock` with a non-atomic store issued from inline asm, which
    // ThreadSanitizer cannot instrument, so a protocol bug that handed one slot
    // to two owners would produce silent data corruption rather than a report.
    void DebugAcquired() {
#ifndef NDEBUG
      const int before = debug_owners.fetch_add(1, std::memory_order_relaxed);
      DCHECK_EQ(before, 0) << "slot acquired while already owned";
#endif
    }
    void DebugReleasing() {
#ifndef NDEBUG
      const int before = debug_owners.fetch_sub(1, std::memory_order_relaxed);
      DCHECK_EQ(before, 1) << "slot released while not owned";
#endif
    }

    void LockSlow() {
      per_vcpu_internal::LockSlow(lock);
      DebugAcquired();
    }

    void Unlock() noexcept {
      DebugReleasing();
      per_vcpu_internal::UnlockWord(lock);
    }

#ifndef NDEBUG
    std::atomic<int> debug_owners{0};
#endif
  };

  // Table uses a trailing flexible array member pattern:
  // memory for `slots` is allocated contiguously with `Table` via AllocTable()
  // to hold `capacity` atomic pointers.
  struct Table {
    Table* absl_nullable prev = nullptr;  // Linked list of retired tables.
    int64_t capacity = 0;                 // Immutable capacity of `slots`.
    int32_t active_sweeps = 0;            // Guarded by `promotion_mu_`.
    std::atomic<int64_t> fast_capacity{
        0};  // `capacity` in fast mode, `0` in slow mode.
    std::atomic<Slot* absl_nonnull> slots[1];  // Flexible array [capacity].
  };

  static size_t TableAllocBytes(int64_t capacity) {
    DCHECK_GE(capacity, 1);
    return sizeof(Table) + (capacity - 1) * sizeof(std::atomic<Slot*>);
  }

  static Table* absl_nonnull AllocTable(int min_capacity) {
    const size_t min_bytes = TableAllocBytes(min_capacity);
    const __sized_ptr_t alloc = __size_returning_new(min_bytes);
    const int capacity = static_cast<int>(
        (alloc.n - sizeof(Table)) / sizeof(std::atomic<Slot*>) + 1);
    Table* t = new (alloc.p) Table();
    t->capacity = capacity;
    t->fast_capacity.store(capacity, std::memory_order_relaxed);
    for (int i = 0; i < capacity; ++i) {
      // Default construct because we will initialize slots in Promote().
      new (&t->slots[i]) std::atomic<Slot*>;
    }
    return t;
  }

  static void FreeTable(Table* absl_nonnull t) {
    const size_t total_bytes = TableAllocBytes(t->capacity);
    ::operator delete(t, total_bytes);
  }

  // Wraps a freshly acquired slot. Every acquire that reaches a `get()` caller
  // -- RSEQ fast path, CAS slow path, and the blocking retry loop -- funnels
  // through here, so this is where ownership is recorded.
  pointer SlotPointer(Slot* absl_nonnull s) {
    s->DebugAcquired();
    return pointer(s->val_ptr(), Unlocker{s});
  }

  // Blocking slot acquisition used when table is unpromoted, slot is contended,
  // or table is in slow mode.
  ABSL_ATTRIBUTE_NOINLINE pointer AcquireLocalSlotWaiting() {
    int iteration = 0;
    for (;;) {
      const int vcpu = per_vcpu_internal::CurrentVcpu();
      Table* absl_nullable t = table_.load(std::memory_order_acquire);
      if (ABSL_PREDICT_FALSE(t == nullptr || vcpu >= t->capacity)) {
        t = Promote(vcpu);
      }

      Slot* absl_nullable s = per_vcpu_internal::TryLockLocalSlot(t, vcpu);
      if (ABSL_PREDICT_TRUE(s != nullptr)) {
        return SlotPointer(s);
      }

      per_vcpu_internal::LockDelay(++iteration);
    }
  }

  // Single non-blocking acquisition attempt used by try_get() when the RSEQ
  // fast path fails. If `can_alloc` is true, promotes the table if needed,
  // giving up (returning a null pointer) rather than waiting for
  // `promotion_mu_`.
  ABSL_ATTRIBUTE_NOINLINE pointer TryAcquireLocalSlotOnce(bool can_alloc) {
    const int vcpu = per_vcpu_internal::CurrentVcpu();
    Table* absl_nullable t = table_.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(t == nullptr || vcpu >= t->capacity)) {
      if (!can_alloc || !promotion_mu_.try_lock()) {
        return pointer();
      }
      t = PromoteLocked(vcpu);
      promotion_mu_.unlock();
    }
    Slot* absl_nullable s = per_vcpu_internal::TryLockLocalSlot(t, vcpu);
    return s != nullptr ? SlotPointer(s) : pointer();
  }

  // Ensures the table exists and its capacity covers `min_vcpu`. Sized to
  // `min_vcpu + 1` (or higher if the allocator returns excess capacity).
  // Serialized by `promotion_mu_`. If resizing is required, the existing Slot*
  // pointers are copied across to the new Table, and the old Table is linked
  // into `prev` for deferred cleanup in ~.
  Table* absl_nonnull Promote(int min_vcpu) ABSL_LOCKS_EXCLUDED(promotion_mu_) {
    absl::MutexLock l(promotion_mu_);
    return PromoteLocked(min_vcpu);
  }

  Table* absl_nonnull PromoteLocked(int min_vcpu)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(promotion_mu_) {
    Table* absl_nullable cur = table_.load(std::memory_order_acquire);
    if (cur != nullptr && cur->capacity > min_vcpu) {
      return cur;
    }

    Table* absl_nonnull new_table = AllocTable(min_vcpu + 1);
    new_table->prev = cur;

    int active_sweeps = 0;
    const int cur_cap = (cur != nullptr) ? static_cast<int>(cur->capacity) : 0;
    if (cur != nullptr) {
      active_sweeps = cur->active_sweeps;
      // Retire old table from fast path so any thread that already loaded `cur`
      // before `table_.store(new_table)` falls back and reloads `table_`.
      cur->fast_capacity.store(0, std::memory_order_relaxed);
    }
    new_table->active_sweeps = active_sweeps;
    new_table->fast_capacity.store(active_sweeps > 0 ? 0 : new_table->capacity,
                                   std::memory_order_relaxed);

    for (int i = 0; i < cur_cap; ++i) {
      new_table->slots[i].store(cur->slots[i].load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
    }
    for (int i = cur_cap; i < new_table->capacity; ++i) {
      Slot* s = new Slot();
      std::apply(
          [s](const auto&... a) {
            std::construct_at(reinterpret_cast<T*>(s->val_storage), a...);
          },
          ctor_args_);
      new_table->slots[i].store(s, std::memory_order_relaxed);
    }

    table_.store(new_table, std::memory_order_release);
    return new_table;
  }

  // Starts a slow-mode sweep over the current table, returning the table to
  // sweep or nullptr if the object is untouched.
  Table* absl_nullable BeginSweep() const ABSL_LOCKS_EXCLUDED(promotion_mu_) {
    Table* absl_nullable head;
    {
      absl::MutexLock l(promotion_mu_);
      head = table_.load(std::memory_order_relaxed);
      if (head == nullptr) return nullptr;
      if (++head->active_sweeps == 1) {
        head->fast_capacity.store(0, std::memory_order_relaxed);
      }
    }
    // Issue a global Fence() to drain any fast-path commit store already in
    // flight before acquiring slots via LockSlow.
    per_vcpu_internal::FenceFastPath();
    return head;
  }

  // Decrements active sweep count and restores fast mode when no sweeps remain.
  void EndSweep() const ABSL_LOCKS_EXCLUDED(promotion_mu_) {
    absl::MutexLock l(promotion_mu_);
    Table* absl_nonnull cur = table_.load(std::memory_order_relaxed);
    DCHECK_GE(cur->active_sweeps, 1);
    if (--cur->active_sweeps == 0) {
      cur->fast_capacity.store(cur->capacity, std::memory_order_release);
    }
  }

  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS std::tuple<Args...> ctor_args_{};
  // Guards creation of new tables in `Promote()` and updates to
  // `active_sweeps` / `fast_capacity`.
  mutable absl::Mutex promotion_mu_;
  std::atomic<Table* absl_nullable> table_{nullptr};
};

template <typename T, typename... Args>
struct VcpuStripedObject<T, Args...>::Unlocker {
  Unlocker() = default;
  explicit Unlocker(Slot* absl_nonnull s) : slot(s) { DCHECK(slot != nullptr); }

  Slot* absl_nullable slot = nullptr;

  void operator()(T* absl_nonnull /*val*/) const noexcept {
    DCHECK(slot != nullptr);
    slot->Unlock();
  }
};

template <typename T, typename... Args>
template <typename ValueType>
class VcpuStripedObject<T, Args...>::IteratorImpl {
 public:
  // Single-pass: the iterator is move-only and each advance releases the
  // previously visited slot.
  using iterator_concept = std::input_iterator_tag;
  using iterator_category = std::input_iterator_tag;
  using value_type = std::remove_cv_t<ValueType>;
  using difference_type = std::ptrdiff_t;
  using pointer = ValueType*;
  using reference = ValueType&;

  // Constructs an end() iterator.
  IteratorImpl() = default;

  IteratorImpl(IteratorImpl&& other) noexcept { TakeFrom(other); }

  template <typename U,
            std::enable_if_t<!std::is_same_v<U, ValueType> &&
                                 std::is_convertible_v<U*, ValueType*>,
                             int> = 0>
  // NOLINTNEXTLINE(google-explicit-constructor)
  IteratorImpl(IteratorImpl<U>&& other) noexcept {
    TakeFrom(other);
  }

  IteratorImpl& operator=(IteratorImpl&& other) noexcept {
    if (this != &other) {
      Reset();
      TakeFrom(other);
    }
    return *this;
  }

  template <typename U,
            std::enable_if_t<!std::is_same_v<U, ValueType> &&
                                 std::is_convertible_v<U*, ValueType*>,
                             int> = 0>
  IteratorImpl& operator=(IteratorImpl<U>&& other) noexcept {
    Reset();
    TakeFrom(other);
    return *this;
  }

  IteratorImpl(const IteratorImpl&) = delete;
  IteratorImpl& operator=(const IteratorImpl&) = delete;

  ~IteratorImpl() { Reset(); }

  [[nodiscard]] reference operator*() const {
    DCHECK(slot_ != nullptr) << "dereferencing end()";
    return *slot_->val_ptr();
  }

  [[nodiscard]] pointer operator->() const {
    DCHECK(slot_ != nullptr) << "dereferencing end()";
    return slot_->val_ptr();
  }

  // Releases the current slot and acquires the next one. On stepping past the
  // last slot of the swept table, the sweep lease is released immediately (the
  // iterator becomes equal to end()), so fast mode is restored without waiting
  // for the iterator to be destroyed.
  IteratorImpl& operator++() {
    DCHECK(slot_ != nullptr) << "incrementing end()";
    UnlockSlot();
    DCHECK(head_ != nullptr);
    if (ABSL_PREDICT_TRUE(++index_ < head_->capacity)) {
      LockSlot();
    } else {
      Reset();
    }
    return *this;
  }

  // C++20 single-pass input iterator postfix increment returns void because
  // the previous iterator state cannot be copied.
  void operator++(int) { ++*this; }

  // Two distinct iterators compare equal only if both are at end(): an active
  // iterator holds an exclusive lock on its slot, so no two distinct iterators
  // can point to the same slot simultaneously.
  template <typename U>
  [[nodiscard]] bool operator==(const IteratorImpl<U>& other) const {
    return slot_ == other.slot_;
  }

  template <typename U>
  [[nodiscard]] bool operator!=(const IteratorImpl<U>& other) const {
    return slot_ != other.slot_;
  }

 private:
  template <typename U>
  friend class IteratorImpl;
  friend class VcpuStripedObject;

  // Starts a sweep over `head` (the table snapshotted by `BeginSweep()`).
  // Slots created by a later promotion are not visited by this sweep.
  IteratorImpl(const VcpuStripedObject* absl_nonnull parent,
               Table* absl_nullable head) {
    if (ABSL_PREDICT_TRUE(head != nullptr)) {
      parent_ = parent;
      head_ = head;
      index_ = 0;
      LockSlot();
    }
  }

  template <typename U>
  void TakeFrom(IteratorImpl<U>& other) noexcept {
    parent_ = other.parent_;
    head_ = other.head_;
    index_ = other.index_;
    slot_ = other.slot_;
    other.parent_ = nullptr;
    other.head_ = nullptr;
    other.index_ = 0;
    other.slot_ = nullptr;
  }

  void LockSlot() {
    DCHECK(head_ != nullptr);
    slot_ = head_->slots[index_].load(std::memory_order_relaxed);
    ABSL_ASSUME(slot_ != nullptr);
    slot_->LockSlow();
  }

  void UnlockSlot() {
    DCHECK(slot_ != nullptr);
    slot_->Unlock();
    slot_ = nullptr;
  }

  // Releases the current slot (if any) and the sweep lease (if any), leaving
  // the iterator equal to end().
  void Reset() {
    if (slot_ != nullptr) UnlockSlot();
    if (parent_ != nullptr) {
      parent_->EndSweep();
      parent_ = nullptr;
    }
    head_ = nullptr;
    index_ = 0;
  }

  // Invariant: either all four are null/zero (end()), or all of `parent_`,
  // `head_`, and `slot_` are non-null with 0 <= index_ < head_->capacity and
  // `slot_ == head_->slots[index_]` locked by this iterator.
  const VcpuStripedObject* absl_nullable parent_ = nullptr;
  Table* absl_nullable head_ = nullptr;
  int64_t index_ = 0;
  Slot* absl_nullable slot_ = nullptr;
};

template <typename T, typename... Args>
inline VcpuStripedObject<T, Args...>::~VcpuStripedObject() {
  static_assert(std::is_standard_layout_v<Slot>);
  Table* absl_nullable head = table_.load(std::memory_order_relaxed);
  if (head != nullptr) {
    DCHECK_EQ(head->active_sweeps, 0) << "destroyed while sweep active";
    for (int i = 0; i < head->capacity; ++i) {
      Slot* s = head->slots[i].load(std::memory_order_relaxed);
      if (s != nullptr) {
        DCHECK_EQ(s->lock.load(std::memory_order_relaxed), 0u)
            << "slot " << i << " destroyed while locked";
        std::destroy_at(s->val_ptr());
        delete s;
      }
      static_assert(std::is_trivially_destructible_v<decltype(head->slots[i])>);
    }
  }
  Table* absl_nullable t = head;
  while (t != nullptr) {
    Table* absl_nullable prev = t->prev;
    FreeTable(t);
    t = prev;
  }
}

template <typename T, typename... Args>
inline typename VcpuStripedObject<T, Args...>::pointer
VcpuStripedObject<T, Args...>::get() {
  Table* absl_nullable t = table_.load(std::memory_order_acquire);
  if (ABSL_PREDICT_TRUE(t != nullptr)) {
    Slot* absl_nullable s = per_vcpu_internal::RseqTryLockVcpuPointerFast(t);
    if (ABSL_PREDICT_TRUE(s != nullptr)) {
      return SlotPointer(s);
    }
  }
  return AcquireLocalSlotWaiting();
}

template <typename T, typename... Args>
inline typename VcpuStripedObject<T, Args...>::pointer
VcpuStripedObject<T, Args...>::try_get(bool can_alloc)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  Table* absl_nullable t = table_.load(std::memory_order_acquire);
  if (ABSL_PREDICT_TRUE(t != nullptr)) {
    Slot* absl_nullable s = per_vcpu_internal::RseqTryLockVcpuPointerFast(t);
    if (ABSL_PREDICT_TRUE(s != nullptr)) {
      return SlotPointer(s);
    }
  }
  return TryAcquireLocalSlotOnce(can_alloc);
}

template <typename T, typename... Args>
inline typename VcpuStripedObject<T, Args...>::iterator
VcpuStripedObject<T, Args...>::begin() {
  return iterator(/*parent=*/this, /*head=*/BeginSweep());
}

template <typename T, typename... Args>
inline typename VcpuStripedObject<T, Args...>::iterator
VcpuStripedObject<T, Args...>::end() {
  return iterator();
}

template <typename T, typename... Args>
inline typename VcpuStripedObject<T, Args...>::const_iterator
VcpuStripedObject<T, Args...>::begin() const {
  return const_iterator(/*parent=*/this, /*head=*/BeginSweep());
}

template <typename T, typename... Args>
inline typename VcpuStripedObject<T, Args...>::const_iterator
VcpuStripedObject<T, Args...>::end() const {
  return const_iterator();
}

template <typename T, typename... Args>
template <typename Fn>
inline void VcpuStripedObject<T, Args...>::ForEach(Fn&& fn) {
  for (T& val : *this) {
    fn(val);
  }
}

template <typename T, typename... Args>
template <typename Fn>
inline void VcpuStripedObject<T, Args...>::ForEach(Fn&& fn) const {
  for (const T& val : *this) {
    fn(val);
  }
}

}  // namespace percpu
}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_VCPU_STRIPED_OBJECT_H_
