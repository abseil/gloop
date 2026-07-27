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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/internal/low_level_alloc.h"
#include "absl/cleanup/cleanup.h"
#include "absl/debugging/internal/symbolize.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gtest/gtest.h"

using util::SymbolMap;

ABSL_ATTRIBUTE_NOINLINE static void DummyFunction() {}

namespace {

class TestJitSymbolDecorator final
    : public absl::debugging_internal::SymbolDecorator {
 public:
  static absl::debugging_internal::SymbolDecoratorPtr Factory(int /*fd*/) {
    void* ptr = absl::base_internal::LowLevelAlloc::AllocWithArena(
        sizeof(TestJitSymbolDecorator), absl::base_internal::SigSafeArena());
    return absl::debugging_internal::SymbolDecoratorPtr(
        new (ptr) TestJitSymbolDecorator());
  }

  void Decorate(const void* pc, ptrdiff_t /*relocation*/, char* symbol_buf,
                size_t symbol_buf_size, char* /*tmp_buf*/,
                size_t /*tmp_buf_size*/) const override {
    if (pc == kTestJitAddr) {
      snprintf(symbol_buf, symbol_buf_size, "%s", kTestJitName);
    }
  }

  static const void* kTestJitAddr;
  static const char* kTestJitName;
};

const void* TestJitSymbolDecorator::kTestJitAddr = nullptr;
const char* TestJitSymbolDecorator::kTestJitName = "fake_jit_function";

TEST(SymbolMapFallbackTest, GetSymbolInfoAtPositionFallbackToAbslSymbolize) {
  const void* test_addr = reinterpret_cast<const void*>(&DummyFunction);
  TestJitSymbolDecorator::kTestJitAddr = test_addr;

  absl::Cleanup cleanup_decorator =
      [old_decorator = absl::debugging_internal::SetSymbolDecoratorFactory(
           &TestJitSymbolDecorator::Factory)] {
        absl::debugging_internal::SetSymbolDecoratorFactory(old_decorator);
      };

  std::unique_ptr<SymbolMap> symbol_map(SymbolMap::GetEmpty(0));

  const char* name = nullptr;
  uint64_t start = 0, size = 0;

  ASSERT_TRUE(symbol_map->GetSymbolInfoAtPosition(
      reinterpret_cast<uint64_t>(test_addr), &name, &start, &size));

  EXPECT_STREQ("fake_jit_function", name);
  EXPECT_EQ(reinterpret_cast<uint64_t>(test_addr), start);
  EXPECT_EQ(0, size);
}

}  // namespace
