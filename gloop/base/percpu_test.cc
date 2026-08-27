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

#include "gloop/base/percpu.h"

#include <errno.h>
#include <numa.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/unscaledcycleclock.h"
#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/futex.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/percpu_types.h"
#include "gloop/base/proc_maps.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/signal_util_subtle.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/thread.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/gtl/unique_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tcmalloc/internal/linux_syscall_support.h"

using base::internal::AllowedCpus;
using base::internal::ScopedAffinityMask;
using base::subtle::percpu::GetRseqVcpuMode;
using base::subtle::percpu::RseqVcpuMode;

ABSL_FLAG(int32_t, cross_cpu_lock_freq, 25,
          "make 1/this fraction of locks cross cpu.");

namespace gloop_do_not_use {

// friended here:
// http://https://github.com/abseil/abseil-cpp/tree/master/absl/base/internal/unscaledcycleclock.h;l=131;rcl=395982312
class UnscaledCycleClockWrapperForPerCpuTest {
 public:
  static int64_t Now() {
    return absl::base_internal::UnscaledCycleClock::Now();
  }
};

}  // namespace gloop_do_not_use

namespace {
// Chooses an available CPU and executes the passed functor on it.  The
// cpu that is chosen, as long as a valid disjoint remote CPU will be passed
// as arguments to it.
//
// If the functor believes that it has failed in a manner attributable to
// external modification, then it should return false and we will attempt to
// retry the operation (up to a constant limit).
void RunOnSingleCpuWithRemoteCpu(std::function<bool(int, int)> test) {
  constexpr int kMaxTries = 1000;

  for (int i = 0;; i++) {
    auto allowed = AllowedCpus();

    int target_cpu = allowed[0], remote_cpu;

    // We try to pass something actually within the mask, but, for most tests it
    // only needs to exist.
    if (allowed.size() > 1)
      remote_cpu = allowed[1];
    else
      remote_cpu = target_cpu ? 0 : 1;

    ScopedAffinityMask mask({target_cpu});

    // If the test function failed, assert that the mask was tampered with.
    if (!test(target_cpu, remote_cpu))
      CHECK(mask.Tampered());
    else
      break;

    ASSERT_LT(i, kMaxTries);
  }
}

// Equivalent to RunOnSingleCpuWithRemoteCpu, except that only the CPU the
// functor is executing on is passed.
void RunOnSingleCpu(std::function<bool(int)> test) {
  auto wrapper = [&test](int this_cpu, int /*unused*/) {
    return test(this_cpu);
  };
  RunOnSingleCpuWithRemoteCpu(wrapper);
}

using base::subtle::percpu::AllocHandle;
using base::subtle::percpu::AtomicFetchAdd;
using base::subtle::percpu::CompareAndSwap;
using base::subtle::percpu::CompareAndSwapCheck;
using base::subtle::percpu::Fence;
using base::subtle::percpu::FenceCpu;
using base::subtle::percpu::GetCurrentCpu;
using base::subtle::percpu::GetCurrentCpuUnsafe;
using base::subtle::percpu::Handle;
using base::subtle::percpu::HandleFromInt;
using base::subtle::percpu::IntFromHandle;
using base::subtle::percpu::IsFast;
using base::subtle::percpu::NullHandle;
using base::subtle::percpu::PerCpuSpinLock;
using gloop_do_not_use::UnscaledCycleClockWrapperForPerCpuTest;

using thread::Fiber;

using util::functional::ToPermanentCallback;

static void BM_get_current_cpu(benchmark::State& state) {
  base::subtle::percpu::IsFast();

  int cpu = -1;
  for (auto _ : state) {
    cpu = GetCurrentCpu();
  }

  CHECK_GE(cpu, 0);
}
BENCHMARK(BM_get_current_cpu);

static void BM_get_current_cpu_unsafe(benchmark::State& state) {
  if (!base::subtle::percpu::IsFast()) {
    return;
  }

  int cpu = -1;
  for (auto _ : state) {
    cpu = GetCurrentCpuUnsafe();
  }

  CHECK_GE(cpu, 0);
}
BENCHMARK(BM_get_current_cpu_unsafe);

PerCpuSpinLock* g_spinlock = nullptr;
void SetupSpinLock(const benchmark::State&) {
  g_spinlock = new PerCpuSpinLock();
  base::subtle::percpu::IsFast();
  // make sure it's initialized before we start timing:
  g_spinlock->Unlock(g_spinlock->Lock());
}
void TeardownSpinLock(const benchmark::State&) { delete g_spinlock; }

static void BM_PerCpuSpinLock_Lock(benchmark::State& state) {
  for (auto _ : state) {
    int cpu = g_spinlock->Lock();
    g_spinlock->Unlock(cpu);
  }
}
BENCHMARK(BM_PerCpuSpinLock_Lock)
    ->Setup(SetupSpinLock)
    ->Teardown(TeardownSpinLock)
    ->ThreadRange(1, NumCPUs() * 4);

static void BM_PerCpuSpinLock_TryLock(benchmark::State& state) {
  for (auto _ : state) {
    int cpu;
    if (g_spinlock->TryLock(&cpu)) {
      g_spinlock->Unlock(cpu);
    }
  }
}
BENCHMARK(BM_PerCpuSpinLock_TryLock)
    ->Setup(SetupSpinLock)
    ->Teardown(TeardownSpinLock)
    ->ThreadRange(1, NumCPUs() * 4);

// Basically this benchmark is a progress test -- we want to make certain
// we can effectively take the lock against remote competition.
// The window for the retry case is very narrow, so we should be OK.
static void BM_PerCpuSpinLock_LockOn(benchmark::State& state) {
  RunOnSingleCpuWithRemoteCpu([&](int /*this_cpu*/, int remote_cpu) {
    absl::Notification ready;
    Fiber f([&]() {
      ScopedAffinityMask affinity({remote_cpu});
      ready.Notify();
      while (!thread::Cancelled()) {
        int locked_cpu = g_spinlock->Lock();
        // In the case where we're running forced onto a single CPU, we'll
        // always fail the tampered check since it's not possible to set
        if (locked_cpu != remote_cpu) CHECK(affinity.Tampered());
        g_spinlock->Unlock(locked_cpu);
      }
    });
    ready.WaitForNotification();
    for (auto _ : state) {
      g_spinlock->LockOn(remote_cpu);
      g_spinlock->UnlockOn(remote_cpu);
    }
    state.SetItemsProcessed(state.iterations());
    f.Cancel();
    f.Join();
    return true;
  });
}
BENCHMARK(BM_PerCpuSpinLock_LockOn)
    ->Setup(SetupSpinLock)
    ->Teardown(TeardownSpinLock);

static void BM_cmpxchg_increment(benchmark::State& state) {
  int cpu;
  int64_t curr;
  std::atomic<int64_t>* val;
  static Handle vals = AllocHandle();
  base::subtle::percpu::IsFast();

  for (auto _ : state) {
    do {
      cpu = GetCurrentCpu();
      val = GetPointerAtomic(vals, cpu);
      curr = val->load(std::memory_order_relaxed);
    } while (CompareAndSwap(cpu, val, curr, curr + 1) != cpu);
  }
}
BENCHMARK(BM_cmpxchg_increment)->ThreadRange(1, NumCPUs() * 4);

static void BM_counter_fetch_add(benchmark::State& state) {
  static Handle ctr = AllocHandle();
  base::subtle::percpu::IsFast();

  for (auto _ : state) {
    auto previous = AtomicFetchAdd(ctr, 1);
    benchmark::DoNotOptimize(previous);
  }
}
BENCHMARK(BM_counter_fetch_add)->ThreadRange(1, NumCPUs() * 4);

static void BM_cmpxchg_check(benchmark::State& state) {
  static Handle vals = AllocHandle();
  static Handle checks = AllocHandle();
  int cpu;
  int64_t curr, id;
  std::atomic<int64_t>*val, *check;
  base::subtle::percpu::IsFast();

  // Test the double-compare with a system that
  // ensures some sort of transaction ID on each ptr.
  // Don't worry too much about what this code _does_, we just want
  // some invalidations on the check ptr.
  for (auto _ : state) {
    do {
      cpu = GetCurrentCpu();
      val = GetPointerAtomic(vals, cpu);
      check = GetPointerAtomic(checks, cpu);
      curr = val->load(std::memory_order_relaxed);
      id = check->load(std::memory_order_relaxed);
    } while (CompareAndSwapCheck(cpu, val, curr, curr + 1, check, id) != cpu);
    AtomicFetchAdd(checks, 1);
  }
}
BENCHMARK(BM_cmpxchg_check)->ThreadRange(1, NumCPUs() * 4);

static void BackgroundThread(Handle h, std::atomic<int64_t>* sync) {
  while (sync->load(std::memory_order_acquire) == 0) {
    while (true) {
      const int cpu = GetCurrentCpu();
      std::atomic<int64_t>* ptr = GetPointerAtomic(h, cpu);
      const int64_t before = ptr->load(std::memory_order_relaxed);
      const int64_t after = before + 1;
      if (cpu == CompareAndSwap(cpu, ptr, before, after)) {
        break;
      }
    }
  }
}

static void BM_Fence(benchmark::State& state) {
  const int nthreads = state.range(0);
  std::atomic<int64_t> sync{0};
  Handle handle = AllocHandle();

  std::vector<ClosureThread*> threads;
  for (int i = 0; i < nthreads; ++i) {
    auto work = absl::bind_front(BackgroundThread, handle, &sync);

    ClosureThread* thr = new ClosureThread(std::move(work));
    thr->SetJoinable(true);
    thr->Start();
    threads.push_back(thr);
  }
  for (auto _ : state) {
    Fence();
  }
  sync.store(1, std::memory_order_release);
  for (int i = 0; i < nthreads; ++i) {
    threads[i]->Join();
    delete threads[i];
  }
}
BENCHMARK(BM_Fence)->Range(0, NumCPUs());

STATIC_THREAD_LOCAL_POD(int, per_cpu_test_thread_index);

// PerCpuTest is for tests that run some number of interacting threads;
// it's templated across thread count to test a variety of conditions.
class PerCpuTest : public ::testing::TestWithParam<int> {
 public:
  static int TestThreadIndex() {
    // Must be called from our TestThreads
    int i = per_cpu_test_thread_index.get();
    CHECK_GT(i, 0);
    return (i - 1);
  }

