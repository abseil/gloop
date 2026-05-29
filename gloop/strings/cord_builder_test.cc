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

#include "gloop/strings/cord_builder.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <ostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/log/log_streamer.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_buffer.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

// Do not print cord contents, we only care about 'size' perhaps.
// Note that this method must be inside the named namespace.
inline void PrintTo(const absl::Cord& cord, std::ostream* s) {
  if (s) *s << "Cord[" << cord.size() << "]";
}

namespace {

using absl::cord_internal::CordRep;
using absl::cord_internal::kFlatOverhead;

using testing::ElementsAre;
using testing::Eq;
using testing::Gt;
using testing::IsEmpty;
using testing::Le;
using testing::Ne;
using testing::SizeIs;

// Creates a random lower case string of the specified length.
std::string RandomLowercaseString(absl::BitGen& rng, size_t length) {
  std::string result(length, '\0');
  std::uniform_int_distribution<int> char_dist('a', 'z');
  for (char& ch : result) ch = char_dist(rng);
  return result;
}
std::string RandomLowercaseString(size_t length) {
#ifdef ABSL_HAVE_THREAD_LOCAL
  thread_local absl::BitGen rng;
#else
  absl::BitGen rng;
#endif
  return RandomLowercaseString(rng, length);
}

// Returns the contents of the cord as a vector of string_view values.
std::vector<absl::string_view> Fragments(const absl::Cord& cord) {
  return std::vector<absl::string_view>(cord.chunk_begin(), cord.chunk_end());
}

// Returns true if possible running with a Debug allocator
bool PossibleDebugAllocator() {
#if defined(NDEBUG) && !defined(ABSL_HAVE_THREAD_SANITIZER) && \
    !defined(ABSL_HAVE_ADDRESS_SANITIZER) &&                   \
    !defined(ABSL_HAVE_MEMORY_SANITIZER)
  return false;
#else
  return true;
#endif
}

// Returns the expected allocation size for a flat rep of `length` size.
// The returned value assumes reasonable allocators to round up to
// power 2 sizes, with a lower bound of 64 bytes.
// Returns MAX_SIZE_T / 4 when running in debug mode or under sanitizers.
size_t MaxFlatAllocationSize(size_t length) {
  if (PossibleDebugAllocator()) {
    // Return a reasonable large number without overflow risk
    return std::numeric_limits<size_t>::max() / 4;
  }
  size_t size = absl::cord_internal::RoundUpForTag(length + kFlatOverhead);
  return 1 << absl::bit_width(size - 1);  // log2ceil(size)
}

enum TestOptions { kAppend = 0x0, kGetAppendRegion = 0x1 };

std::string TestOptionsToString(testing::TestParamInfo<TestOptions> param) {
  return param.param & kGetAppendRegion ? "GetAppendRegion" : "Append";
}

class CordBuilderTest : public testing::TestWithParam<TestOptions> {};

INSTANTIATE_TEST_SUITE_P(WithParam, CordBuilderTest,
                         testing::Values(TestOptions{kAppend},
                                         TestOptions{kGetAppendRegion}),
                         TestOptionsToString);

// Appends the specified value to `builder` using one or more calls to
// GetAppendRegion() until all data has been consumed, and tests that
// returned span sizes are valid.
void AppendUsingGetAppendRegion(CordBuilder& builder, absl::string_view sv) {
  while (!sv.empty()) {
    absl::Span<char> span = builder.GetAppendRegion(sv.size());
    ASSERT_THAT(span.size(), Gt(0));
    ASSERT_THAT(span.size(), Le(sv.size()));
    memcpy(span.data(), sv.data(), span.size());
    sv.remove_prefix(span.size());
  }
}

TEST(CordBuilderTest, BuildOnEmptyBuilderProducesEmptyCord) {
  CordBuilder builder;
  EXPECT_THAT(builder.Build(), IsEmpty());
}

TEST(CordBuilderTest, GetZeroAppendRegionReturnsEmptyNonNullSpan) {
  CordBuilder builder;
  absl::Span<char> span = builder.GetAppendRegion(0);
  EXPECT_THAT(span, IsEmpty());
  EXPECT_THAT(span.data(), Ne(nullptr));
  EXPECT_THAT(builder.Build(), IsEmpty());
}

// Matcher for a span size matching the given 'block_size' expectation.
// This matcher assumes reasonable memory allocators returning clean power of 2
// sizes from 'size returning operator new' calls, while Debug allocators are
// allowed to be unreasonable and over allocate and report double the memory.
auto MatchesBlockSize(size_t block_size) {
#ifdef NDEBUG
  // Maximum returned size assumes reasonable memory allocators returning
  // clean power of 2 sizes from 'size returning operator new' calls.
  return SizeIs(Eq(block_size - kFlatOverhead));
#else
  // Debug allocators are allowed to be unreasonable.
  return SizeIs(Le(block_size * 2 - kFlatOverhead));
#endif
}

TEST(CordBuilderTest, GetAppendRegionWithSmallSizeReturnsMatchingSpan) {
  for (size_t block_size : {128, 8 << 10}) {
    CordBuilder builder(block_size);
    EXPECT_THAT(builder.GetAppendRegion(10), SizeIs(10));
  }
}

TEST(CordBuilderTest, GetAppendRegionWithHugeSizeReturnsBlockSize) {
  for (size_t block_size : {128, 8 << 10}) {
    CordBuilder builder;
    builder.SetBlockSize(block_size);
    EXPECT_THAT(builder.GetAppendRegion(1 << 20), MatchesBlockSize(block_size));
  }
}

TEST(CordBuilderTest, ShrinkAppendBufferByTruncatesUninitializedSpanData) {
  CordBuilder builder;
  absl::Span<char> span = builder.GetAppendRegion(100);
  memcpy(span.data(), "Abc", 3);
  builder.ShrinkAppendBufferBy(97);
  EXPECT_THAT(builder.Build(), Eq("Abc"));
}

TEST(CordBuilderTest, GetAppendRegionAfterBackupReusesBackedUpBuffer) {
  CordBuilder builder;
  absl::Span<char> span = builder.GetAppendRegion(500);
  memcpy(span.data(), "Abc", 3);
  builder.ShrinkAppendBufferBy(span.size() - 3);

  absl::Span<char> span2 = builder.GetAppendRegion(497);
  EXPECT_THAT(span2.data(), Eq(span.data() + 3));
  EXPECT_THAT(span2.size(), Eq(497));
  memcpy(span2.data(), "defg", 4);
  builder.ShrinkAppendBufferBy(span2.size() - 4);
  EXPECT_THAT(builder.Build(), Eq("Abcdefg"));
}

TEST(CordBuilderTest, GetAppendBufferUsesFullBufferSize) {
  // Request 16 bytes, which is 1 byte over the maximum 'inlined' size, but
  // short enough to guarantee an allocated buffer is larger.
  CordBuilder builder;
  absl::Span<char> span = builder.GetAppendBuffer(16);
  EXPECT_THAT(span.size(), Gt(16));
  memcpy(span.data(), "Abc", 3);
  builder.ShrinkAppendBufferBy(span.size() - 3);
  EXPECT_THAT(builder.Build(), Eq("Abc"));
}

TEST(CordBuilderTest, RepeatedAndZeroSizeCallsToShrinkAppendBufferBy) {
  CordBuilder builder;
  absl::Span<char> span = builder.GetAppendRegion(100);
  memcpy(span.data(), "Abc", 3);
  builder.ShrinkAppendBufferBy(0);
  builder.ShrinkAppendBufferBy(37);
  builder.ShrinkAppendBufferBy(0);
  builder.ShrinkAppendBufferBy(60);
  EXPECT_THAT(builder.Build(), Eq("Abc"));
}

TEST(CordBuilderTest, ShrinkAppendBufferByOnEmptyBuilderAssertsAndDoesNothing) {
  CordBuilder builder1;
  CordBuilder builder2(absl::Cord("Hello world"));
#ifdef NDEBUG
  builder1.ShrinkAppendBufferBy(200);
  builder2.ShrinkAppendBufferBy(100);
  EXPECT_THAT(builder1.Build(), IsEmpty());
  EXPECT_THAT(builder2.Build(), Eq("Hello world"));
#else
  EXPECT_DEATH_IF_SUPPORTED(builder1.ShrinkAppendBufferBy(200), "");
  EXPECT_DEATH_IF_SUPPORTED(builder2.ShrinkAppendBufferBy(100), "");
#endif
}

TEST(CordBuilderTest,
     ShrinkAppendBufferByWithSizeExceedingSpanAssertsAndDoesNothing) {
  CordBuilder builder;
  absl::Span<char> span = builder.GetAppendRegion(3);
  memcpy(span.data(), "Abc", 3);
#ifdef NDEBUG
  builder.ShrinkAppendBufferBy(200);
  EXPECT_THAT(builder.Build(), Eq("Abc"));
#else
  EXPECT_DEATH_IF_SUPPORTED(builder.ShrinkAppendBufferBy(200), "");
#endif
}

#ifndef NDEBUG
TEST(CordBuilderTest, AssertsSetBlockSizeInvariants) {
  CordBuilder builder;
  EXPECT_DEATH_IF_SUPPORTED(builder.SetBlockSize(64), "");   // < 128
  EXPECT_DEATH_IF_SUPPORTED(builder.SetBlockSize(129), "");  // Not power of 2
}
#endif

TEST_P(CordBuilderTest, AppendSingleFragment) {
  const std::string input = RandomLowercaseString(100);

  CordBuilder builder;
  if (GetParam() & kGetAppendRegion) {
    builder.Append(input);
  } else {
    absl::Span<char> span = builder.GetAppendRegion(100);
    ASSERT_THAT(span.size(), Eq(100));
    memcpy(span.data(), input.data(), 100);
  }

  absl::Cord cord = builder.Build();
  EXPECT_THAT(Fragments(cord), ElementsAre(input));
}

TEST_P(CordBuilderTest, AppendThreeFragments) {
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(650),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder builder;
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }

  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
  if (PossibleDebugAllocator()) {
    EXPECT_THAT(Fragments(cord).size(), Eq(3));
  }
}

