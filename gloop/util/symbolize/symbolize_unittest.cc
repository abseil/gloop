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

#include "gloop/util/symbolize/symbolize.h"

#include <elf.h>
#include <sys/auxv.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/container/node_hash_set.h"
#include "absl/debugging/internal/vdso_support.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "gloop/util/symbolize/demangle.h"
#include "gloop/util/symbolize/symbolize-inl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tcmalloc/malloc_extension.h"

using util::SymbolMap;

ABSL_ATTRIBUTE_NOINLINE static void Foo() {}
ABSL_ATTRIBUTE_NOINLINE static void Bar() {}

namespace {

using ::testing::Ge;
using ::testing::Lt;

// This test must be first because the symbol cache must be empty on its start.
TEST(SymbolMap, IndicatesIfCacheWasBuilt) {
  EXPECT_FALSE(SymbolMap::IsCacheInitialized());
  SymbolMap::GetCached();
  EXPECT_TRUE(SymbolMap::IsCacheInitialized());
}

class SymbolMapTest : public testing::TestWithParam<int32_t> {
 public:
  SymbolMapTest() {
    absl::SetFlag(&FLAGS_symbol_map_compression_level, GetParam());
  }
};

TEST_P(SymbolMapTest, GetSymbolInfoAtPosition) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  symbol_map->AddSymbol("bar", 0x80, 0x00);  // no size information.
  symbol_map->AddSymbol("baz", 0xe0, 0x20);
  symbol_map->AddSymbol("quux", 0x120, 0x20);  // gap after baz ends
  symbol_map->AddSymbol("bazz.part.14", 0x200, 0x20);

  const char* name = nullptr;
  uint64_t start = 0, size = 0;
  ASSERT_FALSE(symbol_map->GetSymbolInfoAtPosition(0x00, &name, &start, &size));

  ASSERT_TRUE(symbol_map->GetSymbolInfoAtPosition(0x48, &name, &start, &size));
  ASSERT_STREQ("foo", name);
  ASSERT_EQ(0x40, start);
  ASSERT_EQ(0x40, size);

  ASSERT_TRUE(symbol_map->GetSymbolInfoAtPosition(0x90, &name, &start, &size));
  ASSERT_STREQ("bar", name);
  ASSERT_EQ(0x80, start);
  ASSERT_EQ(0x00, size);

  ASSERT_TRUE(symbol_map->GetSymbolInfoAtPosition(0xff, &name, &start, &size));
  ASSERT_STREQ("baz", name);
  ASSERT_EQ(0xe0, start);
  ASSERT_EQ(0x20, size);

  // Verify "bazz.part.14" is truncated to "bazz".
  ASSERT_TRUE(symbol_map->GetSymbolInfoAtPosition(0x202, &name, &start, &size));
  ASSERT_STREQ("bazz", name);
  ASSERT_EQ(0x200, start);
  ASSERT_EQ(0x20, size);

  ASSERT_FALSE(
      symbol_map->GetSymbolInfoAtPosition(0x100, &name, &start, &size));
  EXPECT_EQ(symbol_map->num_symbols(), 5);
}

TEST_P(SymbolMapTest, GetSymbolAtPosition) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  // Tests for boundary conditions to make sure we don't
  // have off-by-one errors.
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  symbol_map->AddSymbol("bar", 0x80, 0x60);
  symbol_map->AddSymbol("baz", 0xe0, 0x20);
  symbol_map->AddSymbol("quux", 0x120, 0x20);  // gap after baz ends
  symbol_map->AddSymbol("aaa", 0x150, 0x00);   // no size information.
  symbol_map->AddSymbol("bbb", 0x160, 0x00);   // no size information.
  ASSERT_TRUE(nullptr == symbol_map->GetSymbolAtPosition(0x00));
  ASSERT_TRUE(nullptr == symbol_map->GetSymbolAtPosition(0x39));
  ASSERT_STREQ("foo", symbol_map->GetSymbolAtPosition(0x40));
  ASSERT_STREQ("foo", symbol_map->GetSymbolAtPosition(0x60));
  ASSERT_STREQ("foo", symbol_map->GetSymbolAtPosition(0x79));
  ASSERT_STREQ("bar", symbol_map->GetSymbolAtPosition(0x80));
  ASSERT_STREQ("baz", symbol_map->GetSymbolAtPosition(0xe0));
  ASSERT_STREQ("baz", symbol_map->GetSymbolAtPosition(0xff));
  ASSERT_TRUE(nullptr == symbol_map->GetSymbolAtPosition(0x100));
  ASSERT_TRUE(nullptr == symbol_map->GetSymbolAtPosition(0x11f));
  ASSERT_STREQ("quux", symbol_map->GetSymbolAtPosition(0x120));
  ASSERT_STREQ("quux", symbol_map->GetSymbolAtPosition(0x13f));
  ASSERT_TRUE(nullptr == symbol_map->GetSymbolAtPosition(0x140));
  ASSERT_STREQ("aaa", symbol_map->GetSymbolAtPosition(0x150));
  ASSERT_STREQ("aaa", symbol_map->GetSymbolAtPosition(0x15f));
  ASSERT_STREQ("bbb", symbol_map->GetSymbolAtPosition(0x160));
  ASSERT_STREQ("bbb", symbol_map->GetSymbolAtPosition(0x170));
  EXPECT_EQ(symbol_map->num_symbols(), 6);
}

