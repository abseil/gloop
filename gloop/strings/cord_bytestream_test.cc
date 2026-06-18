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

#include "gloop/strings/cord_bytestream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

#include "absl/flags/declare.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_DECLARE_FLAG(int, copy_sharing_threshold);

namespace {

using RandomEngine = std::mt19937_64;

// The number of iterations to run randomized tests for.
static constexpr int kNumTestIters = 3000;

static int32_t GenerateSkewedRandom(RandomEngine* rng, int max_log) {
  const int32_t base =
      absl::Uniform(absl::IntervalClosedClosed, *rng, 0, max_log);
  return absl::Uniform<int32_t>(*rng, 0, base);
}

static std::string RandomLowercaseString(RandomEngine* rng, size_t length) {
  std::string result(length, '\0');
  std::uniform_int_distribution<int> chars('a', 'z');
  std::generate(result.begin(), result.end(),
                [&]() { return static_cast<char>(chars(*rng)); });
  return result;
}

static std::string RandomLowercaseString(RandomEngine* rng) {
  int length;
  std::bernoulli_distribution one_in_1k(0.001);
  std::bernoulli_distribution one_in_10k(0.0001);
  // With low probability, make a large fragment
  if (one_in_10k(*rng)) {
    length = absl::Uniform(*rng, 0, 1048576);
  } else if (one_in_1k(*rng)) {
    length = absl::Uniform(*rng, 0, 10000);
  } else {
    length = GenerateSkewedRandom(rng, 10);
  }
  return RandomLowercaseString(rng, length);
}

static void AddExternalMemory(absl::string_view s, absl::Cord* dst) {
  std::string* str = new std::string(s);
  dst->Append(absl::MakeCordFromExternal(
      *str, [str](absl::string_view data) { delete str; }));
}

// Make a Cord with some number of fragments.  Return the size (in bytes)
// of the smallest fragment.
static size_t AppendWithFragments(const std::string& s, RandomEngine* rng,
                                  absl::Cord* cord) {
  size_t j = 0;
  const size_t max_size = s.size() / 5;  // Make approx. 10 fragments
  size_t min_size = max_size;            // size of smallest fragment
  while (j < s.size()) {
    size_t N = 1 + absl::Uniform<size_t>(*rng, 0, max_size);
    if (N > (s.size() - j)) {
      N = s.size() - j;
    }
    if (N < min_size) {
      min_size = N;
    }

    std::bernoulli_distribution coin_flip(0.5);
    if (coin_flip(*rng)) {
      // Grow by adding an external-memory.
      AddExternalMemory(absl::string_view(s.data() + j, N), cord);
    } else {
      cord->Append(absl::string_view(s.data() + j, N));
    }
    j += N;
  }
  return min_size;
}

// Construct a huge cord with the specified valid prefix.
static absl::Cord MakeHuge(absl::string_view prefix) {
  absl::Cord cord;
  if (sizeof(size_t) > 4) {
    // In 64-bit binaries, test 64-bit Cord support.
    const size_t size =
        static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 314;
    cord.Append(absl::MakeCordFromExternal(
        absl::string_view(prefix.data(), size), [](absl::string_view) {}));
  } else {
    // Cords are limited to 32-bit lengths in 32-bit binaries.  The following
    // tests check for use of "signed int" to represent Cord length/offset.
    // However absl::string_view does not allow lengths >= (1u<<31), so we need
    // to append in two parts;
    const size_t s1 = (1u << 31) - 1;
    // For shorter cord, `Append` copies the data rather than allocating a new
    // node. The threshold is currently set to 511, so `s2` needs to be bigger
    // to not trigger the copy.
    const size_t s2 = 600;
    cord.Append(absl::MakeCordFromExternal(absl::string_view(prefix.data(), s1),
                                           [](absl::string_view) {}));
    cord.Append(absl::MakeCordFromExternal(absl::string_view("", s2),
                                           [](absl::string_view) {}));
  }
  return cord;
}

class SinkAdapator {
 public:
  virtual ~SinkAdapator() {}
  virtual size_t Length() const = 0;
  virtual strings::ByteSink* Sink() = 0;
  virtual void Clear() = 0;
  virtual void CheckContents(const std::string& expected) const = 0;
};

// Test CopyTo to supplied sink.
static void RandomCopyToTest(SinkAdapator* sink) {
  // This test takes a long time so reduce iterations.
  const int iters = std::max<int>(1, kNumTestIters / 10);
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  for (int i = 0; i < iters; i++) {
    if ((i % 1000) == 0) {
      fprintf(stderr, "... checked %8d of %8d CopyTo calls\n", i, iters);
    }

    // Make a Cord that consists of a bunch of fragments, including
    // external ones.
    const size_t length = absl::Uniform<size_t>(rng, 0, 10000);
    const std::string input = RandomLowercaseString(&rng, length);
    absl::Cord from;
    AppendWithFragments(input, &rng, &from);
    strings::CordReader reader(from);
    sink->Clear();
    while (reader.Available() > 0) {
      ASSERT_EQ(length, reader.Available() + sink->Length());
      std::bernoulli_distribution coin_flip(0.5);
      if (coin_flip(rng)) {
        reader.Peek();  // Just to disturb copying state
      }
      size_t n = absl::Uniform<size_t>(rng, 0, length / 2);
      if (n > reader.Available()) {
        n = reader.Available();
      }
      if (n < 1) {
        n = 1;
      }
      reader.CopyTo(sink->Sink(), n);
    }
    ASSERT_EQ(length, sink->Length());
    sink->CheckContents(input);
  }
}

TEST(CordByteStream, CopyReaderRandomToStringByteSink) {
  class StringSink : public SinkAdapator {
   private:
    std::string str_;
    strings::StringByteSink sink_;

   public:
    StringSink() : sink_(&str_) {}
    size_t Length() const override { return str_.size(); }
    strings::ByteSink* Sink() override { return &sink_; }
    void Clear() override { str_.clear(); }
    void CheckContents(const std::string& expected) const override {
      EXPECT_EQ(expected, str_);
    }
  };
  StringSink sink;
  RandomCopyToTest(&sink);
}

TEST(CordByteStream, CopyReaderRandomToCordByteSink) {
  class CordSink : public SinkAdapator {
   private:
    absl::Cord cord_;
    strings::CordByteSink sink_;

   public:
    CordSink() : sink_(&cord_) {}
    size_t Length() const override { return cord_.size(); }
    strings::ByteSink* Sink() override { return &sink_; }
    void Clear() override { cord_.Clear(); }
    void CheckContents(const std::string& expected) const override {
      EXPECT_EQ(expected, std::string(cord_));
    }
  };
  CordSink sink;
  RandomCopyToTest(&sink);
}

static void TestReader(RandomEngine* rng, int iters) {
  printf("Randomized reader tests\n");
  for (int i = 0; i < iters; i++) {
    const std::string s = RandomLowercaseString(rng);
    VLOG(1) << "----- Input: " << s.size();
    // Make a cord adding random-sized fragments at a time
    absl::Cord b;
    AppendWithFragments(s, rng, &b);
    size_t expected_offset = absl::Uniform<size_t>(*rng, 0, s.size());
    VLOG(1) << "SkipTo: " << expected_offset;
    strings::CordReader r(b);
    r.Skip(expected_offset);
    int ops = 0;
    while (ops < 10 && expected_offset < s.size()) {
      ops++;
      VLOG(1) << "@ " << expected_offset;
      ASSERT_EQ(r.Position(), expected_offset);
      ASSERT_EQ(r.Available(), s.size() - expected_offset);
      const int op = absl::Uniform(*rng, 0, 100);
      if (op <= 15) {
        r.Reset(b);
        expected_offset = 0;
      } else if (op <= 50) {
        size_t N = std::min<size_t>(absl::Uniform<size_t>(*rng, 0, 100),
                                    r.Available());
        VLOG(1) << "Read: " << N;
        absl::FixedArray<char> data(N);
        r.ReadN(N, data.data());
        ASSERT_EQ(absl::string_view(s.data() + expected_offset, N),
                  absl::string_view(data.data(), N));
        expected_offset += N;
      } else if (op <= 60) {
        // Run ReadCord
        size_t N = absl::Uniform<size_t>(*rng, 0, r.Available());
        absl::Cord dest = r.ReadCord(N);
        VLOG(1) << "ReadCord: " << N;
        ASSERT_EQ(absl::Cord(absl::string_view(s.data() + expected_offset, N)),
                  dest);
        expected_offset += N;
      } else if (op <= 70) {
        // Run CopyTo
        absl::Cord dest;
        size_t N = absl::Uniform<size_t>(*rng, 0, r.Available());
        strings::CordByteSink sink(&dest);
        r.CopyTo(&sink, N);
        VLOG(1) << "CopyTo: " << N;
        ASSERT_EQ(absl::Cord(absl::string_view(s.data() + expected_offset, N)),
                  dest);
        expected_offset += N;
      } else if (op <= 80) {
        // Read as large a fragment as CordReader is willing to return
        absl::string_view fragment;
        ASSERT_TRUE(r.ReadFragment(&fragment));
        VLOG(1) << "ReadFragment: " << fragment.size();
        ASSERT_EQ(
            absl::string_view(s.data() + expected_offset, fragment.size()),
            fragment);
        expected_offset += fragment.size();
      } else if (op <= 95) {
        // Peek at the next fragment
        absl::string_view fragment;
        ASSERT_TRUE(r.PeekFragment(&fragment));
        VLOG(1) << "PeekFragment: " << fragment.size();
        ASSERT_EQ(
            absl::string_view(s.data() + expected_offset, fragment.size()),
            fragment);
        ASSERT_EQ(r.Peek(), fragment);
      } else {
        // Randomly skip
        const size_t skip = std::min<size_t>(
            absl::Uniform<size_t>(*rng, 0, 100), r.Available());
        VLOG(1) << "Skip: " << skip;
        r.Skip(skip);
        expected_offset += skip;
      }
    }

    if ((i + 1) % 10000 == 0) {
      printf("... checked %d of %d strings\n", (i + 1), iters);
    }
  }
}

TEST(CordByteStream, CordReaderEmptyReader) {
  strings::CordReader reader;

  EXPECT_TRUE(reader.done());
  EXPECT_EQ(reader.Position(), 0);
  EXPECT_EQ(reader.Available(), 0);

  absl::string_view fragment;
  EXPECT_FALSE(reader.ReadFragment(&fragment));
  EXPECT_TRUE(fragment.empty());

  EXPECT_FALSE(reader.PeekFragment(&fragment));
  EXPECT_TRUE(fragment.empty());

  EXPECT_TRUE(reader.Peek().empty());

  uint32_t uint32_val = 0;
  EXPECT_FALSE(reader.Read32(&uint32_val));
  EXPECT_EQ(uint32_val, 0);

  uint64_t uint64_val = 0;
  EXPECT_FALSE(reader.Read64(&uint64_val));
  EXPECT_EQ(uint64_val, 0);
}

TEST(CordByteStream, CordReaderResetEmptyReaderToNonEmpty) {
  strings::CordReader reader;
  absl::Cord non_empty("non empty cord");
  reader.Reset(non_empty);

  EXPECT_FALSE(reader.done());
  EXPECT_EQ(reader.Available(), 14);
}

TEST(CordByteStream, CordReaderResetNonEmptyReaderToEmpty) {
  absl::Cord non_empty("non empty cord");
  strings::CordReader reader(non_empty);

  reader.Reset();
  EXPECT_TRUE(reader.done());
  EXPECT_EQ(reader.Available(), 0);
}

TEST(CordByteStream,
     CordReaderResetReaderToCordMidIterationAndReadFragmentAtEnd) {
  RandomEngine rng;
  std::string s = RandomLowercaseString(&rng, 1 << 16);
  absl::Cord cord(s.substr(1 << 15, 1 << 15));
  cord.Prepend(s.substr(0, 1 << 15));
  strings::CordReader reader(cord);
  reader.Skip(1 << 13);
  reader.Reset(cord);
  EXPECT_EQ(reader.ReadCord(1 << 15), s.substr(0, 1 << 15));
  size_t length = 1 << 15;
  absl::string_view frag;
  while (reader.ReadFragment(&frag)) {
    length += frag.length();
  }
  EXPECT_EQ(length, s.length());
}

TEST(CordByteStream, CordReaderReadCordInFullAndReadFragmentBeyondEnd) {
  RandomEngine rng;
  std::string s = RandomLowercaseString(&rng, 1 << 16);
  absl::Cord cord(s.substr(1 << 15, 1 << 15));
  cord.Prepend(s.substr(0, 1 << 15));
  strings::CordReader reader(cord);
  reader.Skip(1 << 13);
  reader.Reset(cord);
  EXPECT_EQ(reader.ReadCord(1 << 16), s);
  absl::string_view frag;
  EXPECT_FALSE(reader.ReadFragment(&frag));
}

// Randomized tests are at bottom so it runs last
TEST(CordByteStream, RandomizedReader) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  TestReader(&rng, kNumTestIters);
}