// This test is identical to AppendThreeFragments except that we verify
// that the produced cord has zero waste when using a size hint.
TEST_P(CordBuilderTest, AppendThreeFragmentsWithSizeHint) {
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(650),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder builder;
  for (absl::string_view sv : inputs) {
    builder.IncreaseSizeHintBy(sv.size());
  }
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }

  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
  EXPECT_THAT(Fragments(cord), ElementsAre(value));
}

// This test is identical to AppendThreeFragmentsWithSizeHint except that we
// also verify that setting a smaller block size of 1KB is respected, resulting
// in a cord of two fragments with zero waste.
TEST_P(CordBuilderTest, AppendThreeFragmentsWithSizeHintAndSmallBlockSize) {
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(650),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder builder;
  builder.SetBlockSize(1024);
  for (absl::string_view sv : inputs) {
    builder.IncreaseSizeHintBy(sv.size());
  }
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }

  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
  EXPECT_THAT(Fragments(cord).size(), Eq(2));
}

// Tests adding 30 fragments of increasing size of roughly 1K to the builder,
// resulting in a cord guaranteed to span multiple buffers / flats.
TEST_P(CordBuilderTest, AppendThirtyFragments) {
  std::string value;
  std::vector<std::string> inputs;
  for (int i = 0; i < 30; ++i) {
    inputs.push_back(RandomLowercaseString(1000 + i * 10));
    value.append(inputs.back());
  }

  CordBuilder builder;
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }

  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