#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // Extra characters inserted in libstdc++
TEST_P(SymbolMapTest, GetDemangledSymbolAtPosition) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  if (!util::DemanglingIsSupported()) {
    LOG(INFO) << "demangling isn't supported on this platform";
    return;
  }
  symbol_map->AddSymbol("_Z3barv", 0x80, 0x60);
  symbol_map->AddSymbol("_Z3bazi", 0xe0, 0x20);
  ASSERT_EQ("", symbol_map->GetDemangledSymbolAtPosition(0x00));
  ASSERT_EQ("foo", symbol_map->GetDemangledSymbolAtPosition(0x40));
  ASSERT_EQ("bar()", symbol_map->GetDemangledSymbolAtPosition(0x80));
  ASSERT_EQ("baz(int)", symbol_map->GetDemangledSymbolAtPosition(0xe0));

  std::string demangled_baz;
  symbol_map->GetDemangledSymbolAtPositionToString(0xe0, &demangled_baz);
  ASSERT_EQ("baz(int)", demangled_baz);

  EXPECT_EQ(symbol_map->num_symbols(), 3);
}
#endif  // GLOOP_UNSUPPORTED_LIBSTDCXX

TEST_P(SymbolMapTest, GetDemangledRustSymbolAtPosition) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("_RNvNtCs09azAZ_5crate6module3foo", 0x40, 0x40);
  ASSERT_EQ("", symbol_map->GetDemangledSymbolAtPosition(0x00));
  ASSERT_EQ("crate::module::foo",
            symbol_map->GetDemangledSymbolAtPosition(0x40));
}

TEST_P(SymbolMapTest, Iterator) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x00, 0x10);
  symbol_map->AddSymbol("bar", 0x10, 0x20);
  symbol_map->AddSymbol("baz", 0x30, 0x30);
  std::unique_ptr<SymbolMap::Iterator> iter(symbol_map->GetIterator());
  ASSERT_EQ(0x00, iter->start());
  ASSERT_STREQ("foo", iter->name());
  ASSERT_EQ(0x10, iter->size());
  iter->Next();
  ASSERT_EQ(0x10, iter->start());
  ASSERT_STREQ("bar", iter->name());
  ASSERT_EQ(0x20, iter->size());
  iter->Next();
  ASSERT_EQ(0x30, iter->start());
  ASSERT_STREQ("baz", iter->name());
  ASSERT_EQ(0x30, iter->size());
  iter->Next();
  ASSERT_TRUE(iter->done());
  EXPECT_EQ(symbol_map->num_symbols(), 3);
}

TEST_P(SymbolMapTest, IteratorWithPositionZero) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x00, 0x10);
  symbol_map->AddSymbol("bar", 0x10, 0x20);
  symbol_map->AddSymbol("baz", 0x30, 0x30);
  std::unique_ptr<SymbolMap::Iterator> iter(symbol_map->GetIteratorFrom(0x0));
  ASSERT_EQ(0x00, iter->start());
  ASSERT_STREQ("foo", iter->name());
  ASSERT_EQ(0x10, iter->size());
  iter->Next();
  ASSERT_EQ(0x10, iter->start());
  ASSERT_STREQ("bar", iter->name());
  ASSERT_EQ(0x20, iter->size());
  iter->Next();
  ASSERT_EQ(0x30, iter->start());
  ASSERT_STREQ("baz", iter->name());
  ASSERT_EQ(0x30, iter->size());
  iter->Next();
  ASSERT_TRUE(iter->done());
  EXPECT_EQ(symbol_map->num_symbols(), 3);
}

TEST_P(SymbolMapTest, IteratorWithPositionMiddle) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x00, 0x10);
  symbol_map->AddSymbol("bar", 0x10, 0x20);
  symbol_map->AddSymbol("baz", 0x30, 0x30);
  std::unique_ptr<SymbolMap::Iterator> iter(symbol_map->GetIteratorFrom(0x5));
  ASSERT_EQ(0x10, iter->start());
  ASSERT_STREQ("bar", iter->name());
  ASSERT_EQ(0x20, iter->size());
  iter->Next();
  ASSERT_EQ(0x30, iter->start());
  ASSERT_STREQ("baz", iter->name());
  ASSERT_EQ(0x30, iter->size());
  iter->Next();
  ASSERT_TRUE(iter->done());
  EXPECT_EQ(symbol_map->num_symbols(), 3);
}

