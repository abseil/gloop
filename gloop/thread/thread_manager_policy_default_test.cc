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

// A test for the default policy for the ThreadManager

#include <climits>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "gloop/thread/thread_manager_policy.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

// -------------------------------------------------------
// An event simulator

struct Sim;
struct Env;

struct SimEvent {
  absl::AnyInvocable<void(Sim*)> func;
};
using EventQueue =
    std::multimap<int64_t, SimEvent>;  // maps time to an event
                                       // run func(sim, arg) at time
struct Sim {
  int64_t time;
  EventQueue event_queue;
  std::unique_ptr<Env> env;
};

constexpr int64_t kForEver = std::numeric_limits<int64_t>::max();

// Run events from the simulator's event queue until it is empty or time >=
// end.
void Simulate(Sim* sim, int64_t end) {
  VLOG(1) << sim->time << " Simulate";
  while (!sim->event_queue.empty()) {
    auto it = sim->event_queue.begin();
    if (it->first >= end) {
      break;
    }
    int64_t t = it->first;
    SimEvent ev = std::move(it->second);
    sim->event_queue.erase(it);
    ASSERT_LE(sim->time, t);
    sim->time = t;
    ev.func(sim);
  }
  VLOG(1) << sim->time << " EndSimulate";
}

// Add an event (func) to the simulator's event queue at "time".
void SimAddEvent(Sim* sim, int64_t time, absl::AnyInvocable<void(Sim*)> func) {
  VLOG(7) << sim->time << " SimAddEvent " << time;
  sim->event_queue.emplace(time, SimEvent{std::move(func)});
}

int64_t SimTime(Sim* sim) { return sim->time; }

// -------------------------------------------------------
// The environment to be simulated

struct EnvCPU;
void EnvDoneCPU(Sim* sim, EnvCPU* cpu);
void EnvAddThread(Sim* sim);

// Simulated work has a cpu_time component, and a sleep_time component
struct EnvWork {
  int cpu_time;    // time to be CPU bound
  int sleep_time;  // time to sleep
  EnvCPU* cpu;     // when CPU-bound, which CPU; 0 otherwise
};

using EnvWorkQueue = std::deque<EnvWork*>;
using EnvWorkMap = absl::flat_hash_map<EnvWork*, std::unique_ptr<EnvWork>>;

struct EnvCPU {
  int id;         // CPU id
  EnvWork* work;  // which work is being run; 0 if idle
};

struct EnvWorkGenerator;

struct Env {
  // We simulate an array of CPUs, a pool of threads, a work queue that needs
  // CPU and a thread, and a set of unfinished work (which may be sleeping).
  std::vector<std::unique_ptr<EnvCPU>> cpu_array;
  std::vector<EnvCPU*> idle_cpu;  // idle cpus

  int idle_threads;     // count of threads idle
  int blocked_threads;  // count of threads blocked

  EnvWorkQueue work_queue;  // work to be done by a thread
  EnvWorkMap work_set;      // outstanding work

  // We'll call the policy module the first time after
  // next_policy_time that there's work, a CPU, but no thread.
  std::unique_ptr<thread::ThreadManagerPolicy> policy;
  int64_t next_policy_time;

  int64_t closures_run;

  std::unique_ptr<EnvWorkGenerator> work_generator;
};

// Query the policy module and maybe add a thread to the system.
void EnvMaybeAddThread(Sim* sim) {
  VLOG(6) << sim->time << " EnvMaybeAddThread"
          << "  work " << sim->env->work_queue.size() << "  idle_cpus "
          << sim->env->idle_cpu.size() << "  idle_threads "
          << sim->env->idle_threads;
  if (!sim->env->work_queue.empty() && !sim->env->idle_cpu.empty() &&
      sim->env->idle_threads == 0) {  // can't proceed;  no thread
    int64_t time = SimTime(sim);
    if (sim->env->next_policy_time <= time) {
      thread::ThreadManagerState state;
      thread::ThreadManagerAction action;
      state.time_ms = time;
      state.pool_index = 0;
      state.pool_count = 1;
      state.closures_run = sim->env->closures_run;
      state.queue_length = sim->env->work_queue.size();
      state.threads = sim->env->idle_threads + sim->env->cpu_array.size() -
                      sim->env->idle_cpu.size();
      state.blocked = sim->env->blocked_threads;
      state.threads_since_last_exit = state.threads;
      sim->env->policy->Eval(state, &action);
      sim->env->next_policy_time = time + action.delay_ms;
      if (action.create) {
        EnvAddThread(sim);
      } else if (action.desired_threads > state.threads) {
        // Add threads if the policy requests a specific target thread count
        // that is higher than the current thread count.
        const int threads_to_add = action.desired_threads - state.threads;
        for (int i = 0; i < threads_to_add; ++i) {
          EnvAddThread(sim);
        }
      }
    }
  }
}