// This test is identical to AppendThirtyFragments except that we verify
// that the produced cord has zero waste when using a size hint.
TEST_P(CordBuilderTest, AppendThirtyFragmentsWithSizeHint) {
  std::string value;
  std::vector<std::string> inputs;
  for (int i = 0; i < 30; ++i) {
    inputs.push_back(RandomLowercaseString(1000 + i * 10));
    value.append(inputs.back());
  }

  CordBuilder builder;
  for (absl::string_view sv : inputs) {
    builder.IncreaseSizeHintBy(sv.size());
  }
  for (absl::string_view sv : inputs) {
    if ((GetParam() & kGetAppendRegion)) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }

  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

TEST(CordBuilderTest, AppendSingleCord) {
  const std::string input = RandomLowercaseString(100);
  CordBuilder builder;
  builder.Append(absl::Cord(input));
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(input));
}

TEST(CordBuilderTest, AppendThreeCords) {
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(650),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder builder;
  for (absl::string_view sv : inputs) {
    builder.Append(absl::Cord(sv));
  }
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

TEST_P(CordBuilderTest, AppendCordsAndFragments) {
  const std::string inputs[] = {
      RandomLowercaseString(300), RandomLowercaseString(400),
      RandomLowercaseString(650), RandomLowercaseString(450),
      RandomLowercaseString(550),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder builder;
  bool append_as_cord = false;
  for (absl::string_view sv : inputs) {
    if (append_as_cord) {
      builder.Append(absl::Cord(sv));
    } else {
      if (GetParam() & kGetAppendRegion) {
        AppendUsingGetAppendRegion(builder, sv);
      } else {
        builder.Append(sv);
      }
    }
    append_as_cord = !append_as_cord;
  }
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

// This test verifies that appending to an existing private cord results in the
// correct output, and re-uses spare capacity into the original cord.
TEST_P(CordBuilderTest, AppendToExistingSharedCord) {
  // Create cord with plenty spare capacity
  absl::Cord existing;
  std::string value = RandomLowercaseString(1000);
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(1500);
  buffer.SetLength(value.size());
  memcpy(buffer.data(), value.data(), value.size());
  existing.Append(std::move(buffer));

  // Fragments exceeding the spare capacity.
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(650),
  };
  value.append(absl::StrJoin(inputs, ""));

  CordBuilder builder(std::move(existing));
  for (absl::string_view sv : inputs) {
    builder.IncreaseSizeHintBy(sv.size());
  }
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

// This test verifies that appending an existing private cord with spare
// capacity results in the correct output and the spare capacity in the added
// cord being used on subsequent Append and GetAppendRegion calls.
TEST_P(CordBuilderTest, AppendFragmentsAfterAppendingCordWithCapacity) {
  // Create cord with plenty spare capacity
  absl::Cord existing;
  std::string value = RandomLowercaseString(1000);
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(1500);
  buffer.SetLength(value.size());
  memcpy(buffer.data(), value.data(), value.size());
  existing.Append(std::move(buffer));

  // Fragments exceeding the spare capacity.
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(650),
  };
  value.append(absl::StrJoin(inputs, ""));

  CordBuilder builder;
  for (absl::string_view sv : inputs) {
    builder.IncreaseSizeHintBy(sv.size());
  }
  builder.Append(std::move(existing));
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      AppendUsingGetAppendRegion(builder, sv);
    } else {
      builder.Append(sv);
    }
  }
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

// Tests that we can move a CordBuilder before, during, and directly after
// a sequence of Append and GetAppendRegion calls with the expected outcome.
// This verifies that all internal data and buffers are correctly moved.
TEST_P(CordBuilderTest, Move) {
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(350),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder main_builder;
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      while (!sv.empty()) {
        CordBuilder builder(std::move(main_builder));
        absl::Span<char> span = builder.GetAppendRegion(sv.size());
        ASSERT_THAT(span.size(), Gt(0));
        ASSERT_THAT(span.size(), Le(sv.size()));
        memcpy(span.data(), sv.data(), span.size());
        sv.remove_prefix(span.size());
        main_builder = std::move(builder);
      }
    } else {
      CordBuilder builder(std::move(main_builder));
      builder.Append(sv);
      main_builder = std::move(builder);
    }
  }
  CordBuilder builder(std::move(main_builder));
  EXPECT_THAT(main_builder.Build(), IsEmpty());  // NOLINT
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

