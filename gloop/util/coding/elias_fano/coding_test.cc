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

#include "gloop/util/coding/elias_fano/coding.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/random/zipf_distribution.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/googleinit.h"
#include "gloop/util/coding/elias_fano/options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace elias_fano {
namespace testing {

struct CodecAccess {
  // Encoder.
  static void SetBit(Encoder* enc, BitPos pos) { enc->SetBit(pos); }
  static size_t ForceFinalize(Encoder* enc) {
    enc->size_ = enc->opts_.size();
    enc->last_value_ = enc->opts_.max_value();
    return *enc->Finalize();
  }

  // Decoder.
  static BitPos SelectOne(const Decoder& dec, uint32_t rank) {
    return dec.Select(1, rank, false).first;
  }
  static BitPos SelectZero(const Decoder& dec, uint32_t rank) {
    return dec.Select(0, rank, false).first;
  }
};

namespace {

using ::fuzztest::Arbitrary;
using ::fuzztest::BitFlagCombinationOf;
using ::fuzztest::InRange;
using ::fuzztest::VectorOf;

uint32_t NumOnes(absl::Span<const uint64_t> bitmap) {
  uint32_t res = 0;
  for (auto word : bitmap) res += absl::popcount(word);
  return res;
}

template <class DecoderType>
std::shared_ptr<DecoderType> MakeSharedDecoder(std::vector<char> mem,
                                               absl::Span<const char> encoded) {
  struct Impl {
    explicit Impl(std::vector<char> mem, absl::Span<const char> encoded)
        : mem(std::move(mem)), decoder(DecoderType::Make(encoded).value()) {}
    std::vector<char> mem;
    DecoderType decoder;
  };
  auto impl = std::make_shared<Impl>(std::move(mem), encoded);
  return std::shared_ptr<DecoderType>(impl, &impl->decoder);
}

std::shared_ptr<Decoder> EncodeIndex(absl::Span<const uint64_t> bitmap) {
  uint32_t num_ones = NumOnes(bitmap);  // aka size of Elias-Fano array
  uint32_t num_bits = bitmap.size() * 64;
  uint32_t num_zeros = num_bits - num_ones;
  Options opts(num_ones, num_zeros);
  opts.override_width(0).indexes(Options::ONES | Options::ZEROS);
  CHECK_EQ(num_ones, opts.one_bits());
  CHECK_EQ(num_zeros, opts.zero_bits());

  std::vector<char> mem(Encoder::MaxEncodingBytes(opts));
  Encoder encoder(opts, absl::MakeSpan(mem));
  for (BitPos pos(0); pos != BitPos{num_bits}; ++pos)
    if (bitmap[pos.value() / 64] & (uint64_t{1} << (pos.value() % 64)))
      CodecAccess::SetBit(&encoder, pos);
  mem.resize(CodecAccess::ForceFinalize(&encoder));
  auto encoded = absl::MakeSpan(mem);
  return MakeSharedDecoder<Decoder>(std::move(mem), encoded);
}

class SelectIndexTest : public ::testing::Test {
 public:
  void Test(absl::Span<const uint32_t> positions) {
    TestBitmap(BitmapFromPositions(positions.back() + 1, positions));
  }

  void TestBitmap(absl::Span<const uint64_t> bitmap) {
    auto decoder = EncodeIndex(bitmap);

    int zeros = 0, ones = 0;
    for (size_t mask_no = 0; mask_no < bitmap.size(); ++mask_no) {
      for (int bit_no = 0; bit_no < 64; bit_no++) {
        bool bit = bitmap[mask_no] & (1LL << bit_no);
        BitPos position(static_cast<uint64_t>(mask_no) * 64 + bit_no);

        if (bit) {
          EXPECT_EQ(CodecAccess::SelectOne(*decoder, ones), position) << ones;
          ones++;
        } else {
          EXPECT_EQ(CodecAccess::SelectZero(*decoder, zeros), position)
              << zeros;
          zeros++;
        }
      }
    }
  }

  std::vector<uint64_t> BitmapFromPositions(
      int size, absl::Span<const uint32_t> positions) {
    uint64_t num_words = (size - 1) / 64 + 1;
    std::vector<uint64_t> bitmap(num_words);
    for (uint64_t pos : positions) bitmap[pos >> 6] |= 1ULL << (pos % 64);
    return bitmap;
  }

  std::vector<uint64_t> RandomBitmapWithJumps(uint32_t size,
                                              uint32_t jump_size) {
    absl::uniform_int_distribution<> bin(0, 1);
    absl::uniform_int_distribution<uint32_t> jumps(0, jump_size - 1);
    std::vector<uint32_t> positions;
    for (uint32_t position = 0; position < size; position++) {
      if (bin(rng_)) positions.push_back(position);
      if (bin(rng_)) position += jumps(rng_);
    }
    return BitmapFromPositions(size, positions);
  }

