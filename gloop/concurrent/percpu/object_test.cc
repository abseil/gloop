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

#include "gloop/concurrent/percpu/object.h"

#include <sched.h>

#include <cstdint>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "benchmark/benchmark.h"
#include "gloop/base/sysinfo.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using concurrent::percpu::PerCpu;

class PerCpuTest : public testing::Test {
 protected:
#if HAVE_SCHED_SETAFFINITY
  // Arbitrarily picks a CPU and sets the affinity for the calling thread to
  // that
  // CPU. Returns the selected CPU.
  int PinToOneCpu() {
    while (true) {
      for (int cpu = 0; cpu < NumCPUs(); ++cpu) {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(cpu, &cpus);
        if (sched_setaffinity(0, sizeof(cpus), &cpus) == 0) {
          return cpu;
        }
      }
      LOG(INFO) << "All sched_setaffinity calls failed; retrying.";
    }
  }

  // Like PinToOneCpu(), except this will not select avoid_cpu.
  int PinToDifferentCpu(int avoid_cpu) {
    while (true) {
      for (int cpu = 0; cpu < NumCPUs(); ++cpu) {
        if (cpu == avoid_cpu) continue;
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(cpu, &cpus);
        if (sched_setaffinity(0, sizeof(cpus), &cpus) == 0) {
          return cpu;
        }
      }
      LOG(INFO) << "All sched_setaffinity calls failed; retrying.";
    }
  }

  ~PerCpuTest() {
    // these tests might mess with CPU affinity, make sure we're not
    // pinned somewhere we don't much want to be.
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu = 0; cpu < NumCPUs(); ++cpu) {
      CPU_SET(cpu, &set);
    }
    CHECK_EQ(0, sched_setaffinity(0, sizeof(cpu_set_t), &set));
  }
#endif
};

TEST_F(PerCpuTest, SetSomethingAndIterate) {
  PerCpu<int> pc;
  *pc.get() = 3;
  std::map<int, int> observations;
  for (int n : pc) {
    ++observations[n];
  }
  // We should see zero (for default-initialization) NumCPUs - 1 times, since we
  // never touch the other CPUs' values, and 3 once.
  EXPECT_THAT(observations,
              testing::UnorderedElementsAre(testing::Pair(0, NumCPUs() - 1),
                                            testing::Pair(3, 1)));
}

TEST_F(PerCpuTest, DifferentCpus) {
  PerCpu<int> pc;
  *pc.remote_get(0) = 1;
  *pc.remote_get(1) = 2;
  EXPECT_THAT(std::accumulate(pc.begin(), pc.end(), 0), testing::Eq(3));
}

TEST_F(PerCpuTest, NoOps) {
  PerCpu<int> pc;
  {
    PerCpu<int>::iterator it;
  }
  {
    PerCpu<int>::iterator it;
    it = pc.begin();
  }
  {
    PerCpu<int>::pointer p;
  }
  {
    PerCpu<int>::pointer p;
    p = pc.get();
  }
}

#if HAVE_SCHED_SETAFFINITY
TEST_F(PerCpuTest, MoveOperators) {
  PerCpu<int> pc;
  {
    const int first_cpu = PinToOneCpu();
    PerCpu<int>::pointer p1 = pc.get();
    PinToDifferentCpu(first_cpu);
    PerCpu<int>::pointer p2 = pc.get();
    p1 = std::move(p2);
  }
  {
    PerCpu<int>::iterator it1 = pc.begin();
    ++it1;
    PerCpu<int>::iterator it2 = pc.begin();
    it1 = std::move(it2);
  }
}
#endif

TEST_F(PerCpuTest, SelfMove) {
  PerCpu<int> pc;
  {
    PerCpu<int>::iterator it = pc.begin();
    auto& it2 = it;
    it = std::move(it2);
  }
  {
    PerCpu<int>::pointer p = pc.get();
    auto& p2 = p;
    p = std::move(p2);
  }
}