 protected:
  class TestThread : public Thread {
   public:
    void Init(Closure* code, int i) {
      code_ = code;
      index_ = i;
    }

   protected:
    Closure* code_;
    int index_;
    void Run() override {
      *per_cpu_test_thread_index.pointer() = index_ + 1;
      code_->Run();
    }
  };

  int nthreads_;
  int num_nodes_;

  // Runs <code> in the test's configured number of threads. Deletes <code>
  // afterward.
  void RunInThreads(Closure* code) {
    auto threads = gtl::MakeUniqueArrayForOverwrite<TestThread>(nthreads_);
    for (int i = 0; i < nthreads_; ++i) {
      threads[i].Init(code, i);
      threads[i].SetJoinable(true);
      threads[i].Start();
    }
    for (int i = 0; i < nthreads_; ++i) {
      threads[i].Join();
    }
    delete code;
  }

  void SetUp() override {
    nthreads_ = GetParam();
    num_nodes_ = numa_max_node() + 1;
    VLOG(1) << (base::subtle::percpu::IsFast() ? "using" : "not using")
            << " RSEQ with " << nthreads_ << " threads";
  }

  int NumThreads() const { return nthreads_; }

  int NumNodes() const { return num_nodes_; }

  int NumThreadsPerNode() const {
    if (nthreads_ < num_nodes_) return nthreads_;
    return nthreads_ / num_nodes_;
  }

  int GetThreadAffineNode(int thread) const {
    CHECK_LE(0, thread);
    CHECK_GT(nthreads_, thread);
    return thread / NumThreadsPerNode();
  }

