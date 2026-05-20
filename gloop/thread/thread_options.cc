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

#include "gloop/thread/thread_options.h"

#include <cstdint>

#include "absl/flags/flag.h"

ABSL_FLAG(int32_t, default_thread_stack_size, 0,
          "Set the size of the stack for any thread that "
          "doesn't explicitly specify a stack size using SetStackSize()");

ABSL_FLAG(int32_t, default_thread_stack_guard, 0,
          "Bytes to use for the stack guard.  If you suspect stack "
          "overflow, you may catch the problem earlier by setting a large "
          "value, such as 1048576.  Defaults to zero, which uses 1MByte "
          "in 64-bit binaries and 16kBytes in 32-bit binaries.");

namespace thread {

Options::Options()
    : stack_size_(absl::GetFlag(FLAGS_default_thread_stack_size)),
      guard_size_(absl::GetFlag(FLAGS_default_thread_stack_guard)),
      scheduling_policy_(thread::SCHEDPOLICY_NORMAL),
      sched_priority_(-1),
      nice_priority_level_(0),
      io_priority_level_(-1),
      io_class_(-1),
      joinable_(false) {}

Options::~Options() {}

}  // namespace thread