TEST_F(PerCpuTest, StlAlgorithms) {
  PerCpu<int> pc;
  *pc.get() = 3;
  EXPECT_THAT(std::accumulate(pc.begin(), pc.end(), 0), testing::Eq(3));
  *pc.get() += 2;
  EXPECT_THAT(std::accumulate(pc.begin(), pc.end(), 0), testing::Eq(5));
  const auto& const_pc = pc;
  EXPECT_THAT(std::accumulate(const_pc.begin(), const_pc.end(), 0),
              testing::Eq(5));
}

TEST_F(PerCpuTest, Initialization) {
  PerCpu<int> pc(1);
  *pc.get() = 3;
  std::map<int, int> observations;
  for (int n : pc) {
    ++observations[n];
  }
  // We should see one (for the initialization we did in the constructor)
  // NumCPUs - 1 times, since we never touch the other CPUs' values, and 3 once.
  EXPECT_THAT(observations,
              testing::UnorderedElementsAre(testing::Pair(1, NumCPUs() - 1),
                                            testing::Pair(3, 1)));
}

TEST_F(PerCpuTest, DestructorsMustBeCalled) {
  static int outstanding_objects = 0;

  struct TrackableObject {
    TrackableObject() { ++outstanding_objects; }
    ~TrackableObject() { --outstanding_objects; }
  };

  {
    PerCpu<TrackableObject> pc;
    EXPECT_EQ(outstanding_objects, NumCPUs());
  }
  EXPECT_EQ(outstanding_objects, 0);
}

TEST_F(PerCpuTest, DifferentCacheLines) {
  PerCpu<int> pc(1);
  std::set<std::uint64_t> pointers;
  for (int& n : pc) {
    pointers.insert(reinterpret_cast<std::uint64_t>(&n));
  }
  // Now, make sure that the difference from one pointer to the next is at least
  // as large as a cacheline.
  std::uint64_t last = *pointers.begin();
  for (auto it = std::next(pointers.begin()); it != pointers.end(); ++it) {
    EXPECT_GE(*it - last, ABSL_CACHELINE_SIZE);
    last = *it;
  }
}

struct alignas(2 * ABSL_CACHELINE_SIZE) LargeObject {
  int a;
};

TEST_F(PerCpuTest, LargeObject) {
  PerCpu<LargeObject> pc;
  std::set<std::uint64_t> pointers;
  for (auto& n : pc) {
    pointers.insert(reinterpret_cast<std::uint64_t>(&n));
  }
  // Now, make sure that the difference from one pointer to the next is at least
  // as large as a cacheline.
  std::uint64_t last = *pointers.begin();
  for (auto it = std::next(pointers.begin()); it != pointers.end(); ++it) {
    uint64_t v = *it;
    EXPECT_GE(v - last, ABSL_CACHELINE_SIZE);
    EXPECT_EQ(v % alignof(LargeObject), 0);
    last = v;
  }
}

#if !GUNIT_NO_GOOGLE3

void BM_get(benchmark::State& state) {
  static PerCpu<int>* const ints = new PerCpu<int>();

  for (auto s : state) {
    benchmark::DoNotOptimize(*ints->get());
  }
}
BENCHMARK(BM_get)->ThreadRange(1, NumCPUs());

void BM_iterate(benchmark::State& state) {
  PerCpu<int> ints;

  for (auto s : state) {
    for (int n : ints) {
      benchmark::DoNotOptimize(n);
    }
  }
}
BENCHMARK(BM_iterate);

void BM_increment(benchmark::State& state) {
  static PerCpu<int>* const ints = new PerCpu<int>();

  for (auto s : state) {
    benchmark::DoNotOptimize((*ints->get())++);
  }
}
BENCHMARK(BM_increment)->ThreadRange(1, NumCPUs() * 2);

#endif  // !GUNIT_NO_GOOGLE3

}  // anonymous namespace