TEST_P(SymbolMapTest, SelfSymbosArePresent) {
  const SymbolMap& symbol_map = SymbolMap::GetCached();
  const auto p_foo = reinterpret_cast<uint64_t>(&Foo);
  EXPECT_TRUE(
      absl::StartsWith(symbol_map.GetSymbolAtPosition(p_foo), "_ZL3Foov"));
  const auto p_bar = reinterpret_cast<uint64_t>(&Bar);
  EXPECT_TRUE(
      absl::StartsWith(symbol_map.GetSymbolAtPosition(p_bar), "_ZL3Barv"));
}

TEST_P(SymbolMapTest, NumberOfSymbols) {
  auto symbol_map = SymbolMap::Create(
      /*copy_symbol_names*/ true, /*compression_level=*/GetParam());
  // We expect at least 4000 symbols in any google3 test binary.
  EXPECT_THAT(symbol_map->num_symbols(), Ge(4000));
}

TEST_P(SymbolMapTest, BytesAllocated) {
  const SymbolMap& symbol_map = SymbolMap::GetCached();
  EXPECT_LT(0, symbol_map.bytes_allocated());
}

TEST_P(SymbolMapTest, UnstrippedBinary) {
  EXPECT_FALSE(SymbolMap::GetCached().binary_is_stripped());
}

#ifdef ABSL_HAVE_VDSO_SUPPORT  // does OS+compiler support vdso?

// Handle symbol aliases, such as __vdso_getcpu vs. getcpu
static std::string CanonicalSymbolName(const std::string& name) {
  if (name.substr(0, 7) == "__vdso_") {
    return name.substr(7);
  }
  return name;
}

// TODO: Reenable this test on ARM64.
#ifndef __aarch64__
TEST_P(SymbolMapTest, VDSOSymbols) {
  absl::debugging_internal::VDSOSupport vdso;
  if (vdso.IsPresent()) {
    const SymbolMap& symbol_map = SymbolMap::GetCached();
    absl::debugging_internal::VDSOSupport::SymbolIterator it = vdso.begin();
    for (; it != vdso.end(); ++it) {
      const ElfW(Sym)* symbol = it->symbol;
      uint64_t address = reinterpret_cast<uintptr_t>(it->address);
      int type = (sizeof(it->address) == 4) ? ELF32_ST_TYPE(symbol->st_info)
                                            : ELF64_ST_TYPE(symbol->st_info);
      // Test that all VDSO functions are present in the symbol map.
      if (type == STT_FUNC) {
        for (int i = 0; i < symbol->st_size; ++i) {
          std::string vdso_symbol = CanonicalSymbolName(it->name);
          std::string symmap_symbol =
              CanonicalSymbolName(symbol_map.GetSymbolAtPosition(address + i));
          EXPECT_EQ(vdso_symbol, symmap_symbol);
        }
      }
    }
  } else {
    LOG(INFO) << "VDSO not present, test skipped";
  }
}
#endif

// Verify that if AT_SYSINFO is present in auxv[], then it has the correct
// symbol in the symbol map (http://b/5237414)
TEST_P(SymbolMapTest, SysinfoIsSymbolized) {
  uint64_t sysinfo = getauxval(AT_SYSINFO);
  VLOG(1) << "sysinfo = 0x" << std::hex << sysinfo;
  if (sysinfo == 0) {
    return;
  }
  const SymbolMap& symbol_map = SymbolMap::GetCached();
  std::string vsyscall = symbol_map.GetSymbolAtPosition(sysinfo);
  EXPECT_EQ("__kernel_vsyscall", vsyscall);
}

#endif

INSTANTIATE_TEST_SUITE_P(SymbolMapTestInstantiation, SymbolMapTest,
                         ::testing::Values(0, 1, 2, 3));

TEST(SymbolMapSizeTest, CompressionDecreasesSymbolMapSize) {
  auto allocated_bytes = [] {
    std::optional<size_t> bytes = tcmalloc::MallocExtension::GetNumericProperty(
        "generic.current_allocated_bytes");
    return *bytes;
  };
  const size_t before_uncompressed = allocated_bytes();
  auto uncompressed_map = SymbolMap::Create(
      /*copy_symbol_names=*/true, /*compression_level=*/0);
  const size_t delta_uncompressed = allocated_bytes() - before_uncompressed;

  const size_t before_compressed = allocated_bytes();
  auto compressed_map = SymbolMap::Create(
      /*copy_symbol_names=*/true, /*compression_level=*/3);
  const size_t delta_compressed = allocated_bytes() - before_compressed;

  EXPECT_THAT(compressed_map->bytes_allocated(),
              Lt(uncompressed_map->bytes_allocated()));
  // Check the memory usage as seen by tcmalloc.
  EXPECT_THAT(delta_compressed, Lt(delta_uncompressed));
  VLOG(1) << "delta uncompressed: " << delta_uncompressed;
  VLOG(1) << "delta   compressed: " << delta_compressed;
  size_t improvement = delta_uncompressed - delta_compressed;
  VLOG(1) << "improvement: " << 100.0 * improvement / delta_compressed << "%";
}

}  // namespace
