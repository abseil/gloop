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
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/node_hash_set.h"
#include "absl/debugging/internal/vdso_support.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gloop/util/symbolize/demangle.h"
#include "gloop/util/symbolize/symbolize-inl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tcmalloc/malloc_extension.h"

using util::SymbolMap;

namespace util {

// Test-only access to SymbolMap internals.
struct SymbolMapTestPeer {
  // Like GetEmpty(), but with copy_symbol_names false, so that the name
  // pointers (and thus the string table layout) are the ones the test passes.
  static std::unique_ptr<SymbolMap> CreateEmpty(int compression_level) {
    return absl::WrapUnique(
        new SymbolMap(/*copy_symbol_names=*/false, compression_level));
  }

  static size_t NameColumnBytes(const SymbolMap& map) {
    map.EnsureFinalized();
    return map.name_shrunk_array_.shrunk_array_byte_length();
  }

  struct Baseline {
    size_t name_bytes = 0;
    size_t total_bytes = 0;
  };

  // Bytes that the previous encoding (global-bias CompressedUintArrays over
  // the raw name pointers, addresses and 32-bit sizes) takes for exactly the
  // symbols of the compressed `map`, at the same compression level.
  static Baseline BaselineBytes(const SymbolMap& map) {
    std::vector<uint64_t> names;
    std::vector<uint64_t> addrs;
    std::vector<uint32_t> sizes;
    std::unique_ptr<SymbolMap::Iterator> it(map.GetIterator());
    for (; !it->done(); it->Next()) {
      names.push_back(reinterpret_cast<uint64_t>(it->name()));
      addrs.push_back(it->start());
      sizes.push_back(static_cast<uint32_t>(it->size()));
    }
    const int level = map.symbol_map_compression_level_;
    SymbolMap::CompressedUintArray name_col, addr_col, size_col;
    name_col.Assign({names.data(), names.size()}, level);
    addr_col.Assign({addrs.data(), addrs.size()}, level);
    size_col.Assign({sizes.data(), sizes.size()}, level);
    return {name_col.shrunk_array_byte_length(),
            name_col.shrunk_array_byte_length() +
                addr_col.shrunk_array_byte_length() +
                size_col.shrunk_array_byte_length()};
  }
};

}  // namespace util

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
  const char* const foo_name = symbol_map.GetSymbolAtPosition(p_foo);
  ASSERT_FALSE(foo_name == nullptr);
  EXPECT_TRUE(absl::StartsWith(foo_name, "_ZL3Foov"));

  const auto p_bar = reinterpret_cast<uint64_t>(&Bar);
  const char* const bar_name = symbol_map.GetSymbolAtPosition(p_bar);
  ASSERT_FALSE(bar_name == nullptr);
  EXPECT_TRUE(absl::StartsWith(bar_name, "_ZL3Barv"));
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

// TODO: Reenable this test on ARM64.
#ifndef __aarch64__
// Handle symbol aliases, such as __vdso_getcpu vs. getcpu
static std::string CanonicalSymbolName(const std::string& name) {
  if (name.substr(0, 7) == "__vdso_") {
    return name.substr(7);
  }
  return name;
}

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

