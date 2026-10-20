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

#include "gloop/concurrent/percpu/vcpu_striped_object.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "gloop/base/percpu.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace concurrent {
namespace percpu {

struct VcpuStripedObjectTestPeer {
  template <typename T, typename... Args>
  static void Promote(VcpuStripedObject<T, Args...>& obj, int min_vcpu) {
    obj.Promote(min_vcpu);
  }

  // Acquires `slot_index` via slow-path CAS.
  // WARNING: Bypasses RSEQ fast-path mutual exclusion. Use only in
  // single-threaded test setup or when slow mode is already held by an active
  // sweep.
  template <typename T, typename... Args>
  static typename VcpuStripedObject<T, Args...>::pointer AcquireSlot(
      VcpuStripedObject<T, Args...>& obj, int slot_index) {
    typename VcpuStripedObject<T, Args...>::Table* t = obj.Promote(slot_index);
    typename VcpuStripedObject<T, Args...>::Slot* s =
        t->slots[slot_index].load(std::memory_order_relaxed);
    s->LockSlow();
    return typename VcpuStripedObject<T, Args...>::pointer(
        s->val_ptr(), typename VcpuStripedObject<T, Args...>::Unlocker{s});
  }

  // Number of sweeps currently holding a lease on the object's current table.
  template <typename T, typename... Args>
  static int ActiveSweeps(const VcpuStripedObject<T, Args...>& obj) {
    absl::MutexLock l(obj.promotion_mu_);
    typename VcpuStripedObject<T, Args...>::Table* t =
        obj.table_.load(std::memory_order_relaxed);
    return t != nullptr ? t->active_sweeps : 0;
  }

  // Whether the object's current table permits the RSEQ fast path. An
  // untouched object has no table and is reported as fast.
  template <typename T, typename... Args>
  static bool InFastMode(const VcpuStripedObject<T, Args...>& obj) {
    absl::MutexLock l(obj.promotion_mu_);
    typename VcpuStripedObject<T, Args...>::Table* t =
        obj.table_.load(std::memory_order_relaxed);
    return t == nullptr ||
           t->fast_capacity.load(std::memory_order_relaxed) == t->capacity;
  }
};

namespace {

// Helper to spawn a joinable gloop thread conforming to Google3 style.
std::unique_ptr<::ClosureThread> MakeThread(absl::string_view name,
                                            std::function<void()> fn) {
  thread::Options options;
  options.set_joinable(true);
  auto t = std::make_unique<::ClosureThread>(
      options, name, [func = std::move(fn)]() { func(); });
  t->Start();
  return t;
}

// Number of slots visited by a full sweep.
template <typename Obj>
std::ptrdiff_t CountSlots(Obj& obj) {
  std::ptrdiff_t n = 0;
  for (typename Obj::const_iterator it = obj.cbegin(); it != obj.cend(); ++it) {
    ++n;
  }
  return n;
}

}  // namespace

// -------------------------------------------------------------------------
// Core Contract & Promotion Tests
// -------------------------------------------------------------------------

TEST(VcpuStripedObjectTest, FirstTouchAllocationAndCapacity) {
  VcpuStripedObject<int64_t> obj;
  EXPECT_TRUE(obj.empty());
  EXPECT_EQ(obj.size(), 0);
  EXPECT_EQ(obj.capacity(), 0);

  // Const ForEach on an empty object is a safe no-op.
  int num_visited = 0;
  const VcpuStripedObject<int64_t>& const_obj = obj;
  const_obj.ForEach([&](auto& v) {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(v)>>);
    ++num_visited;
  });
  EXPECT_EQ(num_visited, 0);

  {
    VcpuStripedObject<int64_t>::pointer p = obj.get();
    *p += 42;
  }
  EXPECT_FALSE(obj.empty());
  EXPECT_GT(obj.capacity(), 0);
  EXPECT_EQ(obj.size(), obj.capacity());

  int64_t total = 0;
  obj.ForEach([&](int64_t& v) { total += v; });
  EXPECT_EQ(total, 42);

  // Const ForEach on a populated object visits all slots immutably.
  int64_t const_total = 0;
  const_obj.ForEach([&](auto& v) {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(v)>>);
    const_total += v;
  });
  EXPECT_EQ(const_total, 42);

  // Footprint: sizeof(VcpuStripedObject<T>) is independent of sizeof(T) and
  // takes only 16 bytes for default construction (empty-object optimization).
  using LargeObject = VcpuStripedObject<std::array<char, 256>>;
  EXPECT_LE(sizeof(LargeObject), 16);
}

TEST(VcpuStripedObjectTest, PromotionDoesNotRoundToPowerOfTwo) {
  VcpuStripedObject<int64_t> obj;
  for (int min_vcpu : {4, 8, 16, 32, 64}) {
    VcpuStripedObjectTestPeer::Promote(obj, min_vcpu);
    EXPECT_GE(obj.capacity(), static_cast<size_t>(min_vcpu + 1));
    EXPECT_LT(obj.capacity(), static_cast<size_t>(2 * min_vcpu));
  }
}

