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

#include <stdio.h>

#include <climits>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "gloop/base/init_google.h"
#include "gloop/thread/thread_manager_policy.h"

// -------------------------------------------------------
// An event simulator

struct Sim;
struct Env;
static void EnvDelete(Env* env);

struct SimEvent {
  void (*func)(Sim* sim, void* arg);
  void* arg;
};
typedef std::multimap<int64_t, SimEvent>
    EventQueue;  // maps time to an event
                 // run func(sim, arg) at time
struct Sim {
  int64_t time;
  EventQueue event_queue;
  Env* env;
};

static const int64_t kForEver = (~static_cast<uint64_t>(0)) >> 1;

// Run events from the simulator's event queue until it is empty or time >=
// end.
void Simulate(Sim* sim, int64_t end) {
  VLOG(1) << sim->time << " Simulate";
  EventQueue::iterator it;
  while (!sim->event_queue.empty() &&
         (it = sim->event_queue.begin())->first < end) {
    int64_t t = it->first;
    SimEvent ev = it->second;
    sim->event_queue.erase(it);
    CHECK_LE(sim->time, t);
    sim->time = t;
    (*ev.func)(sim, ev.arg);
  }
  VLOG(1) << sim->time << " EndSimulate";
}

// Add an event (func, arg) to the simulator's event queue at "time".
static void SimAddEvent(Sim* sim, int64_t time, void (*func)(Sim* sim, void*),
                        void* arg) {
  VLOG(7) << sim->time << " SimAddEvent " << time;
  SimEvent ev;
  ev.func = func;
  ev.arg = arg;
  sim->event_queue.insert(std::pair<int64_t, SimEvent>(time, ev));
}

static int64_t SimTime(Sim* sim) { return sim->time; }

// Discard *sim.
static void SimDelete(Sim* sim) {
  VLOG(1) << sim->time << " SimDelete";
  EnvDelete(sim->env);
  delete sim;
}

// -------------------------------------------------------
// The environment to be simulated

struct EnvCPU;
static void EnvDoneCPU(Sim* sim, void* v);
static void EnvAddThread(Sim* sim);

// Simulated work has a cpu_time component, and a sleep_time component
struct EnvWork {
  int cpu_time;    // time to be CPU bound
  int sleep_time;  // time to sleep
  EnvCPU* cpu;     // when CPU-bound, which CPU; 0 otherwise
};

typedef std::deque<EnvWork*> EnvWorkQueue;
typedef absl::flat_hash_set<EnvWork*> EnvWorkSet;

struct EnvCPU {
  int id;         // CPU id
  EnvWork* work;  // which work is being run; 0 if idle
};

struct Env {
  // We simulate an array of CPUs, a pool of threads, a work queue that needs
  // CPU and a thread, and a set of unfinished work (which may be sleeping).
  std::vector<EnvCPU*> cpu_array;
  std::vector<EnvCPU*> idle_cpu;  // idle cpus

  int idle_threads;     // count of threads idle
  int blocked_threads;  // count of threads blocked

  EnvWorkQueue work_queue;  // work to be done by a thread
  EnvWorkSet work_set;      // outstanding work

  // We'll call the policy module the first time after
  // next_policy_time that there's work, a CPU, but no thread.
  thread::ThreadManagerPolicy* policy;
  int64_t next_policy_time;

  int64_t closures_run;
};

// Discard *env.
static void EnvDelete(Env* env) {
  for (int i = 0; i != env->cpu_array.size(); i++) {
    delete env->cpu_array[i];
  }
  while (!env->work_set.empty()) {
    EnvWork* work = *env->work_set.begin();
    env->work_set.erase(env->work_set.begin());
    delete work;
  }
  delete env->policy;
  delete env;
}

// Query the policy module and maybe add a thread to the system.
static void EnvMaybeAddThread(Sim* sim) {
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
      }
    }
  }
}