// Tests for CordReader::CopyToWithSharing(strings::ByteSink* sink, size_t n)
class MockByteSink : public strings::ByteSink {
 public:
  MOCK_METHOD(void, Append, (const char*, size_t), (override));
  MOCK_METHOD(size_t, MinAppendExternalMemoryLength, (), (const, override));
  MOCK_METHOD(void, AppendExternalMemory,
              (absl::string_view, void*, void (*)(void*)), (override));
};

TEST(CordByteStream, CopyToFlatWithSharing) {
  using ::testing::_;
  using ::testing::SaveArg;
  std::string data(absl::GetFlag(FLAGS_copy_sharing_threshold), 'a');
  absl::Cord cord(data);
  MockByteSink sink;
  strings::CordReader reader(cord);

  // Captured arguments for ByteSink::AppendExternalMemory.
  absl::string_view external_memory;
  void (*external_memory_releaser)(void*) = nullptr;
  void* external_memory_arg = nullptr;

  EXPECT_CALL(sink, Append(_, _)).Times(0);
  EXPECT_CALL(sink, MinAppendExternalMemoryLength())
      .WillRepeatedly(
          testing::Return(absl::GetFlag(FLAGS_copy_sharing_threshold)));
  EXPECT_CALL(sink, AppendExternalMemory(_, _, testing::NotNull()))
      .WillOnce(testing::DoAll(SaveArg<0>(&external_memory),
                               SaveArg<1>(&external_memory_arg),
                               SaveArg<2>(&external_memory_releaser)));

  reader.CopyTo(&sink, cord.size());

  EXPECT_EQ(external_memory, data);
  if (external_memory_releaser) {
    external_memory_releaser(external_memory_arg);
  }
}