  int GetThreadIndexInNode(int thread) const {
    return thread % NumThreadsPerNode();
  }
};

void FetchAddRepeatedly(Handle percpu_data, int delta, int n) {
  while (n-- > 0) {
    AtomicFetchAdd(percpu_data, delta);
  }
}

int64_t SumAndClearPtr(Handle percpu_data) {
  int64_t sum = 0;
  for (int i = 0; i < NumCPUs(); ++i) {
    std::atomic<int64_t>* val = GetPointerAtomic(percpu_data, i);
    sum += val->load(std::memory_order_relaxed);
    val->store(0, std::memory_order_relaxed);
  }
  return sum;
}

TEST_P(PerCpuTest, HandleTagged) {
  Handle percpu_data = AllocHandle();
  uint64_t address = absl::bit_cast<uint64_t>(percpu_data.rep);

  bool found = false;
  uint64_t start, end;
  char* filename;
  ProcMapsIterator it(0);
  while (it.Next(&start, &end, nullptr, nullptr, nullptr, &filename)) {
    if (start <= address && address < end) {
      found = true;
      EXPECT_THAT(filename, testing::HasSubstr("percpu_handle_region"));
      break;
    }
  }

  EXPECT_TRUE(found);
  FreeHandle(percpu_data);
}

TEST_P(PerCpuTest, AtomicFetchAddWorks) {
  Handle percpu_data = AllocHandle();

  ASSERT_EQ(0, SumAndClearPtr(percpu_data));
  const int kNumIncrements = 10001;
  int64_t expected;

  // Run with increment one.
  // TODO: Verify the result of fetch add itself, which is dependent
  // on the interleaving of core executions we encounter.
  RunInThreads(::util::functional::ToPermanentCallback(
      absl::bind_front(&FetchAddRepeatedly, percpu_data, 1, kNumIncrements)));

  expected = NumThreads() * kNumIncrements;
  EXPECT_EQ(expected, SumAndClearPtr(percpu_data));

  // Run with a larger increment.
  {
    const int kDelta = 37;
    RunInThreads(::util::functional::ToPermanentCallback(absl::bind_front(
        &FetchAddRepeatedly, percpu_data, kDelta, kNumIncrements)));

    expected = NumThreads() * kNumIncrements * kDelta;
    EXPECT_EQ(expected, SumAndClearPtr(percpu_data));
  }

  // Run with a negative increment.
  {
    const int kDelta = -37;
    RunInThreads(::util::functional::ToPermanentCallback(absl::bind_front(
        &FetchAddRepeatedly, percpu_data, kDelta, kNumIncrements)));

    expected = NumThreads() * kNumIncrements * kDelta;
    EXPECT_EQ(expected, SumAndClearPtr(percpu_data));
  }
}

void SpinlockTest(PerCpuSpinLock* locks, std::vector<int>* counters, int n) {
  for (int i = 0; i < n; ++i) {
    int cpu;
    if (n % 8 == 0) {
      while (!locks->TryLock(&cpu)) {
        // spin
      }
    } else {
      cpu = locks->Lock();
    }
    (*counters)[cpu]++;
    locks->Unlock(cpu);
  }
}

TEST_P(PerCpuTest, SpinlockWorks) {
  PerCpuSpinLock locks;
  std::vector<int> counters(NumCPUs());
  for (int i = 0; i < NumCPUs(); ++i) {
    counters[i] = 0;
  }
  const int kNumIncrements = 12345;

  RunInThreads(::util::functional::ToPermanentCallback(
      absl::bind_front(&SpinlockTest, &locks, &counters, kNumIncrements)));
  int sum = 0;
  for (int i = 0; i < NumCPUs(); ++i) {
    sum += counters[i];
  }
  EXPECT_EQ(NumThreads() * kNumIncrements, sum);
}

TEST_P(PerCpuTest, SpinLockDeletionIsSafe) {
  // Verify that it is safe to delete PerCpuSpinLock immediately after
  // the last Unlock() on it has completed.
  RunOnSingleCpu([](int cpu) {
    auto l = std::make_unique<PerCpuSpinLock>();
    bool done = false;
    Fiber f([&] {
      l->LockOn(cpu);
      done = true;
      l->Unlock(cpu);
    });
    while (true) {
      bool d;
      l->LockOn(cpu);
      d = done;
      l->Unlock(cpu);
      if (d) {
        l.reset();
        break;
      }
    }
    f.Join();
    return true;
  });
}

// Push a bunch of elements onto linked lists on the per-cpu <heads>.
void CASConcurrentTest(Handle heads, std::vector<int64_t>* ptrs, int n,
                       std::atomic<int64_t>* slice) {
  int start = slice->fetch_add(n, std::memory_order_relaxed);
  int end = start + n;

  int cpu;
  int64_t old_val, new_val;
  std::atomic<int64_t>* head;
  for (int i = start; i < end; ++i) {
    do {
      cpu = GetCurrentCpu();
      head = GetPointerAtomic(heads, cpu);
      old_val = head->load(std::memory_order_relaxed);
      int64_t* new_head = &(*ptrs)[i];
      *new_head = old_val;
      new_val = reinterpret_cast<int64_t>(new_head);
    } while (CompareAndSwap(cpu, head, old_val, new_val) != cpu);
  }
}

void SpinlockCrossCpuTest(PerCpuSpinLock* locks, std::vector<int>* counters,
                          int n) {
  absl::BitGen rand(absl::SeedSeq(
      {GTEST_FLAG_GET(random_seed) + PerCpuTest::TestThreadIndex()}));
  int ncpus = NumCPUs();
  struct Helper {
    static void Local(PerCpuSpinLock* locks,
                      std::vector<int>* counters) ABSL_ATTRIBUTE_NOINLINE {
      int cpu = locks->Lock();
      (*counters)[cpu]++;
      locks->Unlock(cpu);
    }

    static void Remote(PerCpuSpinLock* locks, std::vector<int>* counters,
                       int cpu) ABSL_ATTRIBUTE_NOINLINE {
      locks->LockOn(cpu);
      (*counters)[cpu]++;
      locks->UnlockOn(cpu);
    }
  };
  while (n-- > 0) {
    bool local = absl::Uniform<int32_t>(
                     rand, 0, absl::GetFlag(FLAGS_cross_cpu_lock_freq)) == 0;

    if (local) {
      Helper::Local(locks, counters);
    } else {
      int cpu = absl::Uniform<int32_t>(rand, 0, ncpus);
      Helper::Remote(locks, counters, cpu);
    }
  }
}

TEST_P(PerCpuTest, SpinlockCrossCpu) {
  PerCpuSpinLock locks;
  std::vector<int> counters(NumCPUs());
  for (int i = 0; i < NumCPUs(); ++i) {
    counters[i] = 0;
  }

  const int kNumIncrements = 20000;

  RunInThreads(::util::functional::ToPermanentCallback(absl::bind_front(
      &SpinlockCrossCpuTest, &locks, &counters, kNumIncrements)));
  int sum = 0;
  for (int i = 0; i < NumCPUs(); ++i) {
    sum += counters[i];
  }
  EXPECT_EQ(NumThreads() * kNumIncrements, sum);
}

TEST_P(PerCpuTest, CASConcurrent) {
  Handle heads = AllocHandle();
  const int kNumIters = 20000;
  std::atomic<int64_t> slice{0};
  int num_ptrs = kNumIters * NumThreads();
  // initialize the pointers with something invalid--all those 1s should get
  // overwritten.
  std::vector<int64_t> ptrs(num_ptrs, 1);
  RunInThreads(::util::functional::ToPermanentCallback(
      absl::bind_front(&CASConcurrentTest, heads, &ptrs, kNumIters, &slice)));
  int total = 0;

  // did we write to all of the pointers?
  for (int i = 0; i < NumCPUs(); ++i) {
    EXPECT_NE(1, ptrs[i]);
  }
  for (int i = 0; i < NumCPUs(); ++i) {
    int64_t head = GetPointerAtomic(heads, i)->load(std::memory_order_relaxed);
    while (head != 0) {
      ASSERT_NE(1, head);
      ASSERT_NE(2, head);
      total++;
      int64_t* ptr = reinterpret_cast<int64_t*>(head);
      head = *ptr;
      // Catch any pointer that's used twice.
      *ptr = 2;
    }
  }
  EXPECT_EQ(num_ptrs, total);
}

// Structure for building per CPU buffer in CASCheckConcurrent.  A
// buffer is a linked list of chunks.  Each CPU keeps a pointer to the
// head of the linked list.

struct BufferChunk {
  static constexpr size_t kChunkSize = 1024;
  explicit BufferChunk(BufferChunk* chunk) : next(chunk), size(0) {
    memset(data, 0, sizeof(data));
  }