// The CPU's idle loop
void EnvIdleLoop(Sim* sim, EnvCPU* cpu) {
  if (!sim->env->work_queue.empty() && sim->env->idle_threads != 0) {
    // work to do
    EnvWork* work = sim->env->work_queue.front();  // schedule it
    VLOG(6) << sim->time << " EnvIdleLoop " << cpu->id << " work " << work;
    sim->env->work_queue.pop_front();
    cpu->work = work;
    work->cpu = cpu;
    sim->env->idle_threads--;
    SimAddEvent(sim, SimTime(sim) + work->cpu_time,
                [cpu](Sim* sim) { EnvDoneCPU(sim, cpu); });
  } else {  // put CPU to sleep
    VLOG(6) << sim->time << " EnvIdleLoop " << cpu->id << " no_work ";
    sim->env->idle_cpu.push_back(cpu);
    EnvMaybeAddThread(sim);
  }
}

// Wake an idle CPU if possible so it will do some work on a thread.
void EnvWakeIdleCPU(Sim* sim) {
  VLOG(6) << sim->time << " EnvWakeIdleCPU"
          << "  work " << sim->env->work_queue.size() << "  idle_cpus "
          << sim->env->idle_cpu.size() << "  idle_threads "
          << sim->env->idle_threads;
  EnvMaybeAddThread(sim);
  if (!sim->env->work_queue.empty() && !sim->env->idle_cpu.empty() &&
      sim->env->idle_threads != 0) {
    EnvCPU* cpu = sim->env->idle_cpu.back();
    sim->env->idle_cpu.pop_back();
    SimAddEvent(sim, sim->time, [cpu](Sim* sim) { EnvIdleLoop(sim, cpu); });
  }
}

// Finished some work.
void EnvDoneSleep(Sim* sim, EnvWork* work) {
  VLOG(6) << sim->time << " EnvDoneSleep work " << work;
  sim->env->work_set.erase(work);
  sim->env->blocked_threads--;
  sim->env->idle_threads++;
  sim->env->closures_run++;
  EnvWakeIdleCPU(sim);
}

// CPU-bound phase of some work finished.
void EnvDoneCPU(Sim* sim, EnvCPU* cpu) {
  EnvWork* work = cpu->work;
  VLOG(6) << sim->time << " EnvDoneCPU " << cpu->id << "  work " << work;
  CHECK_EQ(work->cpu, cpu);
  cpu->work = nullptr;
  work->cpu = nullptr;
  sim->env->blocked_threads++;
  int64_t time = SimTime(sim);
  SimAddEvent(sim, time + work->sleep_time,
              [work](Sim* sim) { EnvDoneSleep(sim, work); });  // work sleeps
  SimAddEvent(sim, time,
              [cpu](Sim* sim) { EnvIdleLoop(sim, cpu); });  // CPU idles
}

// -------------------------------------------------------
// Code to introduce things to the simulated environment.

// Add some work
void EnvAddWork(Sim* sim, int cpu_time, int sleep_time) {
  auto work = std::make_unique<EnvWork>();
  EnvWork* work_ptr = work.get();
  VLOG(1) << sim->time << " EnvAddWork work " << work_ptr << " " << cpu_time
          << " " << sleep_time;
  work->cpu_time = cpu_time;
  work->sleep_time = sleep_time;
  work->cpu = nullptr;
  sim->env->work_set[work_ptr] = std::move(work);  // outstanding work
  sim->env->work_queue.push_back(work_ptr);        // and on the work queue
  EnvWakeIdleCPU(sim);
}

// A work generator
struct EnvWorkGenerator {
  int left_to_create;  // amount of work left to create
  int period;          // time between creating work items
  int cpu_time;        // time to be CPU bound
  int sleep_time;      // time to sleep
};

void EnvGenerateWork(Sim* sim) {
  EnvWorkGenerator* g = sim->env->work_generator.get();
  VLOG(6) << sim->time << " EnvGenerateWork";
  EnvAddWork(sim, g->cpu_time, g->sleep_time);
  g->left_to_create--;
  if (g->left_to_create > 0) {
    SimAddEvent(sim, SimTime(sim) + g->period,
                [](Sim* sim) { EnvGenerateWork(sim); });
  }
}

// Add a thread
void EnvAddThread(Sim* sim) {
  VLOG(1) << sim->time << " EnvAddThread";
  sim->env->idle_threads++;
  EnvWakeIdleCPU(sim);
}