TEST(CordByteStream, ByteSinkSource) {
  absl::Cord c("foo");

  // Test Cord as ByteSink
  strings::CordByteSink sink(&c);
  sink.Append("bar", 3);
  ASSERT_EQ(c, absl::Cord("foobar"));

  // Test CordReader as ByteSource
  strings::CordReader source(c);
  ASSERT_EQ(source.Available(), 6);
  ASSERT_EQ(source.Peek(), "foobar");
  source.Skip(3);
  ASSERT_EQ(source.Available(), 3);
  ASSERT_EQ(source.Peek(), "bar");
}

TEST(CordByteStream, ByteSinkSourceExternal) {
  for (int len = 0; len <= 8192; len += 1024) {
    std::string buf(len, 't');
    absl::Cord c1(buf);

    // Use CordByteSink::AppendExternalMemory to create c2.
    absl::Cord c2;
    strings::CordByteSink sink(&c2);
    bool released = false;
    sink.AppendExternalMemory(buf, &released, [](void* arg) {
      bool* rel = static_cast<bool*>(arg);
      ASSERT_TRUE(!*rel);
      *rel = true;
    });

    ASSERT_EQ(c1, c2);

    if (len > 0) {
      // Verify that memory is shared.
      auto fragment = c2.TryFlat();
      ASSERT_TRUE(fragment);
      ASSERT_EQ(&buf[0], fragment->data());
    }

    c2 = absl::Cord();
    ASSERT_TRUE(released);
  }
}