// The CPU's idle loop
static void EnvIdleLoop(Sim* sim, void* v) {
  EnvCPU* cpu = static_cast<EnvCPU*>(v);
  if (!sim->env->work_queue.empty() && sim->env->idle_threads != 0) {
    // work to do
    EnvWork* work = sim->env->work_queue.front();  // schedule it
    VLOG(6) << sim->time << " EnvIdleLoop " << cpu->id << " work " << work;
    sim->env->work_queue.pop_front();
    cpu->work = work;
    work->cpu = cpu;
    sim->env->idle_threads--;
    SimAddEvent(sim, SimTime(sim) + work->cpu_time, &EnvDoneCPU, v);
  } else {  // put CPU to sleep
    VLOG(6) << sim->time << " EnvIdleLoop " << cpu->id << " no_work ";
    sim->env->idle_cpu.push_back(cpu);
    EnvMaybeAddThread(sim);
  }
}

// Wake an idle CPU if possible so it will do some work on a thread.
static void EnvWakeIdleCPU(Sim* sim) {
  VLOG(6) << sim->time << " EnvWakeIdleCPU"
          << "  work " << sim->env->work_queue.size() << "  idle_cpus "
          << sim->env->idle_cpu.size() << "  idle_threads "
          << sim->env->idle_threads;
  EnvMaybeAddThread(sim);
  if (!sim->env->work_queue.empty() && !sim->env->idle_cpu.empty() &&
      sim->env->idle_threads != 0) {
    EnvCPU* cpu = sim->env->idle_cpu.back();
    sim->env->idle_cpu.pop_back();
    SimAddEvent(sim, sim->time, &EnvIdleLoop, cpu);
  }
}

// Finished some work.
static void EnvDoneSleep(Sim* sim, void* v) {
  EnvWork* work = static_cast<EnvWork*>(v);
  VLOG(6) << sim->time << " EnvDoneSleep work " << work;
  sim->env->work_set.erase(work);
  delete work;
  sim->env->blocked_threads--;
  sim->env->idle_threads++;
  sim->env->closures_run++;
  EnvWakeIdleCPU(sim);
}

// CPU-bound phase of some work finished.
static void EnvDoneCPU(Sim* sim, void* v) {
  EnvCPU* cpu = static_cast<EnvCPU*>(v);
  EnvWork* work = cpu->work;
  VLOG(6) << sim->time << " EnvDoneCPU " << cpu->id << "  work " << work;
  CHECK(work->cpu == cpu);
  cpu->work = nullptr;
  work->cpu = nullptr;
  sim->env->blocked_threads++;
  int64_t time = SimTime(sim);
  SimAddEvent(sim, time + work->sleep_time, &EnvDoneSleep,
              work);                        // work sleeps
  SimAddEvent(sim, time, &EnvIdleLoop, v);  // CPU idles
}

// -------------------------------------------------------
// Code to introduce things to the simulated environment.

// Add some work
static void EnvAddWork(Sim* sim, int cpu_time, int sleep_time) {
  EnvWork* work = new EnvWork;
  VLOG(1) << sim->time << " EnvAddWork work " << work << " " << cpu_time << " "
          << sleep_time;
  work->cpu_time = cpu_time;
  work->sleep_time = sleep_time;
  work->cpu = nullptr;
  sim->env->work_set.insert(work);       // outstanding work
  sim->env->work_queue.push_back(work);  // and on the work queue
  EnvWakeIdleCPU(sim);
}

// A work generator
struct EnvWorkGenerator {
  int left_to_create;  // amount of work left to create
  int period;          // time between creating work items
  int cpu_time;        // time to be CPU bound
  int sleep_time;      // time to sleep
};
static void EnvGenerateWork(Sim* sim, void* v) {
  EnvWorkGenerator* g = static_cast<EnvWorkGenerator*>(v);
  VLOG(6) << sim->time << " EnvGenerateWork";
  EnvAddWork(sim, g->cpu_time, g->sleep_time);
  g->left_to_create--;
  if (g->left_to_create > 0) {
    SimAddEvent(sim, SimTime(sim) + g->period, &EnvGenerateWork, g);
  } else {
    delete g;
  }
}

