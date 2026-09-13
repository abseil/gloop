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

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_SHARED_BIT_GEN_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_SHARED_BIT_GEN_H_

#include <type_traits>

#include "absl/base/no_destructor.h"
#include "absl/random/random.h"
#include "gloop/concurrent/percpu/object.h"

namespace util_random {

// A thread-safe URBG that is implemented using global shared bit generators.
// This should be used in any cases where you would otherwise use a
// mutex-protected absl::BitGen or a transient absl::BitGen.
template <typename BitGen>
class SharedBitGenT {
  static_assert(
      std::is_same_v<BitGen, absl::BitGen> ||
          std::is_same_v<BitGen, absl::InsecureBitGen>,
      "SharedBitGenT only supports absl::BitGen or absl::InsecureBitGen");

 public:
  SharedBitGenT() = default;
  // SharedBitGenT is move-only.
  SharedBitGenT(SharedBitGenT&&) noexcept = default;
  SharedBitGenT& operator=(SharedBitGenT&&) noexcept = default;
  SharedBitGenT(const SharedBitGenT&) = delete;
  SharedBitGenT& operator=(const SharedBitGenT&) = delete;

  using result_type = typename BitGen::result_type;

  static constexpr result_type(min)() { return (BitGen::min)(); }
  static constexpr result_type(max)() { return (BitGen::max)(); }

  result_type operator()() { return (*PerCpu().get())(); }

 private:
  static concurrent::percpu::PerCpu<BitGen>& PerCpu() {
    static absl::NoDestructor<concurrent::percpu::PerCpu<BitGen>> per_cpu;
    return *per_cpu;
  }
};

using SharedBitGen = SharedBitGenT<absl::BitGen>;

}  // namespace util_random

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_SHARED_BIT_GEN_H_