TEST(CordByteStream, CopyToLargeFlatSubstringWithSharing) {
  using ::testing::_;
  using ::testing::SaveArg;
  std::string data(1 + absl::GetFlag(FLAGS_copy_sharing_threshold) + 1, 'a');
  data[1 + absl::GetFlag(FLAGS_copy_sharing_threshold) / 2] = 'b';
  absl::Cord cord(data);
  MockByteSink sink;
  strings::CordReader reader(cord);
  reader.Skip(1);

  // Captured arguments for ByteSink::AppendExternalMemory.
  absl::string_view external_memory;
  void (*external_memory_releaser)(void*) = nullptr;
  void* external_memory_arg = nullptr;

  EXPECT_CALL(sink, Append(_, _)).Times(0);
  EXPECT_CALL(sink, MinAppendExternalMemoryLength())
      .WillRepeatedly(
          testing::Return(absl::GetFlag(FLAGS_copy_sharing_threshold)));
  EXPECT_CALL(sink, AppendExternalMemory(_, _, testing::NotNull()))
      .WillOnce(testing::DoAll(SaveArg<0>(&external_memory),
                               SaveArg<1>(&external_memory_arg),
                               SaveArg<2>(&external_memory_releaser)));

  reader.CopyTo(&sink, cord.size() - 2);

  EXPECT_EQ(external_memory,
            absl::string_view(data).substr(1, data.size() - 2));
  if (external_memory_releaser) {
    external_memory_releaser(external_memory_arg);
  }
}

