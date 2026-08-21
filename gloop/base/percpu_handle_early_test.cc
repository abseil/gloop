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

#include <optional>

#include "absl/base/attributes.h"
#include "gloop/base/percpu.h"
#include "gtest/gtest.h"

namespace base::subtle::percpu {
namespace {

// TODO: Enable this.
#ifndef ABSL_HAVE_MEMORY_SANITIZER
ABSL_CONST_INIT std::optional<Handle> handle;

TEST(PerCpu, HasHandle) {
  ASSERT_TRUE(handle.has_value());
  if (handle.has_value()) {
    EXPECT_TRUE(NullHandle().rep != handle->rep);
  }
}

void get_handle() { handle = AllocHandle(); }

__attribute__((section(".preinit_array"),
               used)) void (*__local_preinit)() = get_handle;
#endif

}  // namespace
}  // namespace base::subtle::percpu