TEST(VcpuStripedObjectTest, PointerMoveSemantics) {
  VcpuStripedObject<int64_t> obj;
  {
    VcpuStripedObject<int64_t>::pointer p1 = obj.get();
    *p1 = 10;
    // Move-construction transfers lock ownership.
    VcpuStripedObject<int64_t>::pointer p2(std::move(p1));
    *p2 += 5;
    // Move-assignment transfers lock ownership.
    VcpuStripedObject<int64_t>::pointer p3;
    p3 = std::move(p2);
    *p3 += 15;
    // Self-move assignment is a safe no-op.
    VcpuStripedObject<int64_t>::pointer& p3_ref = p3;
    p3 = std::move(p3_ref);
    EXPECT_EQ(*p3, 30);
    // p3 destructs and releases lock.
  }
  // Re-acquire succeeds without deadlock.
  {
    VcpuStripedObject<int64_t>::pointer p4 = obj.get();
    *p4 += 10;
  }
  // Cross-slot active-to-active move assignment releases the target's old slot
  // lock before taking ownership of the source's slot lock.
  {
    VcpuStripedObject<int64_t>::pointer p_slot0 =
        VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0);
    VcpuStripedObject<int64_t>::pointer p_slot1 =
        VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/1);
    *p_slot1 += 50;
    p_slot0 = std::move(p_slot1);
    *p_slot0 += 5;
  }
  int64_t total = 0;
  obj.ForEach([&](int64_t& v) { total += v; });
  EXPECT_EQ(total, 95);
}

TEST(VcpuStripedObjectTest, TryGet) {
  VcpuStripedObject<int64_t> obj;
  EXPECT_TRUE(obj.empty());

  // With `can_alloc=false`, try_get() must return nullptr without allocating
  // or promoting when the object has no table yet.
  {
    EXPECT_EQ(obj.try_get(/*can_alloc=*/false), nullptr);
  }
  EXPECT_TRUE(obj.empty());

  // First touch promotes the table. A single attempt may spuriously fail (e.g.
  // an RSEQ abort on migration), so bound retries and fall back to get().
  VcpuStripedObject<int64_t>::pointer held;
  for (int i = 0; i < 10 && held == nullptr; ++i) {
    held = obj.try_get();
  }
  if (held == nullptr) {
    held = obj.get();
  }
  EXPECT_FALSE(obj.empty());
  *held = 7;

  // While `held` pins its slot, try_get() never blocks and never hands out the
  // same instance again; it either fails or (after a migration) returns a
  // different vCPU's slot.
  for (int i = 0; i < 1000; ++i) {
    VcpuStripedObject<int64_t>::pointer p = obj.try_get();
    if (p != nullptr) {
      EXPECT_NE(p.get(), held.get());
    }
  }

  // Releasing the pointer makes the slot available again. Once all vCPUs are
  // promoted, `try_get(/*can_alloc=*/false)` must succeed without allocating.
  held.reset();
  VcpuStripedObjectTestPeer::Promote(obj, ::base::subtle::percpu::NumCPUs());
  {
    VcpuStripedObject<int64_t>::pointer again;
    for (int i = 0; i < 10 && again == nullptr; ++i) {
      again = obj.try_get(/*can_alloc=*/false);
    }
    EXPECT_NE(again, nullptr);
  }

  int64_t total = 0;
  obj.ForEach([&](int64_t& v) { total += v; });
  EXPECT_EQ(total, 7);
}

TEST(VcpuStripedObjectTest, MoveOnlyAndNonTrivialTypes) {
  // Move-only std::unique_ptr<int>
  {
    VcpuStripedObject<std::unique_ptr<int>> obj;
    {
      auto p = obj.get();
      *p = std::make_unique<int>(99);
    }
    int seen_val = 0;
    obj.ForEach([&](std::unique_ptr<int>& ptr) {
      if (ptr != nullptr) {
        seen_val = *ptr;
        ptr.reset();
      }
    });
    EXPECT_EQ(seen_val, 99);
  }

  // Non-trivial vector type
  {
    VcpuStripedObject<std::vector<int>> obj;
    {
      auto p = obj.get();
      p->push_back(1);
      p->push_back(2);
    }
    std::vector<int> collected;
    obj.ForEach([&](std::vector<int>& v) {
      collected.insert(collected.end(), v.begin(), v.end());
      v.clear();
    });
    EXPECT_THAT(collected, testing::ElementsAre(1, 2));
  }

  // Non-default-constructible type
  {
    struct NonDefaultConstructible {
      NonDefaultConstructible(int x, std::string s)
          : val(x), label(std::move(s)) {}
      int val;
      std::string label;
    };

    VcpuStripedObject<NonDefaultConstructible, int, std::string> obj(42,
                                                                     "hello");
    {
      auto p = obj.get();
      EXPECT_EQ(p->val, 42);
      EXPECT_EQ(p->label, "hello");
      p->val += 10;
    }
    int seen_val = 0;
    std::string seen_label;
    obj.ForEach([&](NonDefaultConstructible& item) {
      if (item.val != 42) {
        seen_val = item.val;
        seen_label = item.label;
      }
    });
    EXPECT_EQ(seen_val, 52);
    EXPECT_EQ(seen_label, "hello");
  }

  // Non-movable and non-copyable type
  {
    struct ImmovableType {
      ImmovableType(int x, double y) : val(x), weight(y) {}
      ImmovableType(const ImmovableType&) = delete;
      ImmovableType(ImmovableType&&) = delete;
      ImmovableType& operator=(const ImmovableType&) = delete;
      ImmovableType& operator=(ImmovableType&&) = delete;

      int val;
      double weight;
    };

    VcpuStripedObject<ImmovableType, int, double> obj(100, 3.14);
    {
      auto p = obj.get();
      EXPECT_EQ(p->val, 100);
      EXPECT_DOUBLE_EQ(p->weight, 3.14);
      p->val += 5;
    }
    int seen_incremented = 0;
    obj.ForEach([&](ImmovableType& item) {
      if (item.val == 105) {
        ++seen_incremented;
      } else {
        EXPECT_EQ(item.val, 100);
      }
      EXPECT_DOUBLE_EQ(item.weight, 3.14);
    });
    EXPECT_EQ(seen_incremented, 1);
  }
}