// Identical to the Move test, except that we verify that the 'perfect size
// hint' behavior of the builder is preserved on move.
TEST_P(CordBuilderTest, MoveWithSizeHint) {
  const std::string inputs[] = {
      RandomLowercaseString(300),
      RandomLowercaseString(400),
      RandomLowercaseString(350),
  };
  const std::string value = absl::StrJoin(inputs, "");

  CordBuilder main_builder;
  for (absl::string_view sv : inputs) {
    CordBuilder builder(std::move(main_builder));
    builder.IncreaseSizeHintBy(sv.size());
    main_builder = std::move(builder);
  }
  for (absl::string_view sv : inputs) {
    if (GetParam() & kGetAppendRegion) {
      while (!sv.empty()) {
        CordBuilder builder(std::move(main_builder));
        absl::Span<char> span = builder.GetAppendRegion(sv.size());
        ASSERT_THAT(span.size(), Gt(0));
        memcpy(span.data(), sv.data(), span.size());
        sv.remove_prefix(span.size());
        main_builder = std::move(builder);
      }
    } else {
      CordBuilder builder(std::move(main_builder));
      builder.Append(sv);
      main_builder = std::move(builder);
    }
  }
  CordBuilder builder(std::move(main_builder));
  EXPECT_THAT(main_builder.Build(), IsEmpty());  // NOLINT
  absl::Cord cord = builder.Build();
  EXPECT_THAT(cord, Eq(value));
}