  BufferChunk* next;         // next chunk in list.
  int64_t size;              // size of data.
  int64_t data[kChunkSize];  // data stored in this chunk.
};

// Implement a slightly subtle lockless (per-CPU) fixed array data structure.
// CASCheck is meant for ABA-like problems and this is a simple way to induce
// them (if CASCheck is broken.)
void CASCheckConcurrentTest(Handle chunks, int n, std::atomic<int64_t>* slice) {
  int end = slice->fetch_add(n, std::memory_order_relaxed) + n;
  int start = end - n;
  std::atomic<int64_t>* size_ptr;
  int64_t size;
  for (int i = start; i < end; ++i) {
    while (true) {
      const int cpu = GetCurrentCpu();

      // Fetch the current per CPU buffer chunk.
      std::atomic<int64_t>* const ptr =
          GetPointerAtomic(chunks, cpu);  // percpu slot.
      BufferChunk* const chunk =
          reinterpret_cast<BufferChunk*>(ptr->load(std::memory_order_relaxed));

      if (chunk != nullptr) {
        // size_ptr gives the index in this buffer of the first free slot
        size_ptr = reinterpret_cast<std::atomic<int64_t>*>(&(chunk->size));
        size = size_ptr->load(std::memory_order_relaxed);
      } else {
        // We have just started and this CPU has no chunk.  Pretend that
        // we had a full chunk to trigger chunk allocation.
        size = BufferChunk::kChunkSize;
      }

      if (size == BufferChunk::kChunkSize) {
        // Try inserting a new chunk in this CPU's buffer.
        BufferChunk* new_chunk = new BufferChunk(chunk);
        for (int64_t& p : new_chunk->data) p = -1;
        if (CompareAndSwap(cpu, ptr, reinterpret_cast<int64_t>(chunk),
                           reinterpret_cast<int64_t>(new_chunk)) != cpu) {
          delete new_chunk;
        }

        // A new block has just been inserted. It is simpler to just restart
        // the while-loop.
        continue;
      }

      // loc points to that first free slot
      std::atomic<int64_t>* const loc =
          reinterpret_cast<std::atomic<int64_t>*>(&chunk->data[size]);
      const int64_t old = loc->load(std::memory_order_relaxed);
      // If no one has inserted in the buffer,
      // put my new value in the next free spot.
      if (CompareAndSwapCheck(cpu, loc, old, i, size_ptr, size) != cpu) {
        continue;
      }

      // If my value is still there, mark that spot filled and we're done.
      // We need to be careful because size_ptr may not be valid.
      if (CompareAndSwapCheck(cpu, size_ptr, size, size + 1, loc, i) == cpu) {
        break;
      }
    }
  }
}

TEST_P(PerCpuTest, CASCheckConcurrent) {
  Handle chunks = AllocHandle();
  constexpr size_t kNumIters = 16384;
  std::atomic<int64_t> slice{0};

  const size_t buffer_size = kNumIters * NumThreads();
  RunInThreads(::util::functional::ToPermanentCallback(
      absl::bind_front(&CASCheckConcurrentTest, chunks, kNumIters, &slice)));

  int64_t value_count = 0;
  std::vector<bool> seen(buffer_size, false);
  for (int32_t i = 0; i < NumCPUs(); ++i) {
    const BufferChunk* chunk = reinterpret_cast<BufferChunk*>(
        GetPointerAtomic(chunks, i)->load(std::memory_order_relaxed));
    while (chunk != nullptr) {
      const int64_t size = chunk->size;
      for (int64_t j = 0; j < size; j++) {
        const int64_t val = chunk->data[j];
        ASSERT_LE(0, val);
        ASSERT_LT(val, buffer_size);
        // Check that every value shows up exactly once.
        ASSERT_FALSE(seen[val]);
        seen[val] = true;
      }
      value_count += size;
      const BufferChunk* old_chunk = chunk;
      chunk = chunk->next;
      delete old_chunk;
    }
  }
  ASSERT_EQ(buffer_size, value_count);
  FreeHandle(chunks);
}

struct node {
  node* next;
  uint64_t data;
};

int RandomCPU() {
  static absl::BitGen rng;
  return absl::Uniform<int32_t>(rng, 0, NumCPUs());
}

static void UpstreamFenceCpu(int cpu) {
  constexpr int kMEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ = (1 << 7);
  constexpr int kMEMBARRIER_CMD_FLAG_CPU = (1 << 0);

  int64_t res = syscall(__NR_membarrier, kMEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ,
                        kMEMBARRIER_CMD_FLAG_CPU, cpu);
  ABSL_RAW_CHECK(res == 0 || res == -ENXIO /* missing CPU */,
                 "Upstream fence failed.");
}

TEST_P(PerCpuTest, Fence) {
  auto test_func = [this](std::function<void(int)> FenceFunc) {
    std::atomic<int64_t> done{0};
    std::atomic<int64_t> ptr{0};

    Fiber f([&ptr, &done, FenceFunc]() {
      Handle a = AllocHandle();
      Handle b = AllocHandle();
      ptr.store(IntFromHandle(a), std::memory_order_release);
      int64_t expect_a = 0, expect_b = 0;
      int cpu_a = 0, cpu_b = 0;
      while (done.load(std::memory_order_acquire) == 0) {
        cpu_a = RandomCPU();
        EXPECT_EQ(expect_b,
                  GetPointerAtomic(b, cpu_b)->load(std::memory_order_relaxed));
        ptr.store(IntFromHandle(b), std::memory_order_relaxed);
        FenceFunc(cpu_a);
        expect_a = GetPointerAtomic(a, cpu_a)->load(std::memory_order_relaxed);
        cpu_b = RandomCPU();
        EXPECT_EQ(expect_a,
                  GetPointerAtomic(a, cpu_a)->load(std::memory_order_relaxed));
        ptr.store(IntFromHandle(a), std::memory_order_relaxed);
        FenceFunc(cpu_b);
        expect_b = GetPointerAtomic(b, cpu_b)->load(std::memory_order_relaxed);
      }
      FreeHandle(a);
      FreeHandle(b);
    });

    RunInThreads(ToPermanentCallback([&ptr]() {
      static const int kNumIters = 1000 * 1000;
      for (int i = 0; i < kNumIters; ++i) {
        while (true) {
          const int cpu = GetCurrentCpu();
          const int64_t pval = ptr.load(std::memory_order_acquire);
          // wait for a useful one
          if (pval == 0) continue;
          Handle h = HandleFromInt(pval);
          std::atomic<int64_t>* const loc = GetPointerAtomic(h, cpu);
          const int64_t old = loc->load(std::memory_order_relaxed);
          if (cpu == CompareAndSwapCheck(cpu, loc, old, old + 1, &ptr, pval)) {
            break;
          }
        }
      }
    }));

    done.store(1, std::memory_order_release);
    f.Join();
  };

  // Test the default fence.
  test_func(FenceCpu);

  if (::base::subtle::percpu::percpu_internal::disable_rseq()) {
    LOG(WARNING) << "RSEQ disabled: not testing upstream fence.";
    return;
  }

  // Test membarrier-based fence.
  constexpr int kMEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ = (1 << 8);
  // It is safe to make the syscall below multiple times.
  const bool kUpstreamFenceAvailable =
      0 == syscall(SYS_membarrier,
                   kMEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ, 0, 0);

  if (!kUpstreamFenceAvailable) {
    LOG(WARNING) << "Upstream fence not available.";
    return;
  }

  LOG(INFO) << "Testing upstream fence.";
  test_func(UpstreamFenceCpu);
}

TEST(PerCpuTest, RseqVirtualFlatCpuId) {
  if (IsFast()) {
    switch (base::subtle::percpu::GetRseqVcpuMode()) {
      case RseqVcpuMode::kFlat:
      case RseqVcpuMode::kFlatPerL3:
        EXPECT_EQ(base::subtle::percpu::__rseq_virtual_flat_cpu_id_offset,
                  offsetof(kernel_rseq, vcpu_id));
        EXPECT_EQ(base::subtle::percpu::RseqVirtualFlatCpuId(),
                  base::subtle::percpu::RseqVcpuId());
        break;
      case RseqVcpuMode::kMM:
        EXPECT_EQ(base::subtle::percpu::__rseq_virtual_flat_cpu_id_offset,
                  offsetof(kernel_rseq, mm_cid));
        EXPECT_GE(base::subtle::percpu::RseqVirtualFlatCpuId(), 0);
        EXPECT_LT(base::subtle::percpu::RseqVirtualFlatCpuId(), NumCPUs());
        break;
      case RseqVcpuMode::kNone:
        EXPECT_EQ(base::subtle::percpu::__rseq_virtual_flat_cpu_id_offset,
                  offsetof(kernel_rseq, cpu_id));
        EXPECT_EQ(base::subtle::percpu::RseqVirtualFlatCpuId(),
                  base::subtle::percpu::RseqCpuId());
        break;
    }
  } else {
    EXPECT_EQ(base::subtle::percpu::RseqCpuId(),
              base::subtle::percpu::kCpuIdUnsupported);
    EXPECT_EQ(base::subtle::percpu::RseqVirtualFlatCpuId(),
              base::subtle::percpu::kCpuIdUnsupported);
    EXPECT_EQ(base::subtle::percpu::__rseq_virtual_flat_cpu_id_offset,
              offsetof(kernel_rseq, cpu_id));
  }
}

TEST(PerCpuTest, UpstreamRseqFenceCpuAvailable) {
  if (!IsFast()) {
    // Initialize/register.  Does the same syscall as below.
    GTEST_SKIP()
        << "Skipping the test due to its dependency on RSEQ availability/usage";
  }

  constexpr int kMEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ = (1 << 8);
  // It is safe to make the syscall below multiple times.
  const bool available =
      0 == syscall(SYS_membarrier,
                   kMEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ, 0, 0);

  LOG(INFO) << "Upstream RSEQ Fence is "
            << (available ? "available." : "unavailable.");

  EXPECT_EQ(available,
            base::subtle::percpu::percpu_internal::UsingUpstreamRseqFenceCpu());
}

// this doesn't do any SMP testing, we're just checking the primitives work
// as advertised.

TEST(PerCpuSingleThreadedTest, SpinLock) {
  auto test = [](int this_cpu, int remote_cpu) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    PerCpuSpinLock lock;

    if (lock.Lock() != this_cpu) return false;

    if (remote_cpu != -1) lock.LockOn(remote_cpu);

    int cpu;
    if (lock.TryLock(&cpu)) return false;

    EXPECT_EQ(-1, cpu);
    lock.Unlock(this_cpu);

    if (!lock.TryLock(&cpu)) return false;
    EXPECT_EQ(this_cpu, cpu);
    lock.Unlock(this_cpu);

    if (remote_cpu != -1) lock.UnlockOn(remote_cpu);
    return true;
  };

