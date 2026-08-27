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

#include <thread>  // NOLINT(build/c++11)

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "gloop/base/scheduling/downcalls.h"

extern "C" {

// Fiber-aware override of AbslInternalMutexYield().
//
// Gives a hint to the scheduler that the current thread should
// relinquish the CPU.  If the current thread is a cooperative thread, the hint
// goes to the cooperative scheduler.  Otherwise the hint goes to the kernel
// thread scheduler.
//
// TODO ABSL_ATTRIBUTE_UNUSED is to suppress dead code deletion.
ABSL_ATTRIBUTE_UNUSED
void ABSL_INTERNAL_C_SYMBOL(AbslInternalMutexYield)() {
  if (base::scheduling::Downcalls::CurrentThreadIsCooperative()) {
    base::scheduling::Downcalls::Reschedule();
  } else {
    std::this_thread::yield();
  }
}

}  // extern "C"