// FuzzTest runs a random set of operations on a CordBuilder for two seconds,
// increasing the chance to find any lurking corner case or new/old bug.
// The test loops for 2 seconds, with 99% of the loops spend doing Append and
// GetAppendRegion calls. The other 1% is spent building the final cord and
// checking it against the control and moving the builder or adding size hints.
TEST(CordBuilderTest, FuzzTest) {
  absl::BitGen rng;
  CordBuilder builder;
  absl::Cord control;
  absl::Cord shared_cord1;  // Used for keeping 'constructor' cord shared
  absl::Cord shared_cord2;  // Used for keeping 'append' cord shared
  size_t block_size = 4 << 10;
  absl::Span<char> last_span;

  auto maybe = [&](float probability) {
    return std::bernoulli_distribution(probability)(rng);
  };
  auto uniform = [&](int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
  };
  auto uniform_sz = [&](size_t lo, size_t hi) {
    return std::uniform_int_distribution<size_t>(lo, hi)(rng);
  };
  auto coin_flip = [&]() { return std::bernoulli_distribution(0.5)(rng); };

  std::uniform_int_distribution<int> char_dist('a', 'z');
  std::uniform_int_distribution<size_t> size_dist(0, block_size * 2);

  absl::Time end_time = absl::Now() + absl::Seconds(2);
  while (absl::Now() < end_time) {
    for (int i = 0; i < 100; ++i) {
      // 99% of the actions are append.
      if (maybe(0.99)) {
        // 75% GetAppendRegion(n) / GetAppendBuffer(n)
        // 23% Append(string_view)
        // 1% Append(Cord)
        // 1% Backup
        const int pick = uniform(0, 100);
        if (pick <= 75) {
          last_span = coin_flip() ? builder.GetAppendRegion(size_dist(rng))
                                  : builder.GetAppendBuffer(size_dist(rng));
          for (auto& ch : last_span) ch = char_dist(rng);
          control.Append(absl::string_view(last_span.data(), last_span.size()));
        } else if (pick <= 98) {
          std::string value = RandomLowercaseString(rng, size_dist(rng));
          builder.Append(value);
          control.Append(value);
          last_span = {};
        } else if (pick <= 99) {
          // Backup(span.size) or Backup(0) should always be legal.
          size_t n = uniform_sz(0, last_span.size());
          builder.ShrinkAppendBufferBy(n);
          control.RemoveSuffix(n);
          last_span.remove_suffix(n);
        } else {
          last_span = {};
          std::string value = RandomLowercaseString(rng, size_dist(rng));
          if (coin_flip()) {
            // Donate rvalue
            builder.Append(absl::Cord(value));
          } else {
            // Provide shared cord
            shared_cord2 = value;
            builder.Append(shared_cord2);
          }
          control.Append(value);
        }
      } else {
        // 45% Build and new
        // 40% Build and append
        // 10% adjust hint
        // 5% Move
        const int pick = uniform(0, 100);
        if (pick <= 85) {
          // Recreate / Build: coin flip ShrinkAppendBufferBy
          if (!last_span.empty() && coin_flip()) {
            size_t n = uniform_sz(0, last_span.size());
            builder.ShrinkAppendBufferBy(n);
            control.RemoveSuffix(n);
          }
          absl::Cord cord = builder.Build();
          ASSERT_EQ(cord, control);

          // Create new builder
          builder.~CordBuilder();
          if (pick <= 45) {
            // New
            new (&builder) CordBuilder();
            control.Clear();
          } else {
            // Append
            if (coin_flip()) {
              // Donate existing cord
              new (&builder) CordBuilder(std::move(cord));
            } else {
              // Provide shared copy of existing cord
              shared_cord1 = cord;
              new (&builder) CordBuilder(shared_cord1);
            }
          }
          last_span = {};

          // Fifty fifty set block size
          if (coin_flip()) {
            block_size = 1 << uniform(7, 16);  // 128 --- 64K
            builder.SetBlockSize(block_size);
          } else {
            block_size = 4 << 10;
          }
          size_dist = std::uniform_int_distribution<size_t>(0, block_size * 2);
        } else if (pick <= 95) {
          // 10% size hint at 8x mean random_size should lag somewhat but
          // advance enough to be ahead enough times.
          const size_t size_hint = size_dist(rng) * 8;
          builder.IncreaseSizeHintBy(size_hint);
        } else {
          // Move
          CordBuilder tmp(std::move(builder));
          builder = std::move(tmp);
        }
      }
    }
  }
}

}  // namespace
}  // namespace strings