  // Wraps the test above, but passing -1 to force no remote CPU.
  auto test_no_remote = [&test](int this_cpu) { return test(this_cpu, -1); };

  // Basic Lock/TryLock functionality.
  RunOnSingleCpu(test_no_remote);

  // Remote lock should not prevent local progress.
  RunOnSingleCpuWithRemoteCpu(test);
}

TEST(PerCpuSingleThreadedTest, CompareAndSwapWorks) {
  // wrong CPU, right value -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100};
    if (CompareAndSwap(remote_cpu, &local, 100, 200) != this_cpu) return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // wrong CPU, wrong value -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{500};
    if (CompareAndSwap(remote_cpu, &local, 100, 200) != this_cpu) return false;
    EXPECT_EQ(500, local.load(std::memory_order_relaxed));
    return true;
  });

  // right CPU, wrong value -- should get back fail (-1), no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int /*remote_cpu*/) {
    std::atomic<int64_t> local{500};
    if (CompareAndSwap(this_cpu, &local, 100, 200) != -1) return false;
    EXPECT_EQ(500, local.load(std::memory_order_relaxed));
    return true;
  });

  // right CPU, right value -- should get back same CPU, changed value
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int /*remote_cpu*/) {
    std::atomic<int64_t> local{100};
    if (CompareAndSwap(this_cpu, &local, 100, 200) != this_cpu) return false;
    EXPECT_EQ(200, local.load(std::memory_order_relaxed));
    return true;
  });
}

