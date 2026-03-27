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

#include "gloop/util/refcount/compact_reference_counted.h"

#include <cstdint>

#include "benchmark/benchmark.h"
#include "gloop/util/refcount/test_common.h"
#include "gtest/gtest.h"

namespace refcount {
namespace {

struct TypeFactory {
  template <typename T>
  using ReferenceCountedBaseT = CompactReferenceCounted<T>;
};
INSTANTIATE_TYPED_TEST_SUITE_P(Compact, ReferenceCountedTest, TypeFactory);

// An empty compact refcounted object holds only a refcount.
class BasicCompactRefCounted final
    : public CompactReferenceCounted<BasicCompactRefCounted, int32_t> {};
static_assert(sizeof(BasicCompactRefCounted) == sizeof(int32_t));

BENCHMARK(BenchmarkCopy<BasicCompactRefCounted>);
BENCHMARK(BenchmarkCtorDtor<BasicCompactRefCounted>);

}  // namespace
}  // namespace refcount
