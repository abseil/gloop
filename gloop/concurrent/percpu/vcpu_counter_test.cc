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

#include "gloop/concurrent/percpu/vcpu_counter.h"

#include <sched.h>
#include <sys/resource.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/base/no_destructor.h"
#include "benchmark/benchmark.h"
#include "gloop/base/percpu.h"
#include "gloop/base/sysinfo.h"
#include "gloop/concurrent/percpu/counter.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"
#include "tcmalloc/malloc_hook.h"

using concurrent::percpu::VcpuCounter;

namespace {

std::vector<int> AllowedCpus() {
  std::vector<int> cpus;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
    const int num_cpus = NumCPUs();
    for (int cpu = 0; cpu < num_cpus && cpu < CPU_SETSIZE; ++cpu) {
      if (CPU_ISSET(cpu, &mask)) {
        cpus.push_back(cpu);
      }
    }
  }
  return cpus;
}

class ScopedCpuPin {
 public:
  explicit ScopedCpuPin(int cpu) : cpu_(cpu) {
    CPU_ZERO(&saved_);
    if (sched_getaffinity(0, sizeof(saved_), &saved_) != 0) {
      return;
    }
    has_saved_ = true;
    cpu_set_t target;
    CPU_ZERO(&target);
    CPU_SET(cpu, &target);
    pinned_ = (sched_setaffinity(0, sizeof(target), &target) == 0);
  }

  ~ScopedCpuPin() {
    if (has_saved_) {
      sched_setaffinity(0, sizeof(saved_), &saved_);
    }
  }

  bool OnTargetCpu() const {
    if (!pinned_) return false;
    cpu_set_t current;
    CPU_ZERO(&current);
    if (sched_getaffinity(0, sizeof(current), &current) != 0) return false;
    return CPU_COUNT(&current) == 1 && CPU_ISSET(cpu_, &current) &&
           base::subtle::percpu::GetCurrentCpu() == cpu_;
  }

 private:
  int cpu_;
  cpu_set_t saved_;
  bool has_saved_ = false;
  bool pinned_ = false;
};

TEST(VcpuCounterTest, SimpleUse) {
  VcpuCounter counter;
  EXPECT_EQ(0, counter.value());

  counter.Add(12);
  EXPECT_EQ(12, counter.value());

  counter.Add(-112);
  EXPECT_EQ(-100, counter.value());

  int64_t per_cpu_sum = 0;
  for (int cpu = 0; cpu < NumCPUs(); ++cpu) {
    per_cpu_sum += counter.subtle_value_for_cpu(cpu);
  }
  EXPECT_EQ(-100, per_cpu_sum);

  counter.Clear();

  EXPECT_EQ(0, counter.value());
}

TEST(VcpuCounterTest, CountsAccurately) {
  VcpuCounter counter;
  {
    ThreadPool pool(20);

    for (int i = 0; i < pool.num_threads(); ++i) {
      pool.Schedule([&counter]() {
        for (int i = 0; i < 5000; ++i) {
          counter.Add(1);
        }
      });
    }
  }
  EXPECT_EQ(100000, counter.value());
}

TEST(VcpuCounterTest, Move) {
  static const size_t kNumCtrs = 128;
  std::vector<VcpuCounter> ctrs;
  for (int i = 0; i < kNumCtrs; ++i) {
    ctrs.push_back(VcpuCounter());
    ctrs.back() = VcpuCounter();
    for (auto& c : ctrs) {
      c.Add(1);
    }
  }

  for (int i = 0; i < kNumCtrs; ++i) {
    EXPECT_EQ(kNumCtrs - i, ctrs[i].value());
  }
}

TEST(VcpuCounterTest, MoveSemanticsAndSelfMove) {
  VcpuCounter src;
  src.Add(42);
  EXPECT_EQ(src.value(), 42);

  // Move construction transfers non-zero count.
  VcpuCounter dst(std::move(src));
  EXPECT_EQ(dst.value(), 42);
  dst.Add(8);
  EXPECT_EQ(dst.value(), 50);

  // Move assignment into a counter with an existing non-zero handle frees the
  // old handle and adopts the source's count.
  VcpuCounter src2;
  src2.Add(99);
  dst = std::move(src2);
  EXPECT_EQ(dst.value(), 99);

  // Self-move assignment must preserve the handle and value rather than freeing
  // the handle and leaving a null handle behind.
  VcpuCounter& dst_alias = dst;
  dst = std::move(dst_alias);
  EXPECT_EQ(dst.value(), 99);
  dst.Add(1);
  EXPECT_EQ(dst.value(), 100);

  // A moved-from counter can be reinitialized by move-assigning a fresh
  // counter into it.
  src = VcpuCounter();
  EXPECT_EQ(src.value(), 0);
  src.Add(7);
  EXPECT_EQ(src.value(), 7);
}