TEST(SymbolMapCompressionParityTest,
     ExhaustiveRealBinaryEquivalenceAcrossAllLevels) {
  auto map0 = SymbolMap::Create(/*copy_symbol_names=*/false, 0);
  ASSERT_GT(map0->num_symbols(), 0);

  struct SymEntry {
    const char* name;
    uint64_t start;
    uint64_t size;
  };
  std::vector<SymEntry> ref_symbols;
  ref_symbols.reserve(map0->num_symbols());
  {
    std::unique_ptr<SymbolMap::Iterator> it0(map0->GetIterator());
    while (!it0->done()) {
      ref_symbols.push_back({it0->name(), it0->start(), it0->size()});
      it0->Next();
    }
  }
  ASSERT_EQ(ref_symbols.size(), static_cast<size_t>(map0->num_symbols()));

  for (int level = 1; level <= 3; ++level) {
    auto map = SymbolMap::Create(/*copy_symbol_names=*/false, level);
    ASSERT_EQ(map->num_symbols(), map0->num_symbols()) << "level=" << level;
    // Names keep the previous encoding byte for byte, and the whole map is
    // smaller than the previous encoding of the same symbols.
    const util::SymbolMapTestPeer::Baseline baseline =
        util::SymbolMapTestPeer::BaselineBytes(*map);
    EXPECT_EQ(util::SymbolMapTestPeer::NameColumnBytes(*map),
              baseline.name_bytes)
        << "level=" << level;
    EXPECT_LT(map->bytes_allocated(), baseline.total_bytes)
        << "level=" << level;

    // 1. Verify full sequential iterator parity (start, size, and C-string
    // equality of name).
    std::unique_ptr<SymbolMap::Iterator> it(map->GetIterator());
    for (size_t i = 0; i < ref_symbols.size(); ++i) {
      ASSERT_FALSE(it->done()) << "level=" << level << " i=" << i;
      EXPECT_EQ(it->start(), ref_symbols[i].start)
          << "level=" << level << " i=" << i;
      EXPECT_EQ(it->size(), ref_symbols[i].size)
          << "level=" << level << " i=" << i;
      EXPECT_STREQ(it->name(), ref_symbols[i].name)
          << "level=" << level << " i=" << i;
      it->Next();
    }
    EXPECT_TRUE(it->done()) << "level=" << level;

    // 2. Exhaustively verify GetSymbolInfoAtPosition at start, interior, and
    // end-boundary positions of every symbol in the binary, plus
    // GetIteratorFrom.
    for (size_t i = 0; i < ref_symbols.size(); ++i) {
      const uint64_t probes[3] = {
          ref_symbols[i].start,
          ref_symbols[i].start + ref_symbols[i].size / 2,
          ref_symbols[i].start + ref_symbols[i].size,
      };
      for (uint64_t pos : probes) {
        const char* name0 = nullptr;
        uint64_t start0 = 0, size0 = 0;
        const bool ok0 =
            map0->GetSymbolInfoAtPosition(pos, &name0, &start0, &size0);

        const char* name_c = nullptr;
        uint64_t start_c = 0, size_c = 0;
        const bool ok_c =
            map->GetSymbolInfoAtPosition(pos, &name_c, &start_c, &size_c);

        ASSERT_EQ(ok_c, ok0)
            << "level=" << level << " i=" << i << " pos=" << pos;
        if (ok0) {
          EXPECT_EQ(start_c, start0) << "level=" << level << " i=" << i;
          EXPECT_EQ(size_c, size0) << "level=" << level << " i=" << i;
          EXPECT_STREQ(name_c, name0) << "level=" << level << " i=" << i;
        }
      }

      if (i % 16 == 0 || i + 1 == ref_symbols.size()) {
        const uint64_t pos = ref_symbols[i].start;
        std::unique_ptr<SymbolMap::Iterator> from0(map0->GetIteratorFrom(pos));
        std::unique_ptr<SymbolMap::Iterator> from_c(map->GetIteratorFrom(pos));
        ASSERT_EQ(from_c->done(), from0->done()) << "level=" << level;
        if (!from0->done()) {
          EXPECT_EQ(from_c->start(), from0->start()) << "level=" << level;
          EXPECT_EQ(from_c->size(), from0->size()) << "level=" << level;
          EXPECT_STREQ(from_c->name(), from0->name()) << "level=" << level;
        }
      }
    }
  }
}