// Add a thread
static void EnvAddThread(Sim* sim) {
  VLOG(1) << sim->time << " EnvAddThread";
  sim->env->idle_threads++;
  EnvWakeIdleCPU(sim);
}

// -------------------------------------------------------
// Return a simulator with n_cpus CPUs, all idle; no threads;
// and n_work Work items with a period of work_period, and
// work parameters (cpu_time, sleep_time).
struct EnvSetupParam {
  thread::ThreadManagerPolicy* policy;
  int n_cpus;
  int n_work;
  int work_period;
  int cpu_time;
  int sleep_time;
};
static Sim* EnvSetup(const EnvSetupParam* param) {
  VLOG(1) << 0 << " EnvSetup";
  Sim* sim = new Sim;
  sim->time = 0;
  sim->env = new Env;
  sim->env->idle_threads = 0;
  sim->env->blocked_threads = 0;
  sim->env->policy = param->policy;
  sim->env->next_policy_time = 0;
  sim->env->closures_run = 0;
  for (int i = 0; i != param->n_cpus; i++) {
    EnvCPU* cpu = new EnvCPU;
    cpu->id = i;
    cpu->work = nullptr;
    sim->env->cpu_array.push_back(cpu);
    sim->env->idle_cpu.push_back(cpu);
  }
  EnvWorkGenerator* g = new EnvWorkGenerator;
  g->left_to_create = param->n_work;
  g->period = param->work_period;
  g->cpu_time = param->cpu_time;
  g->sleep_time = param->sleep_time;
  SimAddEvent(sim, 0, &EnvGenerateWork, g);
  return sim;
}

// Return a string describing the setup parameters for a test
static std::string EnvParamPrint(const EnvSetupParam* param) {
  return absl::StrFormat("%4d cpus %5d closures %5d cputime %5d sleep",
                         param->n_cpus, param->n_work, param->cpu_time,
                         param->sleep_time);
}

// -------------------------------------------------------

static int n_cpus;
static int NCPU() { return n_cpus; }

static void TestClosures(bool eager, int cpus, int n_work, int cpu_time,
                         int sleep_time) {
  EnvSetupParam param;
  n_cpus = cpus;
  if (eager) {
    param.policy = thread::EagerThreadManagerPolicy(INT_MAX);
  } else {
    param.policy = thread::DefaultThreadManagerPolicy(&NCPU);
  }
  param.n_cpus = NCPU();
  param.n_work = n_work;
  param.work_period = 0;
  param.cpu_time = cpu_time;
  param.sleep_time = sleep_time;
  Sim* sim = EnvSetup(&param);
  Simulate(sim, kForEver);
  LOG(INFO) << EnvParamPrint(&param)
            << absl::StrFormat(" end %5d with %5d threads", sim->time,
                               sim->env->idle_threads);
  SimDelete(sim);
}

int main(int argc, char* argv[]) {
  InitGoogle(argv[0], &argc, &argv, true);

  for (int eager_int = 0; eager_int != 2; eager_int++) {
    bool eager = (eager_int != 0);
    LOG(INFO) << (eager ? "eager" : "default") << " policy";
    LOG(INFO) << "=== Sleeping";
    for (int cpus = 1; cpus <= 1024; cpus <<= 1) {
      TestClosures(eager, cpus, 10000, 0, 1000);
    }
    LOG(INFO) << "=== CPUbound";
    for (int cpus = 1; cpus <= 1024; cpus <<= 1) {
      TestClosures(eager, cpus, 10000, 1000, 0);
    }
  }

  printf("PASS\n");
  return 0;
}