TEST(CordByteStream, CordReaderRead64) {
  uint64_t expected{23589023859023523};
  char buf[sizeof(expected)];
  absl::little_endian::Store64(buf, expected);
  absl::string_view data(buf, sizeof(buf));
  for (int i = 0; i < 2; i++) {
    absl::Cord cord;
    if (i == 0) {
      cord.Append(data);
    } else {
      AddExternalMemory(data, &cord);
    }
    strings::CordReader reader(cord);
    uint64_t actual = 0;
    ASSERT_TRUE(reader.Read64(&actual));
    EXPECT_EQ(expected, actual);
    ASSERT_FALSE(reader.Read64(&actual));
  }
}

TEST(CordByteStream, CordReaderSkipBeyondEnd) {
  absl::Cord one_char("a");
  strings::CordReader one_char_reader(one_char);
  strings::CordReader two_char_reader(one_char);
  absl::Cord empty;
  strings::CordReader empty_reader(empty);

  one_char_reader.Skip(1);
#ifdef NDEBUG
  one_char_reader.Skip(1);
  two_char_reader.Skip(2);
  empty_reader.Skip(1);
#else
  EXPECT_DEATH_IF_SUPPORTED(one_char_reader.Skip(1), ".*");
  EXPECT_DEATH_IF_SUPPORTED(two_char_reader.Skip(2), ".*");
  EXPECT_DEATH_IF_SUPPORTED(empty_reader.Skip(1), ".*");
#endif
}

static absl::Cord CopyViaReader(const std::string& src, size_t offset,
                                size_t n) {
  absl::Cord src_cord(src);
  strings::CordReader reader(src_cord);
  reader.Skip(offset);
  absl::Cord dst;
  strings::CordByteSink sink(&dst);
  reader.CopyTo(&sink, n);
  EXPECT_EQ(offset + n, reader.Position());
  EXPECT_EQ(src.size() - reader.Position(), reader.Available());
  return dst;
}

TEST(CordByteStream, CopyCordToCordEmpty) {
  EXPECT_EQ(absl::Cord(), CopyViaReader("", 0, 0));
  EXPECT_EQ(absl::Cord(), CopyViaReader("hello", 0, 0));
  EXPECT_EQ(absl::Cord(), CopyViaReader("hello", 5, 0));
}

