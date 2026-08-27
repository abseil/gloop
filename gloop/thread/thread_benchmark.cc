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

#include "benchmark/benchmark.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/threadpool.h"

namespace thread {
namespace {

void BM_StackTrace(benchmark::State& state) {
  // Create a thread pool to increase the number of threads to collect
  // stack traces from.
  ThreadPool pool(128);

  Thread_ProcessStackTracesArg arg;
  arg.process_trace_arg = nullptr;
  arg.process_trace =
      +[](void* /*writer*/, const LiveThread* thread,
          const StackTrace* /*trace*/) { benchmark::DoNotOptimize(thread); };

  for (auto s : state) {
    Thread_ProcessStackTraces(arg);
  }
}

BENCHMARK(BM_StackTrace)->ThreadRange(1, 8);

}  // namespace
}  // namespace thread
