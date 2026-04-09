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

//
// Unittest from Waldemar to test ThreadPool creation/destruction

#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  {
    ThreadPool pool_1(1,
                      ThreadPool::Options{.thread_options = thread::Options(),
                                          .queue_capacity = 16});
    LOG(INFO) << "Started test threads";
  }

  {
    ThreadPool pool_10(10,
                       ThreadPool::Options{.thread_options = thread::Options(),
                                           .queue_capacity = 16});
    LOG(INFO) << "Started test threads";
  }

  {
    // Start a pool with a queue_capacity < num_threads
    // and verify that it can be destructed without starting threads.
    ThreadPool pool_destructor(
        10, ThreadPool::Options{.thread_options = thread::Options(),
                                .queue_capacity = 1});
  }

  LOG(INFO) << "PASS";
  return 0;
}