 private:
  absl::BitGen rng_;
};

TEST_F(SelectIndexTest, SingleBit) { Test({1}); }

TEST_F(SelectIndexTest, LastBit) { Test({63}); }

TEST_F(SelectIndexTest, SingleWord) {
  Test({1, 2, 3, 4, 6, 7, 9, 10, 12, 13, 17, 20, 21, 25, 32, 40, 43, 55});
}

TEST_F(SelectIndexTest, SingleWordLateBits) {
  Test({20, 21, 25, 32, 40, 43, 55});
}

TEST_F(SelectIndexTest, TwoWords) { Test({0, 10, 74, 120}); }

TEST_F(SelectIndexTest, TenWords) { Test({0, 10, 64, 523, 600}); }

TEST_F(SelectIndexTest, EmptyBitmap) { TestBitmap({0, 0}); }

TEST_F(SelectIndexTest, SparseOrDense) {
  std::vector<uint32_t> positions;
  for (int i = 0; i <= 254; ++i) positions.push_back(i);
  positions.push_back(64000);
  positions.push_back(100000);
  Test(positions);
}

TEST_F(SelectIndexTest, FullBitmap) {
  TestBitmap({0xFFFFFFFFFFFFFFFFLL, 0xFFFFFFFFFFFFFFFFLL});
}

TEST_F(SelectIndexTest, SparseTest1) {
  // Set rare positions, so 256 ones span on more than 1 << 16 positions.
  std::vector<uint32_t> positions = {0};
  for (int i = 0; i < 600; i++) positions.push_back(positions.back() + 512);
  Test(positions);
}

TEST_F(SelectIndexTest, SparseTest2) {
  // Set rare positions, so 256 ones span on a bit less than 1 << 16 positions.
  std::vector<uint32_t> positions = {0};
  for (int i = 0; i < 600; i++) positions.push_back(positions.back() + 220);
  Test(positions);
}

TEST_F(SelectIndexTest, RandomTestsWithVariousDistribution) {
  TestBitmap(RandomBitmapWithJumps(1000000, 0));
  TestBitmap(RandomBitmapWithJumps(1000000, 1));
  TestBitmap(RandomBitmapWithJumps(1000000, 10));
  TestBitmap(RandomBitmapWithJumps(1000000, 100));
  TestBitmap(RandomBitmapWithJumps(1000000, 1000));
  TestBitmap(RandomBitmapWithJumps(1000000, 10000));
  TestBitmap(RandomBitmapWithJumps(1000000, 100000));
  TestBitmap(RandomBitmapWithJumps(1000000, 1000000));
}

void TestIteration(absl::Span<const char> mem,
                   absl::Span<const uint64_t> values) {
  auto dec = Decoder::Make(mem);
  ABSL_EXPECT_OK(dec);
  EXPECT_EQ(dec->size(), values.size());
  for (auto i = dec->begin(), e = dec->end(); i != e; ++i) {
    EXPECT_EQ(*i, values[i.index()]);
  }
  auto fdec = ForwardDecoder::Make(mem);
  ABSL_EXPECT_OK(fdec);
  EXPECT_EQ(fdec->size(), values.size());
  for (; fdec->index() != fdec->size(); fdec->Next()) {
    EXPECT_EQ(fdec->value(), values[fdec->index()]);
  }
}

void CheckOneIndexOps(absl::Span<const char> mem,
                      absl::Span<const uint64_t> values) {
  auto dec = Decoder::Make(mem);
  ABSL_ASSERT_OK(dec);
  EXPECT_EQ(dec->size(), values.size());
  for (size_t i = 0; i != values.size(); ++i) {
    EXPECT_EQ(*dec->IteratorAt(i), values[i]);
  }
  const uint32_t num_ranges = values.size() > 1 ? values.size() - 1 : 0;
  std::vector<uint32_t> indices(num_ranges);
  absl::c_iota(indices, 0);
  if (!values.empty()) indices.push_back(values.size() - 1);
  indices.push_back(values.size());
  indices.push_back(std::numeric_limits<uint32_t>::max());
  uint32_t seen = 0;
  dec->RangesAt(indices, [&](uint32_t idx, uint64_t a, uint64_t b) {
    EXPECT_EQ(idx, seen);
    if (idx < num_ranges) {
      EXPECT_EQ(a, values[idx]);
      EXPECT_EQ(b, values[idx + 1]);
    } else {
      ADD_FAILURE() << "out-of-range idx=" << idx;
    }
    ++seen;
  });
  EXPECT_EQ(seen, num_ranges);

  auto fdec = ForwardDecoder::Make(mem);
  ABSL_ASSERT_OK(fdec);
  EXPECT_EQ(fdec->size(), values.size());
  for (size_t i = 0; i != values.size(); ++i) {
    fdec->AdvanceToIndex(i);
    EXPECT_EQ(fdec->value(), values[i]);
  }
  if (!values.empty()) fdec->Next();
  EXPECT_TRUE(fdec->done());
}

template <typename Int>
void CheckUpperBounds(const Decoder& dec, absl::Span<const uint64_t> values,
                      const std::vector<Int>& queries) {
  std::vector<uint32_t> got;
  dec.UpperBounds(queries, [&](uint32_t idx) { got.push_back(idx); });
  ASSERT_EQ(got.size(), queries.size());
  for (size_t i = 0; i < queries.size(); ++i) {
    const uint32_t want = static_cast<uint32_t>(
        absl::c_upper_bound(values, uint64_t{queries[i]}) - values.begin());
    EXPECT_EQ(got[i], want) << "q=" << queries[i];
  }
}

void CheckZeroIndexOps(absl::Span<const char> mem,
                       absl::Span<const uint64_t> values) {
  auto dec = Decoder::Make(mem);
  ABSL_ASSERT_OK(dec);
  EXPECT_EQ(dec->size(), values.size());
  std::vector<uint64_t> queries64 = {0, 1};
  std::vector<uint32_t> queries32;
  queries32.reserve(48);
  for (uint32_t q = 0; q < 40; ++q) queries32.push_back(q);
  size_t i = 0;
  for (auto x : *dec) {
    EXPECT_LE(i, values.size());
    EXPECT_EQ(x, values[i]);
    if (i > 0 && values[i - 1] < values[i]) {
      uint64_t diff = values[i] - values[i - 1];
      // Get a pseudorandom value in between 'values[i-1]' and 'values[i]'
      uint64_t mid_val = values[i - 1] + (i * 0x9ddfea08eb382d69) % diff;
      EXPECT_EQ(dec->UpperBound(mid_val), i);
      queries64.push_back(mid_val);
      queries64.push_back(values[i]);
      if (mid_val <= std::numeric_limits<uint32_t>::max()) {
        queries32.push_back(static_cast<uint32_t>(mid_val));
      }
      if (values[i] <= std::numeric_limits<uint32_t>::max()) {
        queries32.push_back(static_cast<uint32_t>(values[i]));
      }
      if (mid_val != values[i - 1]) {
        EXPECT_EQ(dec->LowerBound(mid_val), i);
        EXPECT_FALSE(dec->Exists(mid_val));
      } else {
        EXPECT_TRUE(dec->Exists(mid_val));
      }
      EXPECT_EQ(dec->LowerBound(values[i]), i);
    } else {
      EXPECT_TRUE(dec->Exists(values[i]));
    }
    ++i;
  }
  if (!values.empty() && values.back() < std::numeric_limits<uint64_t>::max()) {
    queries64.push_back(values.back() + 1);
  }
  queries64.push_back(std::numeric_limits<uint64_t>::max());
  queries32.push_back(std::numeric_limits<uint32_t>::max());
  CheckUpperBounds(*dec, values, queries64);
  CheckUpperBounds(*dec, values, queries32);
}

void CheckBulkOps(absl::Span<const char> mem, absl::Span<const uint64_t> values,
                  absl::Span<const uint32_t> fuzzed_indices,
                  const std::vector<uint64_t>& fuzzed_queries64,
                  const std::vector<uint32_t>& fuzzed_queries32) {
  auto dec = Decoder::Make(mem);
  ABSL_ASSERT_OK(dec);

  // Mix in-range (unsorted/duplicate) and raw fuzzed indices so RangesAt
  // exercises valid lookups alongside out-of-range skips across all index
  // configurations (including Options::NONE).
  const uint32_t num_ranges = values.size() > 1 ? values.size() - 1 : 0;
  std::vector<uint32_t> indices;
  indices.reserve(fuzzed_indices.size() + 3);
  for (size_t i = 0; i < fuzzed_indices.size(); ++i) {
    uint32_t idx = fuzzed_indices[i];
    if (num_ranges > 0 && (i % 2 == 0)) idx %= num_ranges;
    indices.push_back(idx);
  }
  if (!values.empty()) indices.push_back(values.size() - 1);
  indices.push_back(values.size());
  indices.push_back(std::numeric_limits<uint32_t>::max());

  std::vector<std::tuple<uint32_t, uint64_t, uint64_t>> got_ranges;
  dec->RangesAt(indices, [&](uint32_t idx, uint64_t a, uint64_t b) {
    got_ranges.emplace_back(idx, a, b);
  });
  std::vector<std::tuple<uint32_t, uint64_t, uint64_t>> want_ranges;
  for (uint32_t idx : indices) {
    if (idx < num_ranges) {
      want_ranges.emplace_back(idx, values[idx], values[idx + 1]);
    }
  }
  EXPECT_EQ(got_ranges, want_ranges);

  CheckUpperBounds(*dec, values, fuzzed_queries64);
  CheckUpperBounds(*dec, values, fuzzed_queries32);

  dec->RangesAt({}, [&](uint32_t, uint64_t, uint64_t) { ADD_FAILURE(); });
  dec->UpperBounds(absl::Span<const uint64_t>{},
                   [&](uint32_t) { ADD_FAILURE(); });
  dec->UpperBounds(absl::Span<const uint32_t>{},
                   [&](uint32_t) { ADD_FAILURE(); });
}

void Roundtrip(std::vector<uint64_t> values,
               Options::Index indexes = Options::ALL,
               const std::vector<uint32_t>& range_indices = {},
               const std::vector<uint64_t>& queries64 = {},
               const std::vector<uint32_t>& queries32 = {}) {
  absl::c_sort(values);

  Encoder2 enc(indexes);
  EXPECT_TRUE(enc.Append(values));

  const auto mem = enc.Finalize();
  ABSL_EXPECT_OK(GetInfo(mem));
  ABSL_EXPECT_OK(Validate(mem));

  const auto opts = Options::Decode(mem);
  ABSL_EXPECT_OK(opts);

  TestIteration(mem, values);
  auto supports_index_ops = [&](auto index) {
    return opts->bits() <= Options::kMaxBitsNoIndex || opts->has_index(index);
  };
  if (supports_index_ops(Options::ONES)) CheckOneIndexOps(mem, values);
  if (supports_index_ops(Options::ZEROS)) CheckZeroIndexOps(mem, values);
  CheckBulkOps(mem, values, range_indices, queries64, queries32);
}
FUZZ_TEST(CodingTest, Roundtrip)
    .WithDomains(VectorOf(Arbitrary<uint64_t>()),
                 BitFlagCombinationOf({Options::ONES, Options::ZEROS}),
                 VectorOf(Arbitrary<uint32_t>()),
                 VectorOf(Arbitrary<uint64_t>()),
                 VectorOf(Arbitrary<uint32_t>()));

TEST(CodingTest, CutAtWord) {
  // N = 32, max_value = 65 => l = 1
  // The last bit is high(65) + 31 = 32 + 31 = 63,
  // which is the last bit of the word, and the one before it
  // is high(64) + 30 = 32 + 30 = 62.
  Roundtrip({0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
             16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 64, 65});
}

TEST(CodingTest, Sparse) {
  // Insert at least 2^16 values to make sparse indices.
  const uint32_t N = Options::kR;
  const int W = 4;
  std::vector<uint64_t> v;
  v.reserve(N + 2);
  // N (1 << W) values.
  v.resize(N, 1 << W);
  // a (2 << W) value.
  v.push_back(2 << W);
  // a (N << W) + prev value.
  v.push_back((N << W) + v.back());

  Roundtrip(v);
}

// Disabled because it uses 10s of GB of memory and takes several minutes to
// finish. You can run this manually by passing these flags to the test binary:
//   --gunit_filter=*.DISABLED_MaxEncoding --gunit_also_run_disabled_tests
TEST(CodingTest, DISABLED_MaxEncoding1111111110000____00001) {
  constexpr auto N = std::numeric_limits<uint32_t>::max();
  constexpr auto M = uint64_t{std::numeric_limits<uint32_t>::max()};

  Encoder2 enc;

  for (uint64_t i = 1; i < N; ++i) EXPECT_TRUE(enc.Append(0));
  ASSERT_TRUE(enc.Append(M));

  const auto mem = enc.Finalize();

  ABSL_EXPECT_OK(GetInfo(mem));

  auto dec = Decoder::Make(mem);
  ABSL_EXPECT_OK(dec);

  for (uint64_t i = 1; i < N; ++i) EXPECT_EQ(*dec->IteratorAt(i - 1), 0);
  EXPECT_EQ(*dec->IteratorAt(N - 1), M);
}

TEST(CodingTest, DISABLED_MaxEncoding00000000000111111111____1) {
  constexpr auto N = std::numeric_limits<uint32_t>::max();
  constexpr auto M = uint64_t{std::numeric_limits<uint32_t>::max()};

  Encoder2 enc;
  for (uint64_t i = 0; i < N; ++i) EXPECT_TRUE(enc.Append(M));
  const auto mem = enc.Finalize();

  ABSL_EXPECT_OK(GetInfo(mem));

  auto dec = Decoder::Make(mem);
  ABSL_EXPECT_OK(dec);
  for (uint64_t i = 0; i < N; ++i) EXPECT_EQ(*dec->IteratorAt(i), M);
}

TEST(Finalize, Equivalent) {
  Encoder2 e1;
  Encoder2 e2;
  for (int n = 0; n < 200; ++n) {
    ASSERT_TRUE(e1.Append(n));
    ASSERT_TRUE(e2.Append(n));
  }

  absl::string_view e1_encoded = e1.Finalize();
  auto e2_encoded = e2.FinalizeToStr();
  ABSL_ASSERT_OK(e2_encoded);
  ASSERT_EQ(*e2_encoded, e1_encoded);
}

std::string EncodeSorted(std::vector<uint64_t> values,
                         Options::Index indexes = Options::ALL) {
  absl::c_sort(values);
  Encoder2 enc(indexes);
  CHECK(enc.Append(values));
  return std::string(enc.Finalize());
}

// 1000 values in [0, 2^20), so many consecutive values share high bits and
// both indexes are dense.
std::string DenseEncoding() {
  absl::BitGen rng;
  std::vector<uint64_t> values(1000);
  for (auto& v : values) v = absl::Uniform<uint64_t>(rng, 0, 1 << 20);
  return EncodeSorted(values);
}

// Same values as CodingTest.Sparse, which use both dense and sparse entries.
std::string SparseEncoding() {
  const uint32_t N = Options::kR;
  std::vector<uint64_t> values(N, 1 << 4);
  values.push_back(2 << 4);
  values.push_back((N << 4) + values.back());
  return EncodeSorted(values);
}

// Expects Validate() to reject `mem` with any single bit flipped in every
// `step`-th byte of [begin, end).
void ExpectBitFlipsRejected(std::string mem, size_t begin, size_t end,
                            size_t step = 1) {
  for (size_t i = begin; i < end; i += step) {
    for (int bit = 0; bit < 8; ++bit) {
      mem[i] ^= 1 << bit;
      EXPECT_FALSE(Validate(mem).ok()) << "byte " << i << " bit " << bit;
      mem[i] ^= 1 << bit;
    }
  }
}

// Checks that queries on a span accepted by Validate() stay in bounds and
// decode non-decreasing values. Run under ASAN to catch out of bounds reads.
void CheckDecodable(absl::Span<const char> mem) {
  auto opts = Options::Decode(mem);
  ABSL_ASSERT_OK(opts);
  auto dec = Decoder::Make(mem);
  ABSL_ASSERT_OK(dec);
  std::vector<uint64_t> values(dec->begin(), dec->end());
  ASSERT_EQ(values.size(), opts->size());
  EXPECT_TRUE(absl::c_is_sorted(values));
  if (!values.empty()) EXPECT_EQ(values.back(), opts->max_value());

  auto fdec = ForwardDecoder::Make(mem);
  ABSL_ASSERT_OK(fdec);
  for (; !fdec->done(); fdec->Next()) {
    EXPECT_EQ(fdec->value(), values[fdec->index()]);
  }
  fdec->Reset();
  for (uint64_t v : values) {
    fdec->AdvanceToValue(v);
    ASSERT_FALSE(fdec->done());
    EXPECT_EQ(fdec->value(), v);
  }
  // Also covers empty arrays, where max_value is the only valid argument.
  fdec->Reset();
  fdec->AdvanceToValue(opts->max_value());
  EXPECT_EQ(fdec->index(), static_cast<uint32_t>(
                               absl::c_lower_bound(values, opts->max_value()) -
                               values.begin()));

  // Small arrays have no indexes; their queries scan the unary stream instead.
  const bool small = opts->bits() <= Options::kMaxBitsNoIndex;
  if (small || opts->has_index(Options::ONES)) CheckOneIndexOps(mem, values);
  if (small || opts->has_index(Options::ZEROS)) {
    CheckZeroIndexOps(mem, values);
    // Also covers empty arrays, which CheckZeroIndexOps() does not query.
    for (uint64_t v : {uint64_t{0}, opts->max_value()}) {
      EXPECT_EQ(dec->LowerBound(v),
                static_cast<uint32_t>(absl::c_lower_bound(values, v) -
                                      values.begin()));
      EXPECT_EQ(dec->UpperBound(v),
                static_cast<uint32_t>(absl::c_upper_bound(values, v) -
                                      values.begin()));
    }
  }
}

// Calls CheckDecodable() if Validate() accepts `bytes`. Both run on an exactly
// sized heap copy, so that ASAN catches reads even one byte past the end,
// which a std::string's inline buffer or spare capacity would hide.
void ExpectRejectedOrDecodable(absl::string_view bytes) {
  const std::vector<char> mem(bytes.begin(), bytes.end());
  if (Validate(mem).ok()) CheckDecodable(mem);
}

TEST(ValidateTest, AcceptsEmpty) { ABSL_EXPECT_OK(Validate(EncodeSorted({}))); }

TEST(ValidateTest, AcceptsDenseAndSparse) {
  ABSL_EXPECT_OK(Validate(DenseEncoding()));
  const std::string mem = SparseEncoding();
  auto info = GetInfo(mem);
  ABSL_ASSERT_OK(info);
  ASSERT_GT(info->sparse_index_bytes, 0);
  ABSL_EXPECT_OK(Validate(mem));
}

TEST(ValidateTest, RejectsGarbage) {
  EXPECT_FALSE(Validate("").ok());
  EXPECT_FALSE(Validate("This is definitely not valid Elias-Fano data").ok());
}

TEST(ValidateTest, RejectsUnsupportedVersion) {
  std::string mem = EncodeSorted({1, 2, 3});
  ASSERT_EQ(mem[0], Options::VERSION3);
  mem[0] = 1;
  EXPECT_THAT(Validate(mem),
              absl_testing::StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(ValidateTest, RejectsUnknownIndexFlags) {
  std::string mem = DenseEncoding();
  ASSERT_EQ(mem[1], Options::ALL);
  mem[1] = 0x7;
  EXPECT_THAT(Validate(mem),
              absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ValidateTest, RejectsTruncated) {
  for (const std::string& mem : {DenseEncoding(), SparseEncoding()}) {
    EXPECT_FALSE(Validate(mem.substr(0, mem.size() - 1)).ok());
  }
  // Cut in the middle of the sparse entries.
  const std::string mem = SparseEncoding();
  auto info = GetInfo(mem);
  ABSL_ASSERT_OK(info);
  EXPECT_FALSE(
      Validate(mem.substr(0, mem.size() - info->sparse_index_bytes / 2)).ok());
}

TEST(ValidateTest, RejectsOptionsLargerThanSpan) {
  // Options for 2^32 - 1 values with 24 low bits each, i.e. about 13 GB of
  // high and low bits, followed by only a few bytes.
  Options opts(std::numeric_limits<uint32_t>::max(), ~uint64_t{0} >> 8);
  std::string mem(Options::EncodingLength(opts) + 64, '\0');
  Options::Encode(opts, absl::MakeSpan(mem));
  EXPECT_THAT(Validate(mem),
              absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ValidateTest, RejectsEmptyWithNonZeroMaxValueOrWidth) {
  // Encoder2 writes max_value 0 and width 0 for an empty array. Otherwise,
  // e.g. ForwardDecoder::AdvanceToValue(max_value) would read past the end.
  const std::vector<std::pair<uint64_t, uint8_t>> max_values_and_widths = {
      {101, 1}, {50, 0}, {0, 1}, {0, Options::kMaxWidth}};
  for (const auto& [max_value, width] : max_values_and_widths) {
    Options opts(0, max_value);
    opts.override_width(width);
    std::string mem(Encoder::MinEncodingBytes(opts), '\0');
    Options::Encode(opts, absl::MakeSpan(mem));
    // Set the sentinel, so that only the options are wrong.
    const size_t high = Options::EncodingLength(opts);
    mem[high + opts.bits() / 8] = 1 << (opts.bits() % 8);
    EXPECT_THAT(Validate(mem),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument))
        << opts.DebugString();
  }
}

TEST(ValidateTest, RejectsCorruptUnaryStreamAndDenseIndex) {
  const std::string mem = DenseEncoding();
  auto info = GetInfo(mem);
  ABSL_ASSERT_OK(info);
  ASSERT_EQ(info->sparse_index_bytes, 0);
  const size_t high = info->meta_bytes;
  const size_t dense = high + info->high_bytes + info->low_bytes;
  ExpectBitFlipsRejected(mem, high, high + info->high_bytes);
  ExpectBitFlipsRejected(mem, dense, dense + info->dense_index_bytes);
}

TEST(ValidateTest, RejectsCorruptSparseIndex) {
  const std::string mem = SparseEncoding();
  auto info = GetInfo(mem);
  ABSL_ASSERT_OK(info);
  const size_t dense = info->meta_bytes + info->high_bytes + info->low_bytes;
  const size_t sparse = dense + info->dense_index_bytes;
  // Validating this encoding is relatively slow, so only sample the bytes.
  ExpectBitFlipsRejected(mem, dense, sparse, /*step=*/97);
  ExpectBitFlipsRejected(mem, sparse, sparse + info->sparse_index_bytes,
                         /*step=*/61);
}

TEST(ValidateTest, RejectsDecreasingValues) {
  std::string mem = DenseEncoding();
  auto info = GetInfo(mem);
  ABSL_ASSERT_OK(info);
  ASSERT_GT(info->opts.width(), 0);
  // Flipping a low bit keeps the encoding readable, and it must be rejected
  // iff the decoded values are no longer non-decreasing up to max_value.
  const size_t low = info->meta_bytes + info->high_bytes;
  int rejected = 0;
  for (size_t i = low; i < low + info->low_bytes; ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      mem[i] ^= 1 << bit;
      auto dec = Decoder::Make(mem);
      ABSL_ASSERT_OK(dec);
      std::vector<uint64_t> values(dec->begin(), dec->end());
      const bool want_ok =
          absl::c_is_sorted(values) && values.back() == info->opts.max_value();
      EXPECT_EQ(Validate(mem).ok(), want_ok) << "byte " << i << " bit " << bit;
      rejected += !want_ok;
      mem[i] ^= 1 << bit;
    }
  }
  EXPECT_GT(rejected, 0);
}

void CorruptedEncodingIsRejectedOrDecodable(std::vector<uint64_t> values,
                                            Options::Index indexes,
                                            size_t offset, uint8_t mask) {
  std::string mem = EncodeSorted(std::move(values), indexes);
  mem[offset % mem.size()] ^= mask;
  ExpectRejectedOrDecodable(mem);
}
FUZZ_TEST(ValidateTest, CorruptedEncodingIsRejectedOrDecodable)
    .WithDomains(VectorOf(Arbitrary<uint64_t>()),
                 BitFlagCombinationOf({Options::ONES, Options::ZEROS}),
                 Arbitrary<size_t>(), Arbitrary<uint8_t>());

void ArbitraryBytesAreRejectedOrDecodable(const std::string& mem) {
  ExpectRejectedOrDecodable(mem);
}
FUZZ_TEST(ValidateTest, ArbitraryBytesAreRejectedOrDecodable);

// Like ArbitraryBytesAreRejectedOrDecodable, but with a well formed options
// header for small arrays, so that the fuzzer reaches the checks after it.
void ArbitraryOptionsAreRejectedOrDecodable(uint32_t indexes, uint32_t size,
                                            uint64_t max_value, uint32_t width,
                                            const std::string& payload) {
  std::string mem;
  auto put_varint = [&mem](uint64_t v) {
    for (; v >= 0x80; v >>= 7) mem.push_back(static_cast<char>(v | 0x80));
    mem.push_back(static_cast<char>(v));
  };
  put_varint(Options::VERSION3);
  put_varint(indexes);
  put_varint(size);
  put_varint(max_value);
  put_varint(width);
  mem.append(payload);
  ExpectRejectedOrDecodable(mem);
}
FUZZ_TEST(ValidateTest, ArbitraryOptionsAreRejectedOrDecodable)
    .WithDomains(InRange<uint32_t>(0, Options::ALL), InRange<uint32_t>(0, 64),
                 InRange<uint64_t>(0, 256),
                 InRange<uint32_t>(0, Options::kMaxWidth),
                 Arbitrary<std::string>())
    // Empty arrays with only the sentinel and the 7 trailing bytes, but a
    // non-zero width or max_value.
    .WithSeeds({{0, 0, 0, 1, std::string("\x01\0\0\0\0\0\0\0", 8)},
                {0, 0, 101, 1,
                 std::string("\0\0\0\0\0\0\x04\0\0\0\0\0\0\0", 14)}});

namespace bm {

using ::benchmark::DoNotOptimize;

struct Uniform {
  template <class URNG>
  static std::vector<uint64_t> MakeEntries(const Options& opts, URNG* rng) {
    std::vector<uint64_t> res(opts.size());
    res[0] = opts.max_value();
    absl::uniform_int_distribution<uint64_t> dist(0, opts.max_value());
    std::generate(res.begin() + 1, res.end(), [&]() { return dist(*rng); });
    return res;
  }
};

struct Zipf {
  template <class URNG>
  static std::vector<uint64_t> MakeEntries(const Options& opts, URNG* rng) {
    std::vector<uint64_t> res(opts.size());
    res[0] = opts.max_value();
    absl::zipf_distribution<uint64_t> dist(opts.max_value());
    std::generate(res.begin() + 1, res.end(), [&]() { return dist(*rng); });
    return res;
  }
};

struct Uneven {
  template <class URNG>
  static std::vector<uint64_t> MakeEntries(const Options& opts, URNG* rng) {
    std::vector<uint64_t> res(opts.size());
    std::fill(res.begin() + res.size() / 2, res.end(), opts.max_value());
    return res;
  }
};

// Encode all inputs at program startup to speed up benchmark runs. Otherwise
// we end up generating a gazillion random uint64s and encoding them elias fano
// style over and over.
//
// The state is templetized on distribution so that we get one GlobalState per
// distribution we want to benchmark.
template <class Dist, bool EveryIndex = false>
class GlobalState {
 public:
  static GlobalState* get() {
    static auto* res = new GlobalState();
    return res;
  }

  static void Args(::benchmark::Benchmark* bm) {
    for (auto version : {3})
      for (auto size : sizes())
        for (auto delta : deltas()) {
          if (EveryIndex) {
            for (auto index : indexes()) {
              bm->Args({version, size, delta, index});
            }
          } else {
            bm->Args({version, size, delta});
          }
        }
  }

  absl::Span<const char> GetEncoded(const Options& opts) {
    auto& encoded = encoded_[opts];
    if (encoded.empty()) {
      encoded.resize(Encoder::MaxEncodingBytes(opts));
      auto entries = Dist::MakeEntries(opts, &rng_);
      std::sort(entries.begin(), entries.end());
      Encoder enc(opts, absl::MakeSpan(encoded));
      for (auto entry : entries) enc.Append(entry);
      encoded.resize(*enc.Finalize());
    }
    return encoded;
  }

  // Returns a vector of size with uniform random values in range [0, limit).
  template <class T>
  std::vector<T> UniformInRange(int size, T limit) {
    std::vector<T> res(size);
    std::uniform_int_distribution<T> dist(0, limit - 1);
    std::generate(res.begin(), res.end(), [&]() { return dist(rng_); });
    return res;
  }

  // Returns a vector of size size, with values in range [0, limit). The values
  // are in monotonically increasing groups of values where consecutive values
  // will have difference in range [ceil(jump / 2), ceil(3 * jump / 2)). For
  // example UniformJumpsInRange(10, 3, 10) might return:
  //   {0, 2, 5, 7, 3, 6, 8, 1, 2, 7, 8}
  //   ^ group a ^ group b   ^ group c ^
  template <class T>
  std::vector<T> UniformJumpsInRange(int size, T jump, T limit) {
    T min_jump = ceil(0.5 * jump);
    std::vector<T> res;
    res.reserve(size);
    std::uniform_int_distribution<T> dist(0, jump);
    for (T v = 0; res.size() != size;) {
      v += min_jump + dist(rng_);
      if (v >= limit) v = dist(rng_);
      res.push_back(v);
    }
    return res;
  }

 private:
  static std::vector<int> sizes() {
    return {1,   2,   4,       8,       16,      32,      64,     128,
            256, 512, 1 << 10, 1 << 16, 1 << 20, 1 << 22, 1 << 24};
  }

  static std::vector<int> deltas() { return {0, 1, 16, 32}; }

  static std::vector<int> indexes() {
    return {
        Options::Index::ONES,
        Options::Index::ZEROS,
        Options::Index::ALL,
    };
  }

  struct OptionsLess {
    bool operator()(const Options& a, const Options& b) const {
      return std::forward_as_tuple(a.size(), a.max_value()) <
             std::forward_as_tuple(b.size(), b.max_value());
    }
  };

  absl::BitGen rng_;
  std::map<Options, std::vector<char>, OptionsLess> encoded_;
};

// Adjust benchmark flag defaults to suit our benchmarks better.
REGISTER_MODULE_INITIALIZER(elias_fano_coding_benchmarks, {
  SetCommandLineOptionWithMode("benchmark_min_time", "1", SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("benchmark_min_iters", "1", SET_FLAGS_DEFAULT);
});

#define CODING_BENCHMARK_IMPL(name, every_index)                          \
  void BM_Uniform_##name(benchmark::State& state) {                       \
    name<GlobalState<Uniform, every_index>>(&state);                      \
  }                                                                       \
  BENCHMARK(BM_Uniform_##name)                                            \
      ->Apply(GlobalState<Uniform, every_index>::Args);                   \
  void BM_Zipf_##name(benchmark::State& state) {                          \
    name<GlobalState<Zipf, every_index>>(&state);                         \
  }                                                                       \
  BENCHMARK(BM_Zipf_##name)->Apply(GlobalState<Zipf, every_index>::Args); \
  void BM_Uneven_##name(benchmark::State& state) {                        \
    name<GlobalState<Uneven, every_index>>(&state);                       \
  }                                                                       \
  BENCHMARK(BM_Uneven_##name)->Apply(GlobalState<Uneven, every_index>::Args)

#define CODING_BENCHMARK_WITH_EVERY_INDEX(name) \
  CODING_BENCHMARK_IMPL(name, true)
#define CODING_BENCHMARK(name) CODING_BENCHMARK_IMPL(name, false)

template <class State>
void Encode(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.indexes(state->range(3));
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  std::vector<char> encoded(Encoder::MaxEncodingBytes(opts));
  while (state->KeepRunningBatch(size)) {
    Encoder enc(opts, absl::MakeSpan(encoded));
    for (auto v : dec) enc.Append(v);
    CHECK_OK(enc.Finalize());
    DoNotOptimize(enc);
  }
  // Get stats.
  Encoder enc(opts, absl::MakeSpan(encoded));
  for (auto v : dec) enc.Append(v);
  CHECK_OK(enc.Finalize());
  state->SetLabel(GetInfo(encoded)->DebugString());
}
CODING_BENCHMARK_WITH_EVERY_INDEX(Encode);

template <class State>
void SelectOne(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto ranks = State::get()->template UniformInRange<uint32_t>(100 << 10,
                                                               opts.one_bits());
  while (state->KeepRunningBatch(ranks.size())) {
    for (auto r : ranks) DoNotOptimize(CodecAccess::SelectOne(dec, r));
  }
}
CODING_BENCHMARK(SelectOne);

template <class State>
void SelectZero(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto ranks = State::get()->template UniformInRange<uint32_t>(
      100 << 10, opts.zero_bits());
  while (state->KeepRunningBatch(ranks.size())) {
    for (auto r : ranks) DoNotOptimize(CodecAccess::SelectZero(dec, r));
  }
}
CODING_BENCHMARK(SelectZero);

template <class State>
void Iterate(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto begin = dec.begin();
  auto end = dec.end();
  auto it = begin;
  for (auto _ : *state) {
    if (it == end) it = begin;
    DoNotOptimize(++it);
    DoNotOptimize(*it);
  }
}
CODING_BENCHMARK(Iterate);

template <class State>
void IterateForwardDecoderNext(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = ForwardDecoder::Make(State::get()->GetEncoded(opts)).value();
  for (auto _ : *state) {
    if (dec.done()) dec.Reset();
    dec.Next();
    DoNotOptimize(dec.value());
  }
}
CODING_BENCHMARK(IterateForwardDecoderNext);

template <class State>
void IterateForwardDecoderAdvanceToIndex(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = ForwardDecoder::Make(State::get()->GetEncoded(opts)).value();
  int i = 0;
  for (auto _ : *state) {
    if (dec.done()) {
      dec.Reset();
      i = 0;
    }
    dec.AdvanceToIndex(i++);
    DoNotOptimize(dec.value());
  }
}
CODING_BENCHMARK(IterateForwardDecoderAdvanceToIndex);

template <class State>
void IteratorAt(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto indexes =
      State::get()->template UniformInRange<uint32_t>(100 << 10, opts.size());
  while (state->KeepRunningBatch(indexes.size())) {
    for (auto i : indexes) DoNotOptimize(dec.IteratorAt(i));
  }
}
CODING_BENCHMARK(IteratorAt);

template <class State>
void IteratorAtOrdered(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto indexes = State::get()->template UniformJumpsInRange<uint32_t>(
      100 << 10, std::max(1u, opts.size() / 10), opts.size());
  while (state->KeepRunningBatch(indexes.size())) {
    for (auto i : indexes) DoNotOptimize(dec.IteratorAt(i));
  }
}
CODING_BENCHMARK(IteratorAtOrdered);

template <class State>
void UpperBound(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto values = State::get()->template UniformInRange<uint64_t>(
      100 << 10, opts.max_value() + 1);
  while (state->KeepRunningBatch(values.size())) {
    for (auto v : values) DoNotOptimize(dec.UpperBound(v));
  }
}
CODING_BENCHMARK(UpperBound);

template <class State>
void UpperBoundOrdered(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto values = State::get()->template UniformJumpsInRange<uint64_t>(
      100 << 10, std::max(uint64_t{1}, opts.max_value() / opts.size()),
      opts.max_value() + 1);
  while (state->KeepRunningBatch(values.size())) {
    for (auto v : values) DoNotOptimize(dec.UpperBound(v));
  }
}
CODING_BENCHMARK(UpperBoundOrdered);

// Iterates the decoder by calling LowerBound at values that should advance
// the decoder by one element on average.
template <class State>
void SmallJumps(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).value();
  auto values = State::get()->template UniformJumpsInRange<uint64_t>(
      100 << 10, uint64_t{1} << delta, opts.max_value() + 1);
  while (state->KeepRunningBatch(values.size())) {
    for (auto v : values) {
      auto index = dec.LowerBound(v);
      DoNotOptimize(*dec.IteratorAt(index));
    }
  }
}
CODING_BENCHMARK(SmallJumps);

template <class State>
void SmallJumpsForwardDecoder(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto dec = ForwardDecoder::Make(State::get()->GetEncoded(opts)).value();
  auto values = State::get()->template UniformJumpsInRange<uint64_t>(
      100 << 10, uint64_t{1} << delta, opts.max_value() + 1);
  while (state->KeepRunningBatch(values.size())) {
    uint64_t last_value = 0;
    for (auto v : values) {
      if (v < last_value) dec.Reset();
      dec.AdvanceToValue(v);
      DoNotOptimize(dec.value());
      last_value = v;
    }
  }
}
CODING_BENCHMARK(SmallJumpsForwardDecoder);

template <class State>
void Validate(benchmark::State* state) {
  int version = state->range(0);
  int size = state->range(1);
  int delta = state->range(2);
  Options opts(size, static_cast<uint64_t>(size) << delta);
  opts.version(version);
  auto encoded = State::get()->GetEncoded(opts);
  while (state->KeepRunningBatch(size)) {
    CHECK_OK(elias_fano::Validate(encoded));
  }
}
CODING_BENCHMARK(Validate);

void WorstCase(benchmark::State& state) {
  static std::string* encoded = new std::string;
  if (encoded->empty()) {
    Encoder2 enc;
    CHECK(enc.Append(0));

    // max value == 2^33 * (2^16 - 256) + 2^16 - 257 - 1
    // size ==              2^16 - 256
    // w == 33
    // Then all non-zero keys have high_bits == 2^16 - 256. Thus,
    // the first D2 block spans 2^16 bits and has 2^16 - 256 zero bits between
    // 0-th and 1-th one bit. For example, select1(1) will read
    // ceil((2^16 - 256 + 1) / 64) == 1021 words.
    for (size_t i = 0; i < ((1 << 16) - 257); ++i) {
      CHECK(enc.Append((1ull << 33) * ((1 << 16) - 256) + i));
    }

    *encoded = enc.Finalize();
  }
  auto dec = Decoder::Make(*encoded).value();

  for (auto s : state) DoNotOptimize(dec.IteratorAt(1));
}
BENCHMARK(WorstCase);

}  // namespace bm
}  // namespace
}  // namespace testing
}  // namespace elias_fano