TEST(CordByteStream, CopyCordToCordSmall) {
  absl::SetFlag(&FLAGS_copy_sharing_threshold, 0);
  EXPECT_EQ(absl::Cord("hello"), CopyViaReader("hello", 0, 5));
  EXPECT_EQ(absl::Cord("ello"), CopyViaReader("hello", 1, 4));
  EXPECT_EQ(absl::Cord("ell"), CopyViaReader("hello", 1, 3));

  absl::SetFlag(&FLAGS_copy_sharing_threshold, 1000);
  EXPECT_EQ(absl::Cord("hello"), CopyViaReader("hello", 0, 5));
  EXPECT_EQ(absl::Cord("ello"), CopyViaReader("hello", 1, 4));
  EXPECT_EQ(absl::Cord("ell"), CopyViaReader("hello", 1, 3));
}

static std::string BIG(int n) {
  std::string result;
  int k = 0;
  while (result.size() < n) {
    absl::StrAppend(&result, k, " ");
    ++k;
  }
  result.resize(n);
  return result;
}

TEST(CordByteStream, CopyCordToCordBig) {
  std::string big = BIG(8192);
  std::string sub(big, 100, 8192 - 300);

  EXPECT_EQ(absl::Cord(big), CopyViaReader(big, 0, big.size()));
  EXPECT_EQ(absl::Cord(sub), CopyViaReader(big, 100, sub.size()));
}

TEST(CordByteStream, CopyCordToCordRandom) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  int remaining = kNumTestIters;
  while (remaining > 0) {
    std::string expected = BIG(absl::Uniform(rng, 0, 16328) + 1);
    absl::Cord source(expected);
    strings::CordReader reader(source);
    size_t offset = 0;
    VLOG(1) << "Source size: " << expected.size();

    while (reader.Available() > 0) {
      size_t n = absl::Uniform<size_t>(rng, 0, reader.Available()) + 1;
      reader.Skip(n);
      offset += n;

      size_t m = absl::Uniform<size_t>(rng, 0, reader.Available());
      VLOG(1) << "Skip " << n << " bytes; read " << m << " bytes";
      absl::Cord dst;
      strings::CordByteSink sink(&dst);
      reader.CopyTo(&sink, m);
      ASSERT_EQ(dst,
                absl::Cord(absl::string_view(expected.data() + offset, m)));
      --remaining;
      offset += m;
    }
  }
}

TEST(CordByteStream, ReaderCopyToFromExternalToStringByteSink) {
  static const size_t kLen = 5000;
  ASSERT_EQ(kLen % 2, 0);

  char* data = new char[kLen];
  absl::Cord from;
  from.Append(absl::MakeCordFromExternal(absl::string_view(data, kLen),
                                         [data]() { delete[] data; }));

  std::string to;
  strings::StringByteSink sink(&to);

  strings::CordReader reader(from);
  EXPECT_EQ(kLen, reader.Available());
  reader.CopyTo(&sink, kLen / 2);
  EXPECT_EQ(kLen / 2, reader.Available());
  reader.CopyTo(&sink, kLen / 2);
  EXPECT_EQ(0, reader.Available());

  EXPECT_EQ(kLen, to.size());
}

// Tests for CordReader::CopyTo(strings::ByteSink* sink, size_t n).

TEST(CordByteStream, CopyCordToStringByteSinkFlat) {
  absl::Cord cord("foo bar");
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.CopyTo(&sink, cord.size());

  EXPECT_EQ("foo bar", str);
}

TEST(CordByteStream, CopyCordToStringByteSinkFlatPrefix) {
  absl::Cord cord("foo bar");
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.CopyTo(&sink, 3);

  EXPECT_EQ("foo", str);
}

TEST(CordByteStream, CopyCordToStringByteSinkFlatSuffix) {
  absl::Cord cord("foo bar");
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.Skip(4);
  reader.CopyTo(&sink, 3);

  EXPECT_EQ("bar", str);
}