TEST(PerCpuSingleThreadedTest, CompareAndSwapCheckWorks) {
  // wrong CPU, right value, right check -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(remote_cpu, &local, 100, 200, &check, 456) !=
        this_cpu)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // wrong CPU, right value, wrong check -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(remote_cpu, &local, 100, 200, &check, 789) !=
        this_cpu)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // wrong CPU, wrong value, right check -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(remote_cpu, &local, 101, 200, &check, 456) !=
        this_cpu)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // wrong CPU, wrong value, wrong check -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(remote_cpu, &local, 101, 200, &check, 789) !=
        this_cpu)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // right CPU, right value, wrong check -- should get back fail (-1), no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(remote_cpu, &local, 100, 200, &check, 789) !=
        this_cpu)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // right CPU, wrong value, right check -- should get back fail, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int remote_cpu) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(remote_cpu, &local, 101, 200, &check, 456) !=
        this_cpu)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // right CPU, wrong value, wrong check -- should get back new CPU, no change
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int /*remote_cpu*/) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(this_cpu, &local, 101, 200, &check, 789) != -1)
      return false;
    EXPECT_EQ(100, local.load(std::memory_order_relaxed));
    return true;
  });

  // right CPU, right value, right check -- should get back same CPU,
  // changed value.
  RunOnSingleCpuWithRemoteCpu([](int this_cpu, int /*remote_cpu*/) {
    std::atomic<int64_t> local{100}, check{456};

    if (CompareAndSwapCheck(this_cpu, &local, 100, 200, &check, 456) !=
        this_cpu)
      return false;
    EXPECT_EQ(200, local.load(std::memory_order_relaxed));
    return true;
  });
}

TEST(PerCpuSingleThreadedTest, AllocHandle) {
  static const int kNumIters = 10;
  static const int kNumHandles = 10 * 1000;
  std::vector<Handle> handles;
  for (int i = 0; i < kNumIters; ++i) {
    for (int j = 0; j < kNumHandles; ++j) {
      Handle h = AllocHandle();
      for (int k = 0; k < NumCPUs(); ++k) {
        // write junk into it
        GetPointerAtomic(h, k)->store(i + j + k, std::memory_order_relaxed);
      }
      handles.push_back(h);
    }

    for (int j = 0; j < kNumHandles; ++j) {
      for (int k = 0; k < NumCPUs(); ++k) {
        EXPECT_EQ(
            GetPointerAtomic(handles[j], k)->load(std::memory_order_relaxed),
            i + j + k);
      }
    }
    for (auto h : handles) {
      FreeHandle(h);
    }
    handles.clear();
  }

  // It is always valid to free an empty handle.
  FreeHandle(NullHandle());
}

// Test that static locks do the right thing:
absl::NoDestructor<PerCpuSpinLock> static_lock;

TEST(PerCpuSingleThreadedTest, StaticLocks) {
  static_lock->Unlock(static_lock->Lock());
}

// Simple helper for generating a range of thread counts.
// generates [low, low*scale, low*scale^2, ...high)
// (even if high is between low*scale^k and low*scale^(k+1)).
std::vector<int> MultiplicativeRange(int low, int high, int scale) {
  std::vector<int> result;
  int current = low;
  while (current < high) {
    result.push_back(current);
    current *= scale;
  }
  result.push_back(high);
  return result;
}