TEST(VcpuCounterTest, SlotIndexingMatchesActivePerCpuMode) {
#if !PERCPU_USE_RSEQ
  GTEST_SKIP() << "Emulated VcpuCounter uses a single slot 0.";
#else
  const int num_cpus = NumCPUs();
  VcpuCounter vcpu_counter;
  concurrent::percpu::Counter phys_counter;
  std::vector<int64_t> expected_vcpu_slots(num_cpus, 0);
  std::vector<int64_t> expected_phys_slots(num_cpus, 0);

  const std::vector<int> allowed = AllowedCpus();
  int distinct_vcpu_from_phys_observations = 0;
  for (int cpu : allowed) {
    ASSERT_GE(cpu, 0);
    ASSERT_LT(cpu, num_cpus);
    for (int attempt = 0; attempt < 20; ++attempt) {
      ScopedCpuPin pin(cpu);
      if (!pin.OnTargetCpu()) break;

      struct rusage ru_before = {};
      struct rusage ru_after = {};
      ASSERT_EQ(getrusage(RUSAGE_THREAD, &ru_before), 0);
      const int vcpu_before = base::subtle::percpu::GetCurrentVirtualFlatCpu();
      vcpu_counter.Add(1);
      phys_counter.Add(1);
      const int vcpu_after = base::subtle::percpu::GetCurrentVirtualFlatCpu();
      ASSERT_EQ(getrusage(RUSAGE_THREAD, &ru_after), 0);

      if (pin.OnTargetCpu() && ru_before.ru_nvcsw == ru_after.ru_nvcsw &&
          ru_before.ru_nivcsw == ru_after.ru_nivcsw &&
          vcpu_before == vcpu_after && vcpu_before >= 0) {
        ASSERT_LT(vcpu_before, num_cpus);
        expected_vcpu_slots[vcpu_before] += 1;
        expected_phys_slots[cpu] += 1;
        EXPECT_EQ(vcpu_counter.subtle_value_for_cpu(vcpu_before),
                  expected_vcpu_slots[vcpu_before]);
        EXPECT_EQ(phys_counter.subtle_value_for_cpu(cpu),
                  expected_phys_slots[cpu]);
        if (!base::subtle::percpu::UsingRseqVirtualCpus()) {
          // When vCPUs are disabled in //gloop/base:percpu
          // (PERCPU_VCPU_MODE=none or RSEQ disabled), VcpuCounter must index
          // by physical CPU ID.
          EXPECT_EQ(vcpu_before, cpu);
        } else if (vcpu_before != cpu) {
          // When vCPUs are active and vcpu != physical cpu, the physical cpu's
          // slot in vcpu_counter must remain untouched by this increment.
          EXPECT_EQ(vcpu_counter.subtle_value_for_cpu(cpu),
                    expected_vcpu_slots[cpu]);
          ++distinct_vcpu_from_phys_observations;
        }
        break;
      }

      // Affinity was tampered with or the thread context-switched mid-probe;
      // resync expected slot baselines from the counters and retry on this CPU.
      for (int i = 0; i < num_cpus; ++i) {
        expected_vcpu_slots[i] = vcpu_counter.subtle_value_for_cpu(i);
        expected_phys_slots[i] = phys_counter.subtle_value_for_cpu(i);
      }
    }
  }

  EXPECT_EQ(vcpu_counter.value(), phys_counter.value());
  for (int i = 0; i < num_cpus; ++i) {
    EXPECT_EQ(vcpu_counter.subtle_value_for_cpu(i), expected_vcpu_slots[i])
        << "slot=" << i;
    EXPECT_EQ(phys_counter.subtle_value_for_cpu(i), expected_phys_slots[i])
        << "cpu=" << i;
  }

  if (!base::subtle::percpu::UsingRseqVirtualCpus()) {
    for (int i = 0; i < num_cpus; ++i) {
      EXPECT_EQ(vcpu_counter.subtle_value_for_cpu(i),
                phys_counter.subtle_value_for_cpu(i))
          << "cpu=" << i;
    }
  } else if (allowed.size() > 1) {
    EXPECT_GT(distinct_vcpu_from_phys_observations, 0);
  }
#endif
}

