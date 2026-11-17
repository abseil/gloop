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

#include "gloop/util/coding/elias_fano/nthbit.h"

#include <cstdint>

#include "absl/base/internal/cpu_detect.h"

namespace elias_fano {
namespace nthbit_internal {

#if defined(__x86_64__)
// NOLINTBEGIN(abseil-no-internal-dependencies)
const bool kUseFastPath = []() {
  // If there is no pdep instruction, use the fallback.
  if (!absl::base_internal::SupportsBmi2()) return false;

  // AMD Zen1 and Zen2 have very slow pdep, so use the fallback implementation.
  const absl::base_internal::CpuType cpu_type =
      absl::base_internal::GetCpuType();
  switch (cpu_type) {
    case absl::base_internal::CpuType::kAmdNaples:
    case absl::base_internal::CpuType::kAmdRome:
      return false;
    default:
      return true;
  }
}();
// NOLINTEND(abseil-no-internal-dependencies)
#endif

uint64_t NthbitSlow(uint64_t x, uint64_t n) { return NthbitNoPdep(x, n); }

}  // namespace nthbit_internal
}  // namespace elias_fano