INSTANTIATE_TEST_SUITE_P(MultiThread, PerCpuTest,
                         ::testing::ValuesIn(MultiplicativeRange(1, NumCPUs(),
                                                                 2)));

TEST(RestartableSequences, VirtualCpus) {
  if (!base::subtle::percpu::IsFast()) {
    return;  // RSEQ not enabled.
  }
  switch (GetRseqVcpuMode()) {
    case RseqVcpuMode::kNone:
      EXPECT_LT(base::subtle::percpu::RseqVcpuId(), 0);
      LOG(INFO) << "RSEQ VCPUs not enabled.";
      return;
    case RseqVcpuMode::kFlat:
      EXPECT_GE(base::subtle::percpu::RseqVcpuId(), 0);
      EXPECT_GE(base::subtle::percpu::RseqVirtualFlatCpuId(), 0);
      LOG(INFO) << "RSEQ VCPUs enabled (flat).";
      break;
    case RseqVcpuMode::kFlatPerL3:
      EXPECT_GE(base::subtle::percpu::RseqVcpuId(), 0);
      EXPECT_GE(base::subtle::percpu::RseqVirtualFlatCpuId(), 0);
      LOG(INFO) << "RSEQ VCPUs enabled (per-L3).";
      break;
    case RseqVcpuMode::kMM:
      EXPECT_LT(base::subtle::percpu::RseqVcpuId(), 0);
      EXPECT_GE(base::subtle::percpu::RseqVirtualFlatCpuId(), 0);
      break;
  }

  std::atomic<int> cpu_vcpu_always_equal_errors{0};
  auto functor = [&cpu_vcpu_always_equal_errors]() {
    CHECK(IsFast());
    // In forge, threads migrate CPUs quite often, and often cpu != vcpu below.
    // So we read vcpu before and after we read cpu in order to detect this.
    const int vcpu_prev = base::subtle::percpu::RseqVirtualFlatCpuId();
    const int cpu = GetCurrentCpu();
    const int vcpu = base::subtle::percpu::RseqVirtualFlatCpuId();
    EXPECT_LT(cpu, NumCPUs());
    EXPECT_LT(vcpu, NumCPUs());
    EXPECT_LE(0, vcpu);
    if (cpu != vcpu && vcpu_prev == vcpu) {
      cpu_vcpu_always_equal_errors.fetch_add(1, std::memory_order_relaxed);
    }
  };

  LOG(INFO) << "Testing threads";
  const int iterations = 30 * NumCPUs();
  std::vector<std::unique_ptr<ClosureThread>> threads;
  for (int idx = 0; idx < iterations; ++idx) {
    threads.emplace_back(std::make_unique<ClosureThread>(functor));
  }
  for (auto& t : threads) {
    t->SetJoinable(true);
    t->Start();
  }
  for (auto& t : threads) {
    t->Join();
  }

  LOG(INFO) << "Testing fibers";
  thread::Bundle bundle;
  for (int idx = 0; idx < iterations; ++idx) {
    bundle.Add(functor);
  }
  bundle.JoinAll();
}

// Test that vCPUs are "sticky".
TEST(RestartableSequences, VcpuPersistence) {
  if (!base::subtle::percpu::IsFast()) {
    GTEST_SKIP() << "Skipping the test: RSeq not enabled.";
  }
  if (base::subtle::percpu::RseqVcpuId() < 0) {
    GTEST_SKIP() << "Skipping the test: vCPUs not enabled.";
  }

  // Test that two threads doing ping-pong via futexes mostly keep their
  // VCPUs, and that the VCPUs are different.
  //
  // The primary complexity here is that if the two threads end up with the
  // same VCPU assigned (e.g. the first thread gets VCPU 0, gets de-scheduled
  // in the kernel and releases the VCPU, the second thread upon start-up will
  // get VCPU 0 as well), the ping-pong operation will result in the threads
  // never running at the same time, and thus they will always "share" the
  // same initial VCPU.
  //
  // In addition, forge is known to introduce delays, so even if the two
  // threads started with different VCPUs, after both are descheduled for
  // several milliseconds their VCPU persistence quantas will expire and
  // they will end up sharing the same VCPU (given they almost never
  // run concurrently during the ping-pong operation).
  //
  // The first complication above is dealt with by making sure the two threads
  // get different VCPUs before the ping-pong operation starts; the second
  // complication is difficult to properly address, but re-trying the test
  // a few times makes it pass consistently.

  for (int attempt = 0; attempt < 5; ++attempt) {
    constexpr int kIterations = 1000;
    constexpr int32_t kWait = 1;
    constexpr int32_t kWake = 2;

    std::atomic<int32_t> global_vcpu{
        base::subtle::percpu::RseqVirtualFlatCpuId()};
    std::atomic<int32_t> global_flips{0};

    auto run_futex_iters = [&](std::atomic<int32_t>* futex_this,
                               std::atomic<int32_t>* futex_that) {
      int32_t prev_vcpu = base::subtle::percpu::RseqVirtualFlatCpuId();
      int32_t local_flips = 0;

      for (int iter = 0; iter < kIterations; ++iter) {
        const int32_t next = base::subtle::percpu::RseqVirtualFlatCpuId();
        if (next != prev_vcpu) {
          ++local_flips;
          prev_vcpu = next;
        }

        if (next != global_vcpu.load()) {
          ++global_flips;
          global_vcpu.store(next);
        }

        while (futex_that->load() != kWait) {
        }
        futex_that->store(kWake);
        base::Futex::Wake(futex_that, 1 /* how many to wake */);
        futex_this->store(kWait);
        base::Futex::Wait(futex_this, kWait);
        CHECK_EQ(futex_this->load(), kWake);
      }

      LOG(INFO) << "local flips: " << local_flips;
      EXPECT_LT(local_flips, kIterations * 0.02);
    };

    std::atomic<int32_t> futex_main{kWake};
    std::atomic<int32_t> futex_child{kWait};

    // VCPUs assigned to the main and the child threads.
    std::atomic<int32_t> vcpu_main{
        base::subtle::percpu::RseqVirtualFlatCpuId()};
    std::atomic<int32_t> vcpu_child{vcpu_main.load()};

    ClosureThread child([&]() {
      EXPECT_TRUE(base::subtle::percpu::IsFast());

      // Make sure the two VCPUs are different.
      while (vcpu_main.load() == vcpu_child.load()) {
        vcpu_child.store(base::subtle::percpu::RseqVirtualFlatCpuId());
      }

      base::Futex::Wait(&futex_child, kWait);
      CHECK_EQ(futex_child.load(), kWake);
      run_futex_iters(&futex_child, &futex_main);
    });
    child.SetJoinable(true);
    child.Start();

    // Make sure the two VCPUs are different.
    while (vcpu_main.load() == vcpu_child.load()) {
      vcpu_main.store(base::subtle::percpu::RseqVirtualFlatCpuId());
    }

    run_futex_iters(&futex_main, &futex_child);

    // Final wakeup.
    futex_child.store(kWake);
    base::Futex::Wake(&futex_child, 1 /* how many to wake */);
    child.Join();

    LOG(INFO) << "Iterations: " << kIterations
              << "; global flips: " << global_flips.load();

    if (global_flips.load() > kIterations) {
      // Pass.
      return;
    }
  }
  GTEST_FAIL() << "All attempts failed.";
}