TEST(VcpuCounterTest, UnregisteredThreadWithStaleVcpuFields) {
  VcpuCounter counter;
  const auto add_from_unregistered_state = [&counter]() {
    ASSERT_FALSE(base::subtle::percpu::IsFastNoInit());
#if PERCPU_USE_RSEQ
    // Simulate stale 0 values in vcpu_id / mm_cid on an unregistered thread
    // (where only cpu_id == kCpuIdUninitialized indicates unregistration).
    base::subtle::percpu::RseqAbi()->mm_cid = 0;
    base::subtle::percpu::RseqAbi()->vcpu_id = 0;
#endif
    counter.Add(19);
    if (base::subtle::percpu::IsFast()) {
      EXPECT_TRUE(base::subtle::percpu::IsFastNoInit());
    }
  };

  std::thread(add_from_unregistered_state).join();
  EXPECT_EQ(counter.value(), 19);

  {
    base::subtle::percpu::ScopedUnregisterRseq unregister;
    add_from_unregistered_state();
  }
  EXPECT_EQ(counter.value(), 38);
}

TEST(VcpuCounterTest, ConcurrentAddAndValueReads) {
  VcpuCounter counter;
  std::atomic<bool> writers_done{false};
  constexpr int kNumWriters = 8;
  constexpr int kItersPerWriter = 10000;

  std::vector<std::thread> writers;
  writers.reserve(kNumWriters);
  for (int t = 0; t < kNumWriters; ++t) {
    writers.emplace_back([&counter, t]() {
      const int64_t sign = (t % 2 == 0) ? 1 : -1;
      for (int i = 0; i < kItersPerWriter; ++i) {
        counter.Add(sign * 3);
      }
    });
  }

  std::thread reader([&]() {
    const int cpus = NumCPUs();
    while (!writers_done.load(std::memory_order_acquire)) {
      benchmark::DoNotOptimize(counter.value());
      for (int cpu = 0; cpu < cpus; ++cpu) {
        benchmark::DoNotOptimize(counter.subtle_value_for_cpu(cpu));
      }
    }
  });

  for (auto& w : writers) {
    w.join();
  }
  writers_done.store(true, std::memory_order_release);
  reader.join();

  EXPECT_EQ(counter.value(), 0);
  counter.Clear();
  for (int cpu = 0; cpu < NumCPUs(); ++cpu) {
    EXPECT_EQ(counter.subtle_value_for_cpu(cpu), 0);
  }
}

absl::once_flag malloc_count_init;
VcpuCounter* malloc_count = nullptr;
void MallocNewHook(const tcmalloc::MallocHook::NewInfo&) {
  malloc_count->Add(1);
}

TEST(VcpuCounterTest, WorksFromMallocHook) {
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
  GTEST_SKIP() << "Sanitizers do not invoke tcmalloc::MallocHook.";
#endif
  absl::call_once(malloc_count_init,
                  []() { malloc_count = new VcpuCounter(); });
  malloc_count->Clear();
  ASSERT_TRUE(tcmalloc::MallocHook::AddNewHook(MallocNewHook));
  {
    ThreadPool pool(20);

    for (int i = 0; i < pool.num_threads(); ++i) {
      pool.Schedule([]() {
        std::vector<std::unique_ptr<int>> all;  // Prevents loop optimizations.
        for (int i = 0; i < 5000; ++i) {
          all.push_back(std::make_unique<int>(i));
        }
      });
    }
  }
  EXPECT_TRUE(tcmalloc::MallocHook::RemoveNewHook(MallocNewHook));
  EXPECT_GE(malloc_count->value(), 100000);
}

absl::NoDestructor<VcpuCounter> c;

void BM_VcpuCounter_Add(benchmark::State& state) {
  int i = 0;
  for (auto s : state) {
    c->Add(i++);
    benchmark::DoNotOptimize(c);
  }
}

void BM_VcpuCounter_Value(benchmark::State& state) {
  for (auto s : state) {
    benchmark::DoNotOptimize(c->value());
  }
}
BENCHMARK(BM_VcpuCounter_Add)->ThreadRange(1, NumCPUs());
BENCHMARK(BM_VcpuCounter_Value);

void BM_VcpuCounterCreate(benchmark::State& state) {
  for (auto s : state) {
    VcpuCounter c;
    benchmark::DoNotOptimize(c);
  }
}
BENCHMARK(BM_VcpuCounterCreate);

}  // anonymous namespace
