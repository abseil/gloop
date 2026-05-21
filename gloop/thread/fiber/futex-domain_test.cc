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

#include "gloop/thread/fiber/futex-domain.h"

#include <tuple>

#include "absl/log/log.h"
#include "gloop/base/init_google.h"
#include "gloop/base/scheduling/domain-test.h"
#include "gtest/gtest.h"

namespace base {
namespace scheduling {

INSTANTIATE_TEST_SUITE_P(
    FutexDomainTest, DomainTest,
    ::testing::Values(std::make_tuple(thread::NewFutexDomain, true)));

}  // namespace scheduling
}  // namespace base

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);

  if (!thread::FutexDomainAvailable()) {
    // Skip test since attempting to construct a FutexDomain would
    // CHECK-fail.
#if !PORTABLE_BASE
    LOG(ERROR) << "FutexDomain unavailable";
#else
    ABSL_RAW_LOG(ERROR, "FutexDomain unavailable.");
#endif
    return 0;
  }

  return RUN_ALL_TESTS();
}
