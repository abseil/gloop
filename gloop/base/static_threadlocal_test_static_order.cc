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

#include <memory>

#include "gloop/base/static_threadlocal.h"

#if GTEST_GOOGLE3_MODE_
#include "benchmark/benchmark.h"
#endif

// See comments in static_threadlocal_test.cc
STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(std::unique_ptr<int>,
                                          static_thread_local, (new int(1)));

int PeekAtRemoteStaticThreadLocal2() {
#if GTEST_GOOGLE3_MODE_
  benchmark::DoNotOptimize(static_thread_local.safe_pointer());
  benchmark::DoNotOptimize(static_thread_local.pointer());
#endif

  return *static_thread_local.get();
}

extern int PeekAtRemoteStaticThreadLocal1();
static int unordered_global_constructors = PeekAtRemoteStaticThreadLocal1();