TEST(VcpuStripedObjectTest, NonStandardLayoutType) {
  static std::atomic<int> live_instances{0};

  struct BaseWithMember {
    int base_offset = 10;
    virtual ~BaseWithMember() = default;
    virtual int Compute() const { return base_offset; }
  };

  class NonStandardLayout : public BaseWithMember {
   public:
    explicit NonStandardLayout(int init) : public_val(init), secret_(init * 2) {
      live_instances.fetch_add(1, std::memory_order_relaxed);
    }
    ~NonStandardLayout() override {
      live_instances.fetch_sub(1, std::memory_order_relaxed);
    }

    void Add(int delta) {
      public_val += delta;
      secret_ += delta;
    }

    int Compute() const override { return base_offset + public_val + secret_; }

    int public_val;
    // Naming `VcpuStripedObject<T>::pointer` inside `T` while `T` is still
    // incomplete must not prematurely instantiate `Slot`.
    VcpuStripedObject<NonStandardLayout, int>::pointer held_lock;

   private:
    int secret_;
  };
  static_assert(!std::is_standard_layout_v<NonStandardLayout>);

  EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
  {
    VcpuStripedObject<NonStandardLayout, int> obj(5);
    {
      auto p = obj.get();
      EXPECT_EQ(p->Compute(), 25);
      p->Add(3);
      EXPECT_EQ(p->Compute(), 31);
    }
    {
      VcpuStripedObject<NonStandardLayout, int>::pointer p;
      for (int i = 0; i < 10 && p == nullptr; ++i) {
        p = obj.try_get();
      }
      if (p == nullptr) {
        p = obj.get();
      }
      p->Add(2);
    }

    const size_t cap = obj.capacity();
    EXPECT_GT(cap, 0);
    EXPECT_EQ(live_instances.load(std::memory_order_relaxed), cap);

    int total_delta = 0;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      total_delta += (*it).Compute() - 25;
      EXPECT_EQ(it->Compute(), (*it).Compute());
    }
    EXPECT_EQ(total_delta, 10);

    const auto& const_obj = obj;
    int const_total_delta = 0;
    for (auto it = const_obj.cbegin(); it != const_obj.cend(); ++it) {
      const_total_delta += (*it).Compute() - 25;
      EXPECT_EQ(it->Compute(), (*it).Compute());
    }
    EXPECT_EQ(const_total_delta, total_delta);
  }
  EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
}

