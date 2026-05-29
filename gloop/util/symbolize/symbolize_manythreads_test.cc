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
#include <optional>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/synchronization/barrier.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gtest/gtest.h"
#include "tcmalloc/malloc_extension.h"

using util::SymbolMap;

const int kNumThreads = 100;

class MyThread : public Thread {
 public:
  MyThread(thread::Options& options, absl::Barrier* barrier, bool create_map)
      : Thread(options, "MyThread"),
        barrier_(barrier),
        create_map_(create_map) {}

 protected:
  void Run() override {
    barrier_->Block();
    if (create_map_) {
      const util::SymbolMap& map = util::SymbolMap::GetCached();
      VLOG(2) << "Symbols: " << map.num_symbols();
    }
  }

 private:
  absl::Barrier* barrier_;
  const bool create_map_;
};

// Note: this test needs to be the only test in this process.
TEST(SymbolMap, ManyThreads) {
  std::vector<std::unique_ptr<MyThread>> thread;
  thread.reserve(kNumThreads);
  thread::Options options;
  options.set_joinable(true);

  // First we create / join no-op threads, to account for overhead of
  // thread creation.
  absl::Barrier barrier1(kNumThreads);
  for (int j = 0; j < kNumThreads; j++) {
    thread.emplace_back(
        std::make_unique<MyThread>(options, &barrier1, /*create_map=*/false));
    thread.back()->Start();
  }
  for (int j = 0; j < kNumThreads; j++) {
    thread[j]->Join();
  }
  thread.clear();

  ASSERT_FALSE(SymbolMap::IsCacheInitialized());

  // Measure "initial" heap and RSS usage.
  int64_t rss_before, rss_after;
  std::optional<size_t> heap_before =
      tcmalloc::MallocExtension::GetNumericProperty(
          "generic.current_allocated_bytes");
  ASSERT_TRUE(heap_before.has_value());
  rss_before = MemoryUsage(0);

  // Now create threads that all race to init SymbolMap::GetCached().
  absl::Barrier barrier2(kNumThreads);
  for (int j = 0; j < kNumThreads; j++) {
    thread.emplace_back(
        std::make_unique<MyThread>(options, &barrier2, /*create_map=*/true));
    thread.back()->Start();
  }
  for (int j = 0; j < kNumThreads; j++) {
    thread[j]->Join();
  }
  thread.clear();

  // Collect "after" measurements.
  std::optional<size_t> heap_after =
      tcmalloc::MallocExtension::GetNumericProperty(
          "generic.current_allocated_bytes");
  ASSERT_TRUE(heap_after.has_value());
  rss_after = MemoryUsage(0);

  const size_t heap_delta = *heap_after - *heap_before;
  const int64_t rss_delta = rss_after - rss_before;
  LOG(INFO) << "Heap increase   " << heap_delta << " bytes";
  LOG(INFO) << "RSS increase     " << rss_delta << " bytes";

  const util::SymbolMap& map = util::SymbolMap::GetCached();
  const size_t symbol_map_bytes = map.bytes_allocated();
  LOG(INFO) << "SymbolMap bytes " << symbol_map_bytes << " bytes";
  LOG(INFO) << "RSS delta / SymbolMap bytes "
            << static_cast<double>(rss_delta) / symbol_map_bytes;

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_THREAD_SANITIZER) || defined(ABSL_HAVE_MEMORY_SANITIZER)
  // As of cr/156613953,
  // under ASan, the increase is ~7.2x.
  // under MSan, the increase is ~20x.
  // under TSan, the increase is ~33x.
  //
  // Either way, we don't really care -- sanitizers have their own memory
  // requirements that are irrelevant to the original bug.
#else
  double expected_symbol_map_bytes_increase_factor = 0.0;
  printf("====== %d\n\n\n", absl::GetFlag(FLAGS_symbol_map_compression_level));
  switch (absl::GetFlag(FLAGS_symbol_map_compression_level)) {
    case 0:
      // As of cr/273549420, observed increases are sometimes in 8.3x range.
      expected_symbol_map_bytes_increase_factor = 10.0;
      break;
    // With symbol map compression, we temporarily allocate additional buffers.
    case 1:
      // As of cr/273549420, observed increases are sometimes in 15.7x range.
      expected_symbol_map_bytes_increase_factor = 18.0;
      break;
    case 2:
    case 3:
      expected_symbol_map_bytes_increase_factor = 35.0;
      break;
  }
  // RSS increase should not be much greater than heap and symbol map increase.
  EXPECT_LT(rss_delta,
            expected_symbol_map_bytes_increase_factor * symbol_map_bytes)
      << "RSS delta is " << static_cast<double>(rss_delta) / symbol_map_bytes
      << " times larger than symbol map bytes used (expected "
      << expected_symbol_map_bytes_increase_factor
      << "x max for compression level "
      << absl::GetFlag(FLAGS_symbol_map_compression_level) << ")";
#endif
}
