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

#include <atomic>

#include "gloop/base/callback-types.h"
#include "gloop/base/context.h"

namespace base {
namespace internal {

CallbackBase::~CallbackBase() {
  delete context_.load(std::memory_order_acquire);
}

void CallbackBase::UnusedKeyMethod() {}  // <link>

::base::Context* CallbackBase::SlowContextPtr() { return nullptr; }

::base::Context* CallbackBase::MaterializeDefaultContext() {
  auto* created = new ::base::Context();
  ::base::Context* expected = nullptr;
  if (!context_.compare_exchange_strong(expected, created,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
    delete created;
    return expected;
  }
  return created;
}

}  // namespace internal
}  // namespace base