TEST(VcpuStripedObjectTest, LifetimeAndDestructionTracking) {
  static std::atomic<int> live_instances{0};
  struct Trackable {
    Trackable() { live_instances.fetch_add(1, std::memory_order_relaxed); }
    explicit Trackable(int x) : val(x) {
      live_instances.fetch_add(1, std::memory_order_relaxed);
    }
    Trackable(const Trackable& o) : val(o.val) {
      live_instances.fetch_add(1, std::memory_order_relaxed);
    }
    Trackable(Trackable&& o) noexcept : val(o.val) {
      live_instances.fetch_add(1, std::memory_order_relaxed);
    }
    Trackable& operator=(const Trackable& o) = default;
    Trackable& operator=(Trackable&& o) noexcept {
      val = o.val;
      return *this;
    }
    ~Trackable() { live_instances.fetch_sub(1, std::memory_order_relaxed); }
    int val = 0;
  };

  EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
  {
    VcpuStripedObject<Trackable> obj;
    // 0 live instances initially before first touch.
    EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
    {
      VcpuStripedObject<Trackable>::pointer p = obj.get();
      p->val = 42;
    }
    EXPECT_FALSE(obj.empty());
    const size_t created_instances = obj.capacity();
    EXPECT_GT(created_instances, 0);
    EXPECT_EQ(live_instances.load(std::memory_order_relaxed),
              created_instances);

    // In-place reset via ForEach preserves instances.
    obj.ForEach([](Trackable& t) { t.val = 0; });
    EXPECT_EQ(live_instances.load(std::memory_order_relaxed),
              created_instances);
  }
  // Destruction of VcpuStripedObject destroys all allocated slot instances: 0
  // leaks.
  EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);

  // Also verify lifetime and destruction for parameterized construction.
  EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
  {
    VcpuStripedObject<Trackable, int> obj(/*x=*/99);
    EXPECT_TRUE(obj.empty());
    EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
    {
      VcpuStripedObject<Trackable, int>::pointer p = obj.get();
      EXPECT_EQ(p->val, 99);
    }
    EXPECT_FALSE(obj.empty());
    const size_t created_instances = obj.capacity();
    EXPECT_GT(created_instances, 0);
    EXPECT_EQ(live_instances.load(std::memory_order_relaxed),
              created_instances);
  }
  EXPECT_EQ(live_instances.load(std::memory_order_relaxed), 0);
}

// -------------------------------------------------------------------------
// Multi-Threaded Concurrency & Stress Tests
// -------------------------------------------------------------------------

// Stress test: concurrent writers race to acquire per-vCPU slots via get()
// while dedicated reader threads continuously drain totals via ForEach().
//
// Multiple sweepers are used on purpose: sweeps do not exclude each other, so
// this exercises a slot being held in slow mode by one sweep while another
// fences and acquires it, and mutators acquire it by CAS throughout. The
// drained total checks that no increment was lost or double-counted across all
// of it.
TEST(VcpuStripedObjectTest, ConcurrentLockedMutationAndSweeps) {
  VcpuStripedObject<int64_t> obj;
  constexpr int kPerWriter = 50000;
  const int num_writers = 8;
  const int num_sweepers = 4;
  std::atomic<bool> writers_done{false};
  std::atomic<int64_t> drained_total{0};
  absl::Notification start;

  std::vector<std::unique_ptr<::ClosureThread>> writers;
  writers.reserve(num_writers);
  for (int t = 0; t < num_writers; ++t) {
    writers.push_back(MakeThread(absl::StrCat("writer_", t), [&] {
      start.WaitForNotification();
      for (int i = 0; i < kPerWriter; ++i) {
        auto p = obj.get();
        *p += 1;
      }
    }));
  }

  const auto drain = [&] {
    obj.ForEach([&](int64_t& v) {
      const int64_t val = v;
      v = 0;
      drained_total.fetch_add(val, std::memory_order_relaxed);
    });
  };

  std::vector<std::unique_ptr<::ClosureThread>> sweepers;
  sweepers.reserve(num_sweepers);
  for (int t = 0; t < num_sweepers; ++t) {
    sweepers.push_back(MakeThread(absl::StrCat("sweeper_", t), [&] {
      start.WaitForNotification();
      while (!writers_done.load(std::memory_order_acquire)) {
        drain();
      }
      // Final drain after all writers complete.
      drain();
    }));
  }

  start.Notify();
  for (auto& th : writers) th->Join();
  writers_done.store(true, std::memory_order_release);
  for (auto& th : sweepers) th->Join();

  EXPECT_EQ(drained_total.load(std::memory_order_relaxed),
            int64_t{num_writers} * kPerWriter);
}

// Simple persistent worker pool to dispatch concurrent tasks without thread
// spawn overhead per round.
class WorkerPool {
 public:
  explicit WorkerPool(int num_threads) {
    workers_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
      workers_.push_back(std::make_unique<Worker>());
      Worker* w = workers_.back().get();
      w->thread = MakeThread(absl::StrCat("worker_", i), [w] {
        while (true) {
          std::function<void()> task;
          {
            absl::MutexLock l(w->mu);
            w->mu.Await(absl::Condition(w, &Worker::HasWorkOrStopped));
            if (w->stopped && w->task == nullptr) return;
            task = std::move(w->task);
            w->task = nullptr;
          }
          task();
        }
      });
    }
  }

  ~WorkerPool() {
    for (auto& w : workers_) {
      absl::MutexLock l(w->mu);
      w->stopped = true;
    }
    for (auto& w : workers_) {
      w->thread->Join();
    }
  }

  void Submit(int thread_idx, std::function<void()> task) {
    Worker* w = workers_[thread_idx].get();
    absl::MutexLock l(w->mu);
    w->task = std::move(task);
  }

 private:
  struct Worker {
    bool HasWorkOrStopped() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
      return stopped || task != nullptr;
    }

    absl::Mutex mu;
    std::function<void()> task ABSL_GUARDED_BY(mu);
    bool stopped ABSL_GUARDED_BY(mu) = false;
    std::unique_ptr<::ClosureThread> thread;
  };
  std::vector<std::unique_ptr<Worker>> workers_;
};