TEST(SymbolMapCompressionParityTest,
     EdgeCasesDuplicateZeroSizeOverflowAndDisjointNames) {
  auto populate = [](SymbolMap* map) {
    // 1. Fill 40 normal symbols so we cross the 32-symbol block boundary.
    for (int i = 0; i < 40; ++i) {
      std::string s = absl::StrCat("sym_normal_", i);
      EXPECT_TRUE(map->AddSymbol(s.c_str(), 0x1000 + i * 64, 48));
    }
    // 2. Duplicate addresses at block boundary (31/32) to verify
    // lexicographical tie-breaking (strcmp(prev_symbol.name, *name) > 0).
    const uint64_t dup_addr = 0x1000 + 31 * 64;
    EXPECT_TRUE(map->AddSymbol("sym_normal_31_alpha", dup_addr, 48));
    EXPECT_TRUE(map->AddSymbol("sym_normal_31_zeta", dup_addr, 48));
    EXPECT_TRUE(map->AddSymbol("sym_normal_31_beta", dup_addr, 48));
    // 3. Zero-size symbol.
    EXPECT_TRUE(map->AddSymbol("zero_size_restore", 0x50000, 0));
    // 4. Symbol with large size (> 64 KiB and > 4 GiB) and cross-segment jump.
    EXPECT_TRUE(map->AddSymbol("huge_func_64k", 0x00007f0000000000ULL, 131072));
    EXPECT_TRUE(
        map->AddSymbol("huge_func_5g", 0x00007f0010000000ULL, 5000000000ULL));
    // 5. High 64-bit address near UINT64_MAX (overflow safety).
    const uint64_t near_max = std::numeric_limits<uint64_t>::max() - 64;
    EXPECT_TRUE(map->AddSymbol("high_addr_near_max", near_max, 32));
  };

  std::unique_ptr<SymbolMap> map0(SymbolMap::GetEmpty(0));
  populate(map0.get());

  const std::vector<uint64_t> query_positions = {
      0,
      0xfff,
      0x1000,
      0x1000 + 31 * 64,
      0x1000 + 31 * 64 + 20,
      0x1000 + 31 * 64 + 50,
      0x1000 + 32 * 64,
      0x50000,
      0x50001,
      0x00007f0000010000ULL,
      0x00007f0000020000ULL,
      0x00007f0010000000ULL + 4000000000ULL,
      std::numeric_limits<uint64_t>::max() - 64,
      std::numeric_limits<uint64_t>::max() - 48,
      std::numeric_limits<uint64_t>::max() - 10,
      std::numeric_limits<uint64_t>::max(),
  };

  for (int level = 1; level <= 3; ++level) {
    std::unique_ptr<SymbolMap> map(SymbolMap::GetEmpty(level));
    populate(map.get());
    ASSERT_EQ(map->num_symbols(), map0->num_symbols()) << "level=" << level;

    // Verify full iterator sequence matches level 0.
    std::unique_ptr<SymbolMap::Iterator> it0(map0->GetIterator());
    std::unique_ptr<SymbolMap::Iterator> it(map->GetIterator());
    while (!it0->done()) {
      ASSERT_FALSE(it->done()) << "level=" << level;
      EXPECT_EQ(it->start(), it0->start()) << "level=" << level;
      EXPECT_EQ(it->size(), it0->size()) << "level=" << level;
      EXPECT_STREQ(it->name(), it0->name()) << "level=" << level;
      it0->Next();
      it->Next();
    }
    EXPECT_TRUE(it->done()) << "level=" << level;

    // Verify duplicate address tie-breaking picks "sym_normal_31_zeta".
    const char* dup_name = map->GetSymbolAtPosition(0x1000 + 31 * 64 + 10);
    ASSERT_NE(dup_name, nullptr);
    EXPECT_STREQ(dup_name, "sym_normal_31_zeta") << "level=" << level;

    // Verify all query positions match level 0 bit-for-bit.
    for (uint64_t pos : query_positions) {
      const char* name0 = nullptr;
      uint64_t start0 = 0, size0 = 0;
      const bool ok0 =
          map0->GetSymbolInfoAtPosition(pos, &name0, &start0, &size0);

      const char* name_c = nullptr;
      uint64_t start_c = 0, size_c = 0;
      const bool ok_c =
          map->GetSymbolInfoAtPosition(pos, &name_c, &start_c, &size_c);

      ASSERT_EQ(ok_c, ok0) << "level=" << level << " pos=" << pos;
      if (ok0) {
        EXPECT_EQ(start_c, start0) << "level=" << level << " pos=" << pos;
        EXPECT_EQ(size_c, size0) << "level=" << level << " pos=" << pos;
        EXPECT_STREQ(name_c, name0) << "level=" << level << " pos=" << pos;
      }

      std::unique_ptr<SymbolMap::Iterator> from0(map0->GetIteratorFrom(pos));
      std::unique_ptr<SymbolMap::Iterator> from_c(map->GetIteratorFrom(pos));
      ASSERT_EQ(from_c->done(), from0->done())
          << "level=" << level << " pos=" << pos;
      if (!from0->done()) {
        EXPECT_EQ(from_c->start(), from0->start())
            << "level=" << level << " pos=" << pos;
        EXPECT_EQ(from_c->size(), from0->size())
            << "level=" << level << " pos=" << pos;
        EXPECT_STREQ(from_c->name(), from0->name())
            << "level=" << level << " pos=" << pos;
      }
    }
  }
}

