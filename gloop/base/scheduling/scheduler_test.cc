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

#include "gloop/base/scheduling/scheduler.h"

#include <stdint.h>

#include "gtest/gtest.h"

namespace base {
namespace scheduling {
namespace {

TEST(Shedulable, ManagerShort) {
  Schedulable s(nullptr, Schedulable::kWorkItem);

  s.set_manager_short(1234);
  EXPECT_EQ(1234, s.manager_short());
}

TEST(Schedulable, AttachFiber) {
  Schedulable s(nullptr, Schedulable::kWorkItem);
  int foo = 1;

  EXPECT_FALSE(IsFiberAttached(&s));

  InternalAttachFiber(&s, &foo);
  EXPECT_TRUE(IsFiberAttached(&s));
  EXPECT_EQ(&foo, reinterpret_cast<int*>(s.managed_arg()));

  s.set_managed_arg(reinterpret_cast<intptr_t>(&foo));
  EXPECT_FALSE(IsFiberAttached(&s));
  EXPECT_EQ(&foo, reinterpret_cast<int*>(s.managed_arg()));
}

}  // namespace
}  // namespace scheduling
}  // namespace base