// Validates concurrent promotion on unpromoted objects across repeated rounds
// using a persistent thread pool. In each round, all threads are released
// simultaneously via Notification to maximize race conditions on table_ ==
// nullptr and contention/promotion, exercising mixed operations (get(),
// ForEach()).
TEST(VcpuStripedObjectTest, ConcurrentPromotionUnderThreadPool) {
  constexpr int kNumThreads = 16;
  constexpr int kRounds = 50;
  constexpr int kOpsPerThread = 10;

  WorkerPool pool(kNumThreads);

  for (int r = 0; r < kRounds; ++r) {
    auto obj = std::make_unique<VcpuStripedObject<int64_t>>();
    absl::Notification start_wave;
    absl::BlockingCounter done(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
      pool.Submit(t, [&, t]() {
        start_wave.WaitForNotification();
        if (t % 4 == 0) {
          // Reader exercising ForEach during concurrent promotion.
          for (int i = 0; i < kOpsPerThread; ++i) {
            int64_t sum = 0;
            obj->ForEach([&](int64_t& v) { sum += v; });
            EXPECT_GE(sum, 0);
          }
        } else {
          // Mutators taking slot locks.
          for (int i = 0; i < kOpsPerThread; ++i) {
            auto p = obj->get();
            *p += 1;
          }
        }
        done.DecrementCount();
      });
    }

    start_wave.Notify();
    done.Wait();

    // Verify final sum matches number of mutator ops.
    int64_t total = 0;
    obj->ForEach([&](int64_t& v) { total += v; });
    const int num_mutators = kNumThreads - (kNumThreads / 4);
    EXPECT_EQ(total, int64_t{num_mutators} * kOpsPerThread);
  }
}

// -------------------------------------------------------------------------
// Iterator API Tests
// -------------------------------------------------------------------------

TEST(VcpuStripedObjectTest, IteratorTypeTraitsAndBasicUsage) {
  using StripedInt = VcpuStripedObject<int64_t>;
  using Iter = StripedInt::iterator;
  using ConstIter = StripedInt::const_iterator;

  static_assert(std::is_same_v<std::iterator_traits<Iter>::iterator_category,
                               std::input_iterator_tag>);
  static_assert(
      std::is_same_v<std::iterator_traits<Iter>::value_type, int64_t>);
  static_assert(
      std::is_same_v<std::iterator_traits<Iter>::reference, int64_t&>);
  static_assert(std::is_same_v<std::iterator_traits<ConstIter>::reference,
                               const int64_t&>);
  static_assert(std::is_move_constructible_v<Iter>);
  static_assert(std::is_move_assignable_v<Iter>);
  static_assert(!std::is_copy_constructible_v<Iter>);
  static_assert(!std::is_copy_assignable_v<Iter>);
  static_assert(std::is_constructible_v<ConstIter, Iter&&>);
  static_assert(std::is_assignable_v<ConstIter&, Iter&&>);
  static_assert(!std::is_constructible_v<Iter, ConstIter&&>);

  // C++20 std::input_iterator concept conformance for single-pass move-only
  // input iterators.
  static_assert(std::input_iterator<Iter>);
  static_assert(std::input_iterator<ConstIter>);

  StripedInt obj;
  const StripedInt& const_obj = obj;

  // Empty object iteration and heterogeneous iterator comparison.
  EXPECT_EQ(obj.begin(), obj.end());
  EXPECT_EQ(const_obj.begin(), const_obj.end());
  EXPECT_EQ(obj.cbegin(), obj.cend());
  EXPECT_EQ(obj.begin(), obj.cend());
  EXPECT_EQ(const_obj.begin(), obj.end());
  EXPECT_EQ(CountSlots(obj), 0);
  int empty_count = 0;
  for (int64_t v : obj) {
    (void)v;
    ++empty_count;
  }
  EXPECT_EQ(empty_count, 0);
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);

  // Deterministically promote to at least 4 slots and populate slot 0.
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0) = 17;
  const std::ptrdiff_t cap = static_cast<std::ptrdiff_t>(obj.capacity());
  EXPECT_GE(cap, 4);
  EXPECT_EQ(CountSlots(obj), cap);
  EXPECT_EQ(CountSlots(const_obj), cap);

  // Non-empty heterogeneous comparison and postfix operator++(int).
  {
    Iter it = obj.begin();
    EXPECT_NE(it, obj.cend());
    EXPECT_NE(obj.cend(), it);
    it++;  // Postfix increment (returns void per C++20 single-pass input
           // iterator).
    EXPECT_NE(it, obj.cend());
  }
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);

  // Range-based for mutation and const iteration.
  for (int64_t& v : obj) {
    v += 2;
  }
  EXPECT_EQ(std::accumulate(obj.begin(), obj.end(), int64_t{0}), 17 + 2 * cap);
  EXPECT_EQ(std::accumulate(const_obj.begin(), const_obj.end(), int64_t{0}),
            17 + 2 * cap);

  // Struct member access via operator-> on iterator and const_iterator.
  struct Entry {
    int key = 0;
    int val = 0;
  };
  VcpuStripedObject<Entry> struct_obj;
  VcpuStripedObjectTestPeer::Promote(struct_obj, /*min_vcpu=*/3);
  VcpuStripedObjectTestPeer::AcquireSlot(struct_obj, /*slot_index=*/0)->key = 5;
  for (VcpuStripedObject<Entry>::iterator it = struct_obj.begin();
       it != struct_obj.end(); ++it) {
    it->val = it->key + 10;
  }
  int visited = 0;
  for (VcpuStripedObject<Entry>::const_iterator cit = struct_obj.cbegin();
       cit != struct_obj.cend(); ++cit) {
    EXPECT_EQ(cit->val, cit->key + 10);
    ++visited;
  }
  EXPECT_EQ(visited, static_cast<int>(struct_obj.capacity()));
}

