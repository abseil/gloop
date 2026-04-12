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

// Small test for mapping files
//
// Must be run as root with -uid= to prevent switching back
// to nobody if mlock mode is being tested
//
// Must start rfserver if testing remote files
//
// ./bin/testmmap --datadir=data --v=1 [--uid= --mlock] [--remote]

#include "gloop/strings/memblock.h"

#include <stdint.h>
#include <string.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"

ABSL_FLAG(bool, mlock, false, "Try mlocking mode");
ABSL_FLAG(bool, remote, false, "Test remote files");
ABSL_FLAG(bool, test_long_test, false, "Run a longer test");
// This requires more disk space and just fails on my machine
// due to that, so I've added a separate flag for it
ABSL_FLAG(bool, unittest_bigfiles, false, "Run with multi-gig files");

namespace strings {
namespace {

TEST(CordFromMemBlock, NewedMemBlock) {
  const std::string str(16, 'x');
  char* data = new char[str.size()];
  memcpy(data, str.data(), str.size());
  absl::Cord c =
      CordFromMemBlock(std::make_unique<NewedMemBlock>(data, str.size()));
  EXPECT_EQ(str, std::string(c));
}

TEST(CordFromMemBlock, NewedMemBlockSingleArg) {
  const std::string str(16, 'x');
  auto memblock = std::make_unique<NewedMemBlock>(str.size());
  EXPECT_EQ(memblock->length(), str.size());
  memcpy(memblock->data(), str.data(), str.size());
  absl::Cord c = CordFromMemBlock(std::move(memblock));
  EXPECT_EQ(str, std::string(c));
}

TEST(CordFromMemBlock, NewedConstMemBlock) {
  const std::string str(16, 'x');
  char* data = new char[str.size()];
  memcpy(data, str.data(), str.size());
  absl::Cord c =
      CordFromMemBlock(std::make_unique<const NewedMemBlock>(data, str.size()));
  EXPECT_EQ(str, std::string(c));
}

TEST(CordFromMemBlock, MemBlockNew) {
  const std::string str(16, 'x');
  auto memblock_ptr = MemBlock::New(str.size());
  memcpy(memblock_ptr->data(), str.data(), str.size());
  absl::Cord c = CordFromMemBlock(std::move(memblock_ptr));
  EXPECT_EQ(str, std::string(c));
}

TEST(CordFromMemBlock, StringDataMemBlock) {
  const std::string str(16, 'x');
  absl::Cord c = CordFromMemBlock(
      std::make_unique<StringDataMemBlock>(new std::string(str)));
  EXPECT_EQ(str, std::string(c));
}

TEST(ReleaserFunctions, DeleteCharArray) {
  const std::string str(16, 'x');
  char* buf = new char[str.size()];
  memcpy(buf, str.data(), str.size());
  absl::Cord c =
      absl::MakeCordFromExternal(absl::string_view(buf, str.size()),
                                 [buf] { strings::DeleteCharArray(buf); });
  EXPECT_EQ(str, std::string(c));
}

TEST(ReleaserFunctions, DeleteMemBlock) {
  const std::string str(16, 'x');
  StringDataMemBlock* b =
      new StringDataMemBlock(std::make_unique<std::string>(str));
  absl::Cord c = absl::MakeCordFromExternal(
      b->ToStringPiece(), [b] { strings::DeleteMemBlock(b); });
  EXPECT_EQ(str, std::string(c));
}

TEST(ReleaserFunctions, DeleteString) {
  const std::string str(16, 'x');
  std::string* s = new std::string(str);
  absl::Cord c =
      absl::MakeCordFromExternal(*s, [s] { strings::DeleteString(s); });
  EXPECT_EQ(str, std::string(c));
}

TEST(ReleaserFunctions, NoopReleaser) {
  static constexpr char data[] = "0123456789abcdef";
  absl::string_view sp(data, sizeof(data) - 1);
  absl::Cord c = absl::MakeFragmentedCord({sp});
  EXPECT_EQ(std::string(sp), std::string(c));
}

TEST(EstimatedMemoryUsage, EmptyMemBlock) {
  // This should hit the NoCleanupMemblock case.
  auto block = MemBlock::New(0);
  EXPECT_EQ(block->GetStats().memory_usage, sizeof(MemBlock));
}

TEST(EstimatedMemoryUsage, SwissMemBlock) {
  // This should hit the SwissMemblock case.
  auto block = MemBlock::New(1);
  // SwissMemBlock stores a size_t in the data in its destructor, so it
  // allocates at least that much for the memory buffer.
  EXPECT_EQ(block->GetStats().memory_usage, sizeof(MemBlock) + sizeof(size_t));
}

TEST(EstimatedMemoryUsage, NewedMemBlock) {
  // This should hit the NewedMemBlock case.
  auto block = MemBlock::New(1023);
  EXPECT_EQ(block->GetStats().memory_usage, 1023 + sizeof(MemBlock));
}

void BM_MemBlockNew(benchmark::State& state) {
  int num_blocks = state.range(0);
  CHECK_EQ(num_blocks & (num_blocks - 1), 0)
      << "num_blocks has to be a power of two";
  while (state.KeepRunningBatch(num_blocks)) {
    // We access blocks in pseudo-random order to avoid L1 caching.
    std::vector<std::unique_ptr<MemBlock>> blocks(num_blocks);
    for (int64_t i = 0; i < num_blocks; ++i) {
      blocks[(i * 5741) & (num_blocks - 1)] = MemBlock::New((i * 5741) & 0xFFF);
    }
  }
}
BENCHMARK(BM_MemBlockNew)->Arg(1 << 10)->Arg(1 << 20);

void BM_NewedMemBlock(benchmark::State& state) {
  int num_blocks = state.range(0);
  CHECK_EQ(num_blocks & (num_blocks - 1), 0)
      << "num_blocks has to be a power of two";
  while (state.KeepRunningBatch(num_blocks)) {
    // We access blocks in pseudo-random order to avoid L1 caching.
    std::vector<std::unique_ptr<MemBlock>> blocks(num_blocks);
    for (int64_t i = 0; i < num_blocks; ++i) {
      size_t len = (i * 5741) & 0xFFF;
      char* block = new char[len];
      blocks[(i * 5741) & (num_blocks - 1)] =
          std::make_unique<NewedMemBlock>(block, len);
    }
  }
}
BENCHMARK(BM_NewedMemBlock)->Arg(1 << 10)->Arg(1 << 20);

}  // namespace
}  // namespace strings