// Production .stripped binaries (llvm-strip / llvm-objcopy output) have a
// tail-merged .strtab sorted by suffix, so address-adjacent symbols point
// anywhere in a multi-MiB .strtab; unstripped binaries keep link order. For
// both layouts, every compression level must keep the previous name column
// byte for byte, be smaller overall than the previous encoding of the same
// symbols, and answer exactly like level 0.
TEST(SymbolMapCompressionParityTest, SmallerThanBaselineForAnyStrtabOrder) {
  constexpr size_t kNumSyms = 16384;
  std::string strtab;
  strtab.reserve(kNumSyms * 220);
  std::vector<size_t> name_offsets(kNumSyms);
  for (size_t i = 0; i < kNumSyms; ++i) {
    name_offsets[i] = strtab.size();
    absl::StrAppend(&strtab, "_ZN4prod7service18LongMangledSymbolName_", i, "_",
                    std::string(160, static_cast<char>('a' + i % 26)), "Ev");
    strtab.push_back('\0');
  }
  ASSERT_GT(strtab.size(), 3U << 20);  // > 3 MiB synthetic .strtab.
  const char* const strtab_end = strtab.data() + strtab.size();

  struct Layout {
    const char* label;
    size_t (*name_index)(size_t);  // Name of the i-th symbol by address.
  };
  const Layout kLayouts[] = {
      {"tail_merged",
       [](size_t i) -> size_t { return (i * 7919 + 104729) % kNumSyms; }},
      {"link_order", [](size_t i) -> size_t { return i; }},
  };
  // Function-like sizes (48..547 bytes) with some padding between symbols,
  // spanning ~5 MiB of text.
  auto populate = [&](SymbolMap* map, const Layout& layout) {
    uint64_t addr = 0x400000;
    for (size_t i = 0; i < kNumSyms; ++i) {
      const uint64_t size = 48 + (i * 37) % 500;
      ASSERT_TRUE(
          map->AddSymbol(strtab.data() + name_offsets[layout.name_index(i)],
                         strtab_end, addr, size));
      addr += size + (i % 4 == 0 ? 16 : 0);
    }
  };

  for (const Layout& layout : kLayouts) {
    auto map0 = util::SymbolMapTestPeer::CreateEmpty(0);
    populate(map0.get(), layout);
    ASSERT_EQ(map0->num_symbols(), static_cast<int>(kNumSyms));
    for (int level = 1; level <= 3; ++level) {
      SCOPED_TRACE(absl::StrCat(layout.label, " level=", level));
      auto map = util::SymbolMapTestPeer::CreateEmpty(level);
      populate(map.get(), layout);
      ASSERT_EQ(map->num_symbols(), map0->num_symbols());

      const util::SymbolMapTestPeer::Baseline baseline =
          util::SymbolMapTestPeer::BaselineBytes(*map);
      EXPECT_EQ(util::SymbolMapTestPeer::NameColumnBytes(*map),
                baseline.name_bytes);
      const size_t max_bytes = (level == 1) ? baseline.total_bytes * 9 / 10
                                            : baseline.total_bytes * 4 / 5;
      EXPECT_LT(map->bytes_allocated(), max_bytes)
          << "baseline=" << baseline.total_bytes;

      std::unique_ptr<SymbolMap::Iterator> it0(map0->GetIterator());
      std::unique_ptr<SymbolMap::Iterator> it(map->GetIterator());
      for (size_t i = 0; i < kNumSyms; ++i) {
        ASSERT_FALSE(it->done()) << "i=" << i;
        EXPECT_EQ(it->start(), it0->start()) << "i=" << i;
        EXPECT_EQ(it->size(), it0->size()) << "i=" << i;
        EXPECT_EQ(it->name(), it0->name()) << "i=" << i;
        const char* name = nullptr;
        uint64_t start = 0, size = 0;
        ASSERT_TRUE(map->GetSymbolInfoAtPosition(it0->start() + it0->size() / 2,
                                                 &name, &start, &size));
        EXPECT_EQ(name, it0->name()) << "i=" << i;
        EXPECT_EQ(start, it0->start()) << "i=" << i;
        EXPECT_EQ(size, it0->size()) << "i=" << i;
        if (i % 16 == 0) {
          std::unique_ptr<SymbolMap::Iterator> from(
              map->GetIteratorFrom(it0->start()));
          ASSERT_FALSE(from->done()) << "i=" << i;
          EXPECT_EQ(from->start(), it0->start()) << "i=" << i;
          EXPECT_EQ(from->name(), it0->name()) << "i=" << i;
        }
        it0->Next();
        it->Next();
      }
      EXPECT_TRUE(it->done());
    }
  }
}

}  // namespace