TEST(VcpuStripedObjectTest, SweepLeaseReleasedOnReachingEnd) {
  VcpuStripedObject<int64_t> obj;
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));

  VcpuStripedObject<int64_t>::iterator it = obj.begin();
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
  EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // The lease is held throughout the sweep, up to and including the last slot.
  for (size_t i = 1; i < obj.capacity(); ++i) {
    ++it;
    EXPECT_NE(it, obj.end());
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
    EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));
  }

  // Stepping past the last slot releases the lease immediately: the iterator
  // variable is still alive, but the object is back in fast mode.
  ++it;
  EXPECT_EQ(it, obj.end());
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // Destroying the exhausted iterator must not double-release.
  {
    VcpuStripedObject<int64_t>::iterator moved(std::move(it));
    EXPECT_EQ(moved, obj.end());
  }
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));
}

TEST(VcpuStripedObjectTest, SweepLeaseReleasedOnEarlyExit) {
  VcpuStripedObject<int64_t> obj;
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0) = 100;

  // Early break from loop must release both the slot lock and active sweep.
  int visited_before_break = 0;
  for (int64_t& v : obj) {
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
    EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));
    v += 1;
    if (++visited_before_break == 1) {
      break;
    }
  }
  EXPECT_EQ(visited_before_break, 1);
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // Slot 0 was unlocked by the break: re-acquiring it does not deadlock.
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0) += 10;
  EXPECT_EQ(std::accumulate(obj.begin(), obj.end(), int64_t{0}), 111);
}

TEST(VcpuStripedObjectTest, IteratorMoveSemanticsReleaseLease) {
  VcpuStripedObject<int64_t> obj;
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0) = 7;

  // Default-constructed iterators equal end() and hold nothing.
  {
    VcpuStripedObject<int64_t>::iterator default_it;
    EXPECT_EQ(default_it, obj.end());
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
    default_it = obj.begin();
    EXPECT_NE(default_it, obj.end());
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
  }
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);

  // Move-assigning over an active iterator releases its lease and slot before
  // taking over the source's.
  {
    VcpuStripedObject<int64_t>::iterator it1 = obj.begin();
    ++it1;  // Now at slot 1, so a second begin() can lock slot 0.
    VcpuStripedObject<int64_t>::iterator it2 = obj.begin();
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 2);
    it1 = std::move(it2);
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
    EXPECT_EQ(*it1, 7);         // it1 now owns slot 0.
    EXPECT_EQ(it2, obj.end());  // NOLINT(bugprone-use-after-move)

    // Self-move assignment is a safe no-op.
    VcpuStripedObject<int64_t>::iterator& self_ref = it1;
    it1 = std::move(self_ref);
    EXPECT_EQ(*it1, 7);
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);

    // Move-conversion from iterator to const_iterator transfers ownership.
    VcpuStripedObject<int64_t>::const_iterator cit(std::move(it1));
    EXPECT_EQ(it1, obj.end());  // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(*cit, 7);
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);

    // Move-assigning an exhausted (end) iterator over an active one releases
    // the active one's lease.
    VcpuStripedObject<int64_t>::const_iterator exhausted;
    cit = std::move(exhausted);
    EXPECT_EQ(cit, obj.cend());
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
    EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));
  }

  // Verify object is completely unlocked after scope exit: slot 0 can be
  // re-acquired and a full sweep succeeds.
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0) += 1;
  EXPECT_EQ(std::accumulate(obj.begin(), obj.end(), int64_t{0}), 8);
}

TEST(VcpuStripedObjectTest, ExhaustedIteratorMayOutliveObject) {
  // An iterator that has run to end() holds no lease, so the object may be
  // destroyed before the iterator variable goes out of scope.
  VcpuStripedObject<int64_t>::iterator outer_it;
  int visited = 0;
  {
    VcpuStripedObject<int64_t> obj;
    VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
    for (outer_it = obj.begin(); outer_it != obj.end(); ++outer_it) {
      ++visited;
    }
    EXPECT_EQ(visited, static_cast<int>(obj.capacity()));
    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  }
  // `outer_it` is destroyed after `obj`; this must not touch freed memory.
  EXPECT_EQ(outer_it, VcpuStripedObject<int64_t>::iterator());
}

