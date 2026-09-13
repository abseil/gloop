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

#include "gloop/thread/fiber/pthread-domain.h"

#include <tuple>

#include "gloop/base/scheduling/domain-test.h"
#include "gtest/gtest.h"

namespace base {
namespace scheduling {

INSTANTIATE_TEST_SUITE_P(
    PthreadDomainTest, DomainTest,
    ::testing::Values(std::make_tuple(thread::NewPthreadDomain, true)));

}  // namespace scheduling
}  // namespace base