TEST(CordByteStream, CopyExternalToStringByteSinkExternal) {
  absl::Cord cord;
  AddExternalMemory("foo bar", &cord);
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.CopyTo(&sink, cord.size());

  EXPECT_EQ("foo bar", str);
}

TEST(CordByteStream, CopyExternalToStringByteSinkExternalPrefix) {
  absl::Cord cord;
  AddExternalMemory("foo bar", &cord);
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.CopyTo(&sink, 3);

  EXPECT_EQ("foo", str);
}

TEST(CordByteStream, CopyExternalToStringByteSinkExternalSuffix) {
  absl::Cord cord;
  AddExternalMemory("foo bar", &cord);
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.Skip(4);
  reader.CopyTo(&sink, 3);

  EXPECT_EQ("bar", str);
}

TEST(CordByteStream, CopyExternalToStringByteSinkExternalSubstring) {
  absl::Cord cord;
  AddExternalMemory("foo bar", &cord);
  cord.RemovePrefix(2);
  cord.RemoveSuffix(2);
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.CopyTo(&sink, cord.size());

  EXPECT_EQ("o b", str);
}

TEST(CordByteStream, CopyExternalToStringByteSinkMultipleBlocks) {
  absl::Cord cord("foo");
  AddExternalMemory("bar", &cord);
  cord.Append("baz");
  std::string str;
  strings::StringByteSink sink(&str);
  strings::CordReader reader(cord);
  reader.CopyTo(&sink, cord.size());

  EXPECT_EQ("foobarbaz", str);
}

// Appends a small string after a large cord, and exercise various methods to
// ensure they work in the presence of large cords.
TEST(CordByteStream, HugeCordConcatAndReader) {
  absl::Cord first = MakeHuge("huge first cord");
  size_t first_size = first.size();
  std::string second("second part");
  size_t second_size = second.size();

  absl::Cord cord;
  cord.Append(first);
  cord.Append(second);

  EXPECT_EQ('h', cord[0]);
  EXPECT_EQ('s', cord[first_size]);
  EXPECT_EQ(first_size + second_size, cord.size());
  // Cord::InlineRep::AppendArray does extra allocation so that future appends
  // are fast.  To avoid making the test dependent on its growth policy, we
  // don't check the exact size of "cord"; we just test that it's large.
  EXPECT_LE(first_size + second_size, cord.EstimatedMemoryUsage());

  // Test CordReader for large concatenated cords
  strings::CordReader reader(cord);
  reader.Skip(first_size);
  EXPECT_EQ(first_size, reader.Position());
  EXPECT_EQ(second_size, reader.Available());

  std::string str;
  strings::StringByteSink sink(&str);
  reader.CopyTo(&sink, 3);
  EXPECT_EQ("sec", str);
}

// Like above, but appends a large cord after a small string.
TEST(CordByteStream, HugeCordOppositeConcatAndReader) {
  std::string first("first part");
  size_t first_size = first.size();
  absl::Cord second = MakeHuge("huge second cord");
  size_t second_size = second.size();

  absl::Cord cord;
  cord.Append(first);
  cord.Append(second);

  EXPECT_EQ('f', cord[0]);
  EXPECT_EQ('h', cord[first_size]);
  EXPECT_EQ(first_size + second_size, cord.size());
  // Cord::InlineRep::AppendArray does extra allocation so that future appends
  // are fast.  To avoid making the test dependent on its growth policy, we
  // don't check the exact size of "cord"; we just test that it's large.
  EXPECT_LE(first_size + second_size, cord.EstimatedMemoryUsage());

  // Test CordReader for >4GB concatenated cords
  strings::CordReader reader(cord);
  reader.Skip(first_size);
  EXPECT_EQ(first_size, reader.Position());
  EXPECT_EQ(second_size, reader.Available());

  std::string str;
  strings::StringByteSink sink(&str);
  reader.CopyTo(&sink, 3);
  EXPECT_EQ("hug", str);
}

}  // namespace
