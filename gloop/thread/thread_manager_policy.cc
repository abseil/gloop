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

// Implementation of behavoirs common to all ThreadManagerPolicy, which
// provides certain defaults for ThreadManagerPolicy implementations.
// One default is the TestResult() interface that provides a way for
// a ThreadManagerPolicy to construct conditions under which it determines
// that a policy is performing correctly for a test.

#include "gloop/thread/thread_manager_policy.h"

namespace thread {

// Constructors for default options.
// Base policy constructor/destructor.

ThreadManagerPolicy::ThreadManagerPolicy() {}

ThreadManagerPolicy::~ThreadManagerPolicy() {}

}  // namespace thread
