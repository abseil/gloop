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
#include "absl/random/random.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/random/zipf_distribution.h"
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
  static void SetBit(Encoder* enc, BitPos pos) { return enc->SetBit(pos); }
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
        : mem(std::move(mem)),
          decoder(DecoderType::Make(encoded).ValueOrDie()) {}
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
    for (int mask_no = 0; mask_no < bitmap.size(); mask_no++) {
      for (int bit_no = 0; bit_no < 64; bit_no++) {
        bool bit = bitmap[mask_no] & (1LL << bit_no);
        BitPos position(static_cast<uint64_t>(mask_no) * 64 + bit_no);

        if (bit) {
          EXPECT_EQ(position, CodecAccess::SelectOne(*decoder, ones)) << ones;
          ones++;
        } else {
          EXPECT_EQ(position, CodecAccess::SelectZero(*decoder, zeros))
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

using ::fuzztest::Arbitrary;
using ::fuzztest::BitFlagCombinationOf;
using ::fuzztest::VectorOf;

void TestIteration(absl::Span<const char> mem,
                   absl::Span<const uint64_t> values) {
  auto dec = Decoder::Make(mem);
  EXPECT_OK(dec);
  EXPECT_EQ(dec->size(), values.size());
  for (auto i = dec->begin(), e = dec->end(); i != e; ++i) {
    EXPECT_EQ(*i, values[i.index()]);
  }
  auto fdec = ForwardDecoder::Make(mem);
  EXPECT_OK(fdec);
  EXPECT_EQ(fdec->size(), values.size());
  for (; fdec->index() != fdec->size(); fdec->Next()) {
    EXPECT_EQ(fdec->value(), values[fdec->index()]);
  }
}

void CheckOneIndexOps(absl::Span<const char> mem,
                      absl::Span<const uint64_t> values) {
  auto dec = Decoder::Make(mem);
  EXPECT_OK(dec);
  EXPECT_EQ(dec->size(), values.size());
  for (size_t i = 0; i != values.size(); ++i) {
    EXPECT_EQ(*dec->IteratorAt(i), values[i]);
  }
  auto fdec = ForwardDecoder::Make(mem);
  EXPECT_OK(fdec);
  EXPECT_EQ(fdec->size(), values.size());
  for (size_t i = 0; i != values.size(); ++i) {
    fdec->AdvanceToIndex(i);
    EXPECT_EQ(fdec->value(), values[i]);
  }
  if (!values.empty()) fdec->Next();
  EXPECT_TRUE(fdec->done());
}

void CheckZeroIndexOps(absl::Span<const char> mem,
                       absl::Span<const uint64_t> values) {
  auto dec = Decoder::Make(mem);
  EXPECT_OK(dec);
  EXPECT_EQ(dec->size(), values.size());
  size_t i = 0;
  for (auto x : *dec) {
    EXPECT_LE(i, values.size());
    EXPECT_EQ(values[i], x);
    if (i > 0 && values[i - 1] < values[i]) {
      uint64_t diff = values[i] - values[i - 1];
      // Get a pseudorandom value in between 'values[i-1]' and 'values[i]'
      uint64_t mid_val = values[i - 1] + (i * 0x9ddfea08eb382d69) % diff;
      EXPECT_EQ(dec->UpperBound(mid_val), i);
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
}

void Roundtrip(std::vector<uint64_t> values,
               Options::Index indexes = Options::ALL) {
  absl::c_sort(values);

  Encoder2 enc(indexes);
  EXPECT_TRUE(enc.Append(values));

  const auto mem = enc.Finalize();
  EXPECT_OK(GetInfo(mem));

  const auto opts = Options::Decode(mem);
  EXPECT_OK(opts);

  TestIteration(mem, values);
  auto supports_index_ops = [&](auto index) {
    return opts->bits() <= Options::kMaxBitsNoIndex || opts->has_index(index);
  };
  if (supports_index_ops(Options::ONES)) CheckOneIndexOps(mem, values);
  if (supports_index_ops(Options::ZEROS)) CheckZeroIndexOps(mem, values);
}
FUZZ_TEST(CodingTest, Roundtrip)
    .WithDomains(VectorOf(Arbitrary<uint64_t>()),
                 BitFlagCombinationOf({Options::ONES, Options::ZEROS}));

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

  EXPECT_OK(GetInfo(mem));

  auto dec = Decoder::Make(mem);
  EXPECT_OK(dec);

  for (uint64_t i = 1; i < N; ++i) EXPECT_EQ(0, *dec->IteratorAt(i - 1));
  EXPECT_EQ(M, *dec->IteratorAt(N - 1));
}

TEST(CodingTest, DISABLED_MaxEncoding00000000000111111111____1) {
  constexpr auto N = std::numeric_limits<uint32_t>::max();
  constexpr auto M = uint64_t{std::numeric_limits<uint32_t>::max()};

  Encoder2 enc;
  for (uint64_t i = 0; i < N; ++i) EXPECT_TRUE(enc.Append(M));
  const auto mem = enc.Finalize();

  EXPECT_OK(GetInfo(mem));

  auto dec = Decoder::Make(mem);
  EXPECT_OK(dec);
  for (uint64_t i = 0; i < N; ++i) EXPECT_EQ(M, *dec->IteratorAt(i));
}

TEST(Finalize, Equivalent) {
  Encoder2 e1;
  Encoder2 e2;
  for (int n = 0; n < 200; ++n) {
    ASSERT_TRUE(e1.Append(n));
    ASSERT_TRUE(e2.Append(n));
  }

  absl::string_view e1_encoded = e1.Finalize();
  ASSERT_OK_AND_ASSIGN(std::string e2_encoded, e2.FinalizeToStr());
  ASSERT_EQ(e1_encoded, e2_encoded);
}

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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
  std::vector<char> encoded(Encoder::MaxEncodingBytes(opts));
  while (state->KeepRunningBatch(size)) {
    Encoder enc(opts, absl::MakeSpan(encoded));
    for (auto v : dec) enc.Append(v);
    CHECK(enc.Finalize().ok());
    DoNotOptimize(enc);
  }
  // Get stats.
  Encoder enc(opts, absl::MakeSpan(encoded));
  for (auto v : dec) enc.Append(v);
  CHECK(enc.Finalize().ok());
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = ForwardDecoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = ForwardDecoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = ForwardDecoder::Make(State::get()->GetEncoded(opts)).ValueOrDie();
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
  auto dec = Decoder::Make(*encoded).ValueOrDie();

  for (auto s : state) DoNotOptimize(dec.IteratorAt(1));
}
BENCHMARK(WorstCase);

}  // namespace bm
}  // namespace
}  // namespace testing
}  // namespace elias_fano