// -------------------------------------------------------
// Return a simulator with n_cpus CPUs, all idle; no threads;
// and n_work Work items with a period of work_period, and
// work parameters (cpu_time, sleep_time).
struct EnvSetupParam {
  std::unique_ptr<thread::ThreadManagerPolicy> policy;
  int n_cpus;
  int n_work;
  int work_period;
  int cpu_time;
  int sleep_time;
};
std::unique_ptr<Sim> EnvSetup(EnvSetupParam param) {
  VLOG(1) << 0 << " EnvSetup";
  auto sim = std::make_unique<Sim>();
  sim->time = 0;
  sim->env = std::make_unique<Env>();
  sim->env->idle_threads = 0;
  sim->env->blocked_threads = 0;
  sim->env->policy = std::move(param.policy);
  sim->env->next_policy_time = 0;
  sim->env->closures_run = 0;
  for (int i = 0; i < param.n_cpus; i++) {
    auto cpu = std::make_unique<EnvCPU>();
    cpu->id = i;
    cpu->work = nullptr;
    sim->env->idle_cpu.push_back(cpu.get());
    sim->env->cpu_array.push_back(std::move(cpu));
  }
  sim->env->work_generator = std::make_unique<EnvWorkGenerator>();
  sim->env->work_generator->left_to_create = param.n_work;
  sim->env->work_generator->period = param.work_period;
  sim->env->work_generator->cpu_time = param.cpu_time;
  sim->env->work_generator->sleep_time = param.sleep_time;
  SimAddEvent(sim.get(), 0, [](Sim* sim) { EnvGenerateWork(sim); });
  return sim;
}

// -------------------------------------------------------

thread_local int g_n_cpus = 0;
int GetNCPU() { return g_n_cpus; }

struct WorkloadParams {
  int n_work;
  int cpu_time;
  int sleep_time;
  std::string type;
};

class ThreadManagerPolicyDefaultTest
    : public ::testing::TestWithParam<std::tuple<bool, int, WorkloadParams>> {};

TEST_P(ThreadManagerPolicyDefaultTest, RunSimulation) {
  const auto& [eager, cpus, workload] = GetParam();
  EnvSetupParam param;
  g_n_cpus = cpus;
  if (eager) {
    param.policy = std::unique_ptr<thread::ThreadManagerPolicy>(
        thread::EagerThreadManagerPolicy(INT_MAX));
  } else {
    param.policy = std::unique_ptr<thread::ThreadManagerPolicy>(
        thread::DefaultThreadManagerPolicy(&GetNCPU));
  }
  param.n_cpus = GetNCPU();
  param.n_work = workload.n_work;
  param.work_period = 0;
  param.cpu_time = workload.cpu_time;
  param.sleep_time = workload.sleep_time;

  int n_cpus_val = param.n_cpus;
  int n_work_val = param.n_work;
  int cpu_time_val = param.cpu_time;
  int sleep_time_val = param.sleep_time;

  std::unique_ptr<Sim> sim = EnvSetup(std::move(param));
  Simulate(sim.get(), kForEver);

  VLOG(1) << absl::StrFormat("%4d cpus %5d closures %5d cputime %5d sleep",
                             n_cpus_val, n_work_val, cpu_time_val,
                             sleep_time_val)
          << absl::StrFormat(" end %5d with %5d threads", sim->time,
                             sim->env->idle_threads);

  EXPECT_EQ(sim->env->closures_run, n_work_val);
  EXPECT_THAT(sim->env->work_queue, IsEmpty());
  EXPECT_THAT(sim->env->work_set, IsEmpty());
  EXPECT_EQ(sim->env->blocked_threads, 0);
  EXPECT_THAT(sim->env->idle_cpu, SizeIs(n_cpus_val));
}

std::string GetTestName(
    const ::testing::TestParamInfo<std::tuple<bool, int, WorkloadParams>>&
        info) {
  const auto& [eager, cpus, workload] = info.param;
  return absl::StrFormat("%s_%s_Cpus%d", eager ? "Eager" : "Default",
                         workload.type, cpus);
}

INSTANTIATE_TEST_SUITE_P(
    ThreadManagerPolicyDefaultTestInstantiation, ThreadManagerPolicyDefaultTest,
    ::testing::Combine(
        ::testing::Bool(),
        ::testing::Values(1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024),
        ::testing::Values(WorkloadParams{/*n_work=*/10000, /*cpu_time=*/0,
                                         /*sleep_time=*/1000, "Sleeping"},
                          WorkloadParams{/*n_work=*/10000, /*cpu_time=*/1000,
                                         /*sleep_time=*/0, "CPUbound"})),
    GetTestName);

}  // namespace