TEST(VcpuStripedObjectTest, SweepSnapshotsTableAcrossPromotion) {
  VcpuStripedObject<int64_t> obj;
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  const size_t old_cap = obj.capacity();
  EXPECT_GE(old_cap, 4);

  VcpuStripedObject<int64_t>::iterator it = obj.begin();
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
  EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // Promote while the sweep is active. The lease carries over to the new
  // table, which must also start in slow mode because it shares the slots
  // the in-flight sweep is visiting.
  VcpuStripedObjectTestPeer::Promote(
      obj,
      /*min_vcpu=*/static_cast<int>(2 * old_cap - 1));
  const size_t new_cap = obj.capacity();
  EXPECT_GE(new_cap, 2 * old_cap);
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
  EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // A newly created slot (beyond the snapshotted table) can be acquired while
  // the sweep is in flight, and is not visited by that sweep.
  *VcpuStripedObjectTestPeer::AcquireSlot(
      obj, /*slot_index=*/static_cast<int>(old_cap)) = 99;

  int visited = 0;
  for (; it != obj.end(); ++it) {
    EXPECT_EQ(*it, 0);
    ++visited;
  }
  EXPECT_EQ(visited, static_cast<int>(old_cap));

  // Finishing the sweep restores fast mode on the *new* table.
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // A fresh sweep sees the promoted table, including the new slot.
  EXPECT_EQ(CountSlots(obj), static_cast<std::ptrdiff_t>(new_cap));
  EXPECT_EQ(std::accumulate(obj.begin(), obj.end(), int64_t{0}), 99);
}

TEST(VcpuStripedObjectTest, OverlappingSweepsShareSlowMode) {
  VcpuStripedObject<int64_t> obj;
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  const size_t cap = obj.capacity();

  VcpuStripedObject<int64_t>::iterator it1 = obj.begin();
  ++it1;  // Free slot 0 so the second sweep can start on this thread.
  VcpuStripedObject<int64_t>::iterator it2 = obj.begin();
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 2);
  EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // Run it1 to completion; it2 still holds a lease so slow mode persists.
  for (size_t i = 1; i < cap; ++i) ++it1;
  EXPECT_EQ(it1, obj.end());
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 1);
  EXPECT_FALSE(VcpuStripedObjectTestPeer::InFastMode(obj));

  // Run it2 to completion; the last lease out restores fast mode.
  for (size_t i = 0; i < cap; ++i) ++it2;
  EXPECT_EQ(it2, obj.end());
  EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
  EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));
}

TEST(VcpuStripedObjectTest, ConcurrentMutationsAndIteratorSweeps) {
  VcpuStripedObject<int64_t> obj;
  constexpr int kPerWriter = 30000;
  const int num_writers = 6;
  const int num_sweepers = 4;
  std::atomic<bool> writers_done{false};
  std::atomic<int64_t> drained_total{0};
  absl::Notification start;

  std::vector<std::unique_ptr<::ClosureThread>> writers;
  writers.reserve(num_writers);
  for (int t = 0; t < num_writers; ++t) {
    writers.push_back(MakeThread(absl::StrCat("writer_", t), [&] {
      start.WaitForNotification();
      for (int i = 0; i < kPerWriter; ++i) {
        *obj.get() += 1;
      }
    }));
  }

  std::vector<std::unique_ptr<::ClosureThread>> sweepers;
  sweepers.reserve(num_sweepers);
  for (int t = 0; t < num_sweepers; ++t) {
    sweepers.push_back(MakeThread(absl::StrCat("sweeper_", t), [&, t] {
      start.WaitForNotification();
      int iter = 0;
      while (!writers_done.load(std::memory_order_acquire)) {
        // Half the time do a partial sweep with early break to stress early
        // iterator destruction under concurrency.
        const bool partial = ((t + iter++) % 2 == 0);
        int count = 0;
        for (int64_t& v : obj) {
          const int64_t val = v;
          v = 0;
          drained_total.fetch_add(val, std::memory_order_relaxed);
          if (partial && ++count >= 2) {
            break;
          }
        }
      }
      // Final full drain using range-based for.
      for (int64_t& v : obj) {
        const int64_t val = v;
        v = 0;
        drained_total.fetch_add(val, std::memory_order_relaxed);
      }
    }));
  }

  start.Notify();
  for (const std::unique_ptr<::ClosureThread>& th : writers) th->Join();
  writers_done.store(true, std::memory_order_release);
  for (const std::unique_ptr<::ClosureThread>& th : sweepers) th->Join();

  EXPECT_EQ(drained_total.load(std::memory_order_relaxed),
            int64_t{num_writers} * kPerWriter);
}