#if ABSL_INTERNAL_HAVE_ELF_SYMBOLIZE
static int FilterElfHeader(struct dl_phdr_info* info, size_t size, void* data) {
  *reinterpret_cast<uint64_t*>(data) =
      reinterpret_cast<uint64_t>(info->dlpi_addr);
  // No further iteration wanted.
  return 1;
}
#endif

TEST(RestartableSequences, CorruptedRseqStateDeathTest) {
  if (!IsFast()) {
    GTEST_SKIP() << "Require fast per-CPU";
  }

  EXPECT_DEATH(
      {
        // Corrupt rseq state, then try to reinitialize this thread.  We might
        // get preempted while this is happening, so we make several attempts.
        for (int i = 0; i < 10; ++i) {
          __rseq_abi.cpu_id = -1;
          CHECK(IsFast());
        }
      },
      "Thread registration failed with errno 16");
}

TEST(RestartableSequences, CriticalSectionMetadata) {
  // We expect that restartable sequence critical sections (rseq_cs) are in the
  // __rseq_cs section (by convention, not hard requirement).  Additionally, for
  // each entry in that section, there should be a pointer to it in
  // __rseq_cs_ptr_array.
#if ABSL_INTERNAL_HAVE_ELF_SYMBOLIZE
  uint64_t relocation = 0;
  dl_iterate_phdr(FilterElfHeader, &relocation);

  int fd = base::internal::signal_safe_open("/proc/self/exe", O_RDONLY);
  ASSERT_NE(fd, -1);

  const kernel_rseq_cs* cs_start = nullptr;
  const kernel_rseq_cs* cs_end = nullptr;

  const kernel_rseq_cs** cs_array_start = nullptr;
  const kernel_rseq_cs** cs_array_end = nullptr;

  ForEachSection(fd, [&](const absl::string_view name, const ElfW(Shdr) & hdr) {
    uint64_t start = relocation + reinterpret_cast<uint64_t>(hdr.sh_addr);
    uint64_t end =
        relocation + reinterpret_cast<uint64_t>(hdr.sh_addr + hdr.sh_size);

    if (name == "__rseq_cs") {
      CHECK_EQ(cs_start, nullptr);
      CHECK_EQ(start % alignof(*cs_start), 0);
      CHECK_EQ(end % alignof(*cs_end), 0);
      CHECK_LT(start, end) << "__rseq_cs must not be empty";

      cs_start = reinterpret_cast<const kernel_rseq_cs*>(start);
      cs_end = reinterpret_cast<const kernel_rseq_cs*>(end);
    } else if (name == "__rseq_cs_ptr_array") {
      CHECK_EQ(cs_array_start, nullptr);
      CHECK_EQ(start % alignof(*cs_array_start), 0);
      CHECK_EQ(end % alignof(*cs_array_end), 0);
      CHECK_LT(start, end) << "__rseq_cs_ptr_array must not be empty";

      cs_array_start = reinterpret_cast<const kernel_rseq_cs**>(start);
      cs_array_end = reinterpret_cast<const kernel_rseq_cs**>(end);
    }

    return true;
  });

  close(fd);

  // The length of the array in multiples of rseq_cs should be the same as the
  // length of the array of pointers.
  ASSERT_EQ(cs_end - cs_start, cs_array_end - cs_array_start);

  // The array should not be empty.
  ASSERT_NE(cs_start, nullptr);

  absl::flat_hash_set<const kernel_rseq_cs*> cs_pointers;
  for (auto* ptr = cs_start; ptr != cs_end; ++ptr) {
    cs_pointers.insert(ptr);
  }

  absl::flat_hash_set<const kernel_rseq_cs*> cs_array_pointers;
  for (auto** ptr = cs_array_start; ptr != cs_array_end; ++ptr) {
    // __rseq_cs_ptr_array should have no duplicates.
    EXPECT_TRUE(cs_array_pointers.insert(*ptr).second);
  }

  EXPECT_THAT(cs_pointers, ::testing::ContainerEq(cs_array_pointers));
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  const bool fast_desired =
      !base::subtle::percpu::percpu_internal::disable_rseq();
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);

  if (fast_desired) {
    if (!base::subtle::percpu::IsFast()) {
      LOG(WARNING) << "No fast per-cpu support here; test coverage is limited. "
                   << "PERCPU_USE_RSEQ=" << PERCPU_USE_RSEQ;
    }
  } else {
    // env variable should have forced !IsFast, if we asked
    QCHECK(!base::subtle::percpu::IsFast());
  }

  // For many tests to be meaningful we need SMP.
  CHECK_GT(NumCPUs(), 1) << "Test requires SMP environment.";
  // Runs benchmarks if there are any and --benchmark_filter is provided.
  // Note: this cannot use benchmark::RunSpecifiedBenchmarksThenExit() because
  // it may be link with the external library.
  // FIXME(vyng): Fix this once we've moved the wrapper out of the internal
  // header.
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  return RUN_ALL_TESTS();
}