TEST(VcpuStripedObjectTest, SetSomethingAndIterate) {
  VcpuStripedObject<int> obj;
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  *obj.get() = 3;
  const int cap = static_cast<int>(obj.capacity());
  EXPECT_GE(cap, 4);
  std::map<int, int> observations;
  for (int n : obj) {
    ++observations[n];
  }
  EXPECT_THAT(observations,
              testing::UnorderedElementsAre(testing::Pair(0, cap - 1),
                                            testing::Pair(3, 1)));
}

TEST(VcpuStripedObjectTest, InitializationAndIterate) {
  VcpuStripedObject<int, int> obj(/*args=*/1);
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  *obj.get() = 3;
  const int cap = static_cast<int>(obj.capacity());
  EXPECT_GE(cap, 4);
  std::map<int, int> observations;
  for (int n : obj) {
    ++observations[n];
  }
  EXPECT_THAT(observations,
              testing::UnorderedElementsAre(testing::Pair(1, cap - 1),
                                            testing::Pair(3, 1)));
}

TEST(VcpuStripedObjectTest, DifferentVcpusAndAccumulate) {
  VcpuStripedObject<int> obj;
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/0) = 1;
  *VcpuStripedObjectTestPeer::AcquireSlot(obj, /*slot_index=*/1) = 2;
  EXPECT_EQ(std::accumulate(obj.begin(), obj.end(), 0), 3);
}

TEST(VcpuStripedObjectTest, ConcurrentMultiHopPromotionAndPartialSweeps) {
  constexpr int kRounds = 40;
  constexpr int kNumSweepers = 4;
  constexpr int kNumMutators = 4;

  for (int round = 0; round < kRounds; ++round) {
    VcpuStripedObject<int64_t> obj;
    std::atomic<bool> stop{false};
    std::atomic<int64_t> sweeps_completed{0};
    absl::Notification start;

    // Promoter thread walks capacity across multiple power-of-two tiers,
    // waiting at each tier until sweeper threads have actively swept the
    // current table generation so promote/sweep interleavings occur at every
    // hop.
    std::unique_ptr<::ClosureThread> promoter = MakeThread("promoter", [&] {
      start.WaitForNotification();
      for (int min_vcpu : {1, 3, 7, 15, 31, 63}) {
        const int64_t baseline =
            sweeps_completed.load(std::memory_order_acquire);
        while (sweeps_completed.load(std::memory_order_acquire) <
               baseline + kNumSweepers) {
        }
        VcpuStripedObjectTestPeer::Promote(obj, min_vcpu);
      }
      stop.store(true, std::memory_order_release);
    });

    std::vector<std::unique_ptr<::ClosureThread>> mutators;
    mutators.reserve(kNumMutators);
    for (int m = 0; m < kNumMutators; ++m) {
      mutators.push_back(MakeThread(absl::StrCat("mutator_", m), [&] {
        start.WaitForNotification();
        while (!stop.load(std::memory_order_acquire)) {
          *obj.get() += 1;
        }
      }));
    }

    std::vector<std::unique_ptr<::ClosureThread>> sweepers;
    sweepers.reserve(kNumSweepers);
    for (int s = 0; s < kNumSweepers; ++s) {
      sweepers.push_back(MakeThread(absl::StrCat("sweeper_", s), [&, s] {
        start.WaitForNotification();
        int iter = 0;
        while (!stop.load(std::memory_order_acquire)) {
          int count = 0;
          const int max_visit = 1 + ((s + iter++) % 3);
          for (int64_t& v : obj) {
            (void)v;
            if (++count >= max_visit) {
              break;
            }
          }
          sweeps_completed.fetch_add(1, std::memory_order_release);
        }
      }));
    }

    start.Notify();
    promoter->Join();
    for (const std::unique_ptr<::ClosureThread>& th : mutators) th->Join();
    for (const std::unique_ptr<::ClosureThread>& th : sweepers) th->Join();

    EXPECT_EQ(VcpuStripedObjectTestPeer::ActiveSweeps(obj), 0);
    EXPECT_TRUE(VcpuStripedObjectTestPeer::InFastMode(obj));
    EXPECT_GE(obj.capacity(), 64);
  }
}

TEST(VcpuStripedObjectTest, DifferentCacheLines) {
  VcpuStripedObject<int, int> obj(/*args=*/1);
  VcpuStripedObjectTestPeer::Promote(obj, /*min_vcpu=*/3);
  std::set<std::uint64_t> pointers;
  for (int& n : obj) {
    pointers.insert(reinterpret_cast<std::uint64_t>(&n));
  }
  EXPECT_EQ(pointers.size(), obj.capacity());
  EXPECT_GE(pointers.size(), 4);
  std::uint64_t last = *pointers.begin();
  for (std::set<std::uint64_t>::const_iterator it = std::next(pointers.begin());
       it != pointers.end(); ++it) {
    EXPECT_GE(*it - last, ABSL_CACHELINE_SIZE);
    last = *it;
  }
}

}  // namespace percpu
}  // namespace concurrent
