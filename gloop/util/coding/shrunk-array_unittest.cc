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

#include "gloop/util/coding/shrunk-array.h"

#include <stdlib.h>

#include <cstdint>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/endian/endian.h"
#include "gtest/gtest.h"

// GenericReader: This is a pure interface for adapter classes.  We want to
// test both ShrunkArray::Reader and ShrunkArray::UnalignedLittleEndianReader
// using the same unit tests but they have slightly different APIs.

namespace {

class GenericReader {
 public:
  GenericReader() {}
  virtual ~GenericReader() {}

  virtual void Bind(const void* shrunk_array, const uint64_t decode_key[2]) = 0;
  // Unlike ShrunkArray::Reader::Bind1(), this Bind1() requires that
  // decode_key[1] exists (but it still has no effect).
  virtual void Bind1(const void* shrunk_array,
                     const uint64_t decode_key[2]) = 0;
  virtual uint64_t Get(size_t position) = 0;
  virtual uint64_t Get(const void* shrunk_array, const uint64_t decode_key[2],
                       size_t position) = 0;
  virtual uint64_t SharedGet(size_t position) const = 0;
};

// Adapter implementation shared by ShrunkArray::Reader &
// ShrunkArray::UnalignedLittleEndianReaderAdapter.

template <class Reader, bool unaligned_little_endian, typename PointerType>
class ReaderAdapterImpl : public GenericReader {
 public:
  // Take ownership.
  explicit ReaderAdapterImpl(Reader* reader) : reader_(reader) {}

  // This type is neither copyable nor movable.
  ReaderAdapterImpl(const ReaderAdapterImpl&) = delete;
  ReaderAdapterImpl& operator=(const ReaderAdapterImpl&) = delete;
  ~ReaderAdapterImpl() override { delete reader_; }
  void Bind(const void* shrunk_array, const uint64_t decode_key[2]) override {
    reader_->Bind(reinterpret_cast<PointerType>(shrunk_array),
                  SetupKey(decode_key));
  }
  void Bind1(const void* shrunk_array, const uint64_t decode_key[2]) override {
    reader_->Bind1(reinterpret_cast<PointerType>(shrunk_array),
                   SetupKey(decode_key));
  }
  uint64_t Get(size_t position) override { return reader_->Get(position); }
  uint64_t Get(const void* shrunk_array, const uint64_t decode_key[2],
               size_t position) override {
    return reader_->Get(reinterpret_cast<PointerType>(shrunk_array),
                        SetupKey(decode_key), position);
  }
  uint64_t SharedGet(size_t position) const override {
    return reader_->SharedGet(position);
    // If we call Get() here instead, we can see the SharedGet test fail.
  }

 private:
  // Returns decode_key or a misaligned byte-swapped copy in buffer_.
  PointerType SetupKey(const uint64_t decode_key[2]) {
    if (unaligned_little_endian) {
      char* data = reinterpret_cast<char*>(buffer_) + 1;
      LittleEndian::Store64(data, decode_key[0]);
      LittleEndian::Store64(data + sizeof(uint64_t), decode_key[1]);
      return reinterpret_cast<PointerType>(data);
    } else {
      return reinterpret_cast<PointerType>(decode_key);
    }
  }

  Reader* reader_;
  uint64_t buffer_[3];
};

typedef ReaderAdapterImpl<ShrunkArray::Reader, false, const uint64_t*>
    ReaderAdapter;
typedef ReaderAdapterImpl<ShrunkArray::UnalignedLittleEndianReader, true,
                          const void*>
    UnalignedLittleEndianReaderAdapter;

class ShrunkArrayTest : public testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    unaligned_little_endian_ = GetParam();
    shrunk_array_ = nullptr;
    reader_ = NewReader();
  }

  void TearDown() override {
    Delete(shrunk_array_);
    delete reader_;
  }

  GenericReader* NewReader() {
    if (unaligned_little_endian_) {
      return new UnalignedLittleEndianReaderAdapter(
          ShrunkArray::UnalignedLittleEndianReader::New());
    } else {
      return new ReaderAdapter(ShrunkArray::Reader::New());
    }
  }

  const void* Copy(absl::Span<const uint64_t> uint64_vector) {
    if (!unaligned_little_endian_) {
      return ShrunkArray::Copy(uint64_vector);
    }
    uint64_t* buffer = new uint64_t[uint64_vector.size() + 1];
    char* data = reinterpret_cast<char*>(buffer) + 1;
    for (size_t i = 0; i < uint64_vector.size(); ++i) {
      LittleEndian::Store64(data + i * sizeof(uint64_t), uint64_vector[i]);
    }
    return data;
  }

  void Delete(const void* copied_vector) {
    if (copied_vector != nullptr) {
      const char* data = reinterpret_cast<const char*>(copied_vector);
      if (unaligned_little_endian_) --data;
      delete[] reinterpret_cast<const uint64_t*>(data);
    }
  }

  void CopyAndBind(int out_size) {
    EXPECT_EQ(out_size, shrunk_vector_.size());
    Delete(shrunk_array_);
    shrunk_array_ = Copy(shrunk_vector_);
    reader_->Bind(shrunk_array_, decode_key_);
  }

  // When verifying the shrunk array, we read it both backwards and
  // forwards to exercise both the serial and non-serial code paths.

  void ShrinkAndVerify(const uint64_t* in, int in_size, int out_size) {
    ShrunkArray::Write(in, in_size, 3, decode_key_, &shrunk_vector_);
    CopyAndBind(out_size);
    for (int i = -in_size + 1; i < in_size; ++i) {
      EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
    }
  }

  void ShrinkAndVerify(const uint32_t* in, int in_size, int out_size) {
    ShrunkArray::Write(in, in_size, 3, decode_key_, &shrunk_vector_);
    CopyAndBind(out_size);
    for (int i = -in_size + 1; i < in_size; ++i) {
      EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
    }
  }

  void ShrinkAndVerify(const uint16_t* in, int in_size, int out_size) {
    ShrunkArray::Write(in, in_size, 3, decode_key_, &shrunk_vector_);
    CopyAndBind(out_size);
    for (int i = -in_size + 1; i < in_size; ++i) {
      EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
    }
  }

  void ShrinkAndVerify(const uint8_t* in, int in_size, int out_size) {
    ShrunkArray::Write(in, in_size, 3, decode_key_, &shrunk_vector_);
    CopyAndBind(out_size);
    for (int i = -in_size + 1; i < in_size; ++i) {
      EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
    }
  }

  bool unaligned_little_endian_;
  uint64_t decode_key_[2];
  std::vector<uint64_t> shrunk_vector_;
  const void* shrunk_array_;
  GenericReader* reader_;
};

TEST_P(ShrunkArrayTest, Empty) {
  const int in_size = 0, out_size = 0;
  const uint64_t* const in = nullptr;
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, OneZero) {
  const int in_size = 1, out_size = 0;
  const uint8_t in[in_size] = {0};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, OneNonzero) {
  const int in_size = 1, out_size = 1;
  uint32_t in[in_size] = {99999};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, MaxSpan) {
  const int in_size = 2, out_size = 2;
  uint64_t in[in_size] = {0, 0xFFFFFFFFFFFFFFFFll};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, TwoPositionsTwoWidths) {
  const int in_size = 2, out_size = 1;
  uint32_t in[in_size] = {3999777000u, 5};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, TwoPositionsOneWidth) {
  const int in_size = 2, out_size = 1;
  uint32_t in[in_size] = {999777, 999555};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, TwoPositionsOneValue) {
  const int in_size = 2, out_size = 1;
  uint16_t in[in_size] = {9999, 9999};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, NoRepetitionOneWidth) {
  const int in_size = 30, out_size = 7;
  uint16_t in[30] = {5000, 5001, 5002, 5003, 5004, 5005, 5006, 5007,
                     5008, 5009, 5010, 5011, 5012, 5013, 5014, 5015,
                     5016, 5017, 5018, 5019, 5020, 5021, 5022, 5023,
                     5024, 5025, 5026, 5027, 5028, 5029};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, NoRepetitionTwoWidths) {
  const int in_size = 30, out_size = 12;
  uint32_t in[30] = {100,       101,       102,       103,       104,
                     105,       106,       107,       108,       109,
                     110,       111,       112,       113,       114,
                     999777000, 999777001, 999777002, 999777003, 999777004,
                     999777005, 999777006, 999777007, 999777008, 999777009,
                     999777010, 999777011, 999777012, 999777013, 999777014};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, NoRepetitionFourWidths) {
  const int in_size = 32, out_size = 12;
  uint64_t in[in_size] = {0,
                          1,
                          2,
                          3,
                          4,
                          5,
                          6,
                          7,
                          1000,
                          1001,
                          1002,
                          1003,
                          1004,
                          1005,
                          1006,
                          1007,
                          999000,
                          999001,
                          999002,
                          999003,
                          999004,
                          999005,
                          999006,
                          999007,
                          999777555000ull,
                          999777555001ull,
                          999777555002ull,
                          999777555003ull,
                          999777555004ull,
                          999777555005ull,
                          999777555006ull,
                          999777555007ull};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, ManyUniqueOneRepeatedOneWidth) {
  const int in_size = 48, out_size = 14;
  uint32_t in[in_size] = {
      444000, 444001, 444002, 444003, 444004, 444005, 444006, 444007,
      444008, 444009, 444010, 444011, 444012, 444013, 444014, 444015,
      444016, 444017, 444018, 444019, 444020, 444021, 444022, 444023,
      444024, 444025, 444026, 444027, 444028, 444029, 444030, 444031,
      499999, 499999, 499999, 499999, 499999, 499999, 499999, 499999,
      499999, 499999, 499999, 499999, 499999, 499999, 499999, 499999};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, ManyUniqueFourRepeatedFourWidths) {
  const int in_size = 63, out_size = 20;
  uint64_t in[in_size] = {
      50,
      50,
      50,
      50,
      50,
      50,
      50,
      50,
      51,
      52,
      53,
      54,
      55,
      56,
      57,
      58,

      555000,
      555000,
      555000,
      555000,
      555000,
      555000,
      555000,
      555000,
      555001,
      555002,
      555003,
      555004,
      555005,
      555006,
      555007,
      555008,

      555444000ull,
      555444000ull,
      555444000ull,
      555444000ull,
      555444000ull,
      555444000ull,
      555444000ull,
      555444000ull,
      555444001ull,
      555444002ull,
      555444003ull,
      555444004ull,
      555444005ull,
      555444006ull,
      555444007ull,
      555444008ull,

      555444333000ull,
      555444333000ull,
      555444333000ull,
      555444333000ull,
      555444333000ull,
      555444333000ull,
      555444333000ull,
      555444333001ull,
      555444333002ull,
      555444333003ull,
      555444333004ull,
      555444333005ull,
      555444333006ull,
      555444333007ull,
      555444333008ull,
  };
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, NoneUniqueOneFrequentOneWidth) {
  const int in_size = 40, out_size = 9;
  uint32_t in[in_size] = {
      444000, 444001, 444002, 444003, 444004, 444005, 444006, 444007,
      444008, 444009, 444010, 444011, 444012, 444013, 444014, 444015,
      444000, 444001, 444002, 444003, 444004, 444005, 444006, 444007,
      444008, 444009, 444010, 444011, 444012, 444013, 444014, 444015,
      499999, 499999, 499999, 499999, 499999, 499999, 499999, 499999};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, NoneUniqueOneVeryFrequentOneWidth) {
  const int in_size = 56, out_size = 10;
  uint32_t in[in_size] = {
      444000, 444001, 444002, 444003, 444004, 444005, 444006, 444007,
      444008, 444009, 444010, 444011, 444012, 444013, 444014, 444015,
      444000, 444001, 444002, 444003, 444004, 444005, 444006, 444007,
      444008, 444009, 444010, 444011, 444012, 444013, 444014, 444015,
      499999, 499999, 499999, 499999, 499999, 499999, 499999, 499999,
      499999, 499999, 499999, 499999, 499999, 499999, 499999, 499999,
      499999, 499999, 499999, 499999, 499999, 499999, 499999, 499999};
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, FourFreqsFourWidthsA) {
  // Every <freq, width> pair occupies 32 positions, except when freq is
  // 128, in which case we use only the smallest width.

  // Use frequencies spread far apart: 2, 8, 32, 128.  This will cause
  // the savings from using multiple profiles to exceed the cost.
  const int in_size = 512, out_size = 72;
  uint32_t in[in_size] = {
      128,       128,       129,       129,       130,       130,
      131,       131,       132,       132,       133,       133,
      134,       134,       135,       135,       136,       136,
      137,       137,       138,       138,       139,       139,
      140,       140,       141,       141,       142,       142,
      143,       143,

      22144,     22144,     22145,     22145,     22146,     22146,
      22147,     22147,     22148,     22148,     22149,     22149,
      22150,     22150,     22151,     22151,     22152,     22152,
      22153,     22153,     22154,     22154,     22155,     22155,
      22156,     22156,     22157,     22157,     22158,     22158,
      22159,     22159,

      2777160,   2777160,   2777161,   2777161,   2777162,   2777162,
      2777163,   2777163,   2777164,   2777164,   2777165,   2777165,
      2777166,   2777166,   2777167,   2777167,   2777168,   2777168,
      2777169,   2777169,   2777170,   2777170,   2777171,   2777171,
      2777172,   2777172,   2777173,   2777173,   2777174,   2777174,
      2777175,   2777175,

      444777176, 444777176, 444777177, 444777177, 444777178, 444777178,
      444777179, 444777179, 444777180, 444777180, 444777181, 444777181,
      444777182, 444777182, 444777183, 444777183, 444777184, 444777184,
      444777185, 444777185, 444777186, 444777186, 444777187, 444777187,
      444777188, 444777188, 444777189, 444777189, 444777190, 444777190,
      444777191, 444777191,

      224,       224,       224,       224,       224,       224,
      224,       224,       225,       225,       225,       225,
      225,       225,       225,       225,       226,       226,
      226,       226,       226,       226,       226,       226,
      227,       227,       227,       227,       227,       227,
      227,       227,

      22228,     22228,     22228,     22228,     22228,     22228,
      22228,     22228,     22229,     22229,     22229,     22229,
      22229,     22229,     22229,     22229,     22230,     22230,
      22230,     22230,     22230,     22230,     22230,     22230,
      22231,     22231,     22231,     22231,     22231,     22231,
      22231,     22231,

      2777232,   2777232,   2777232,   2777232,   2777232,   2777232,
      2777232,   2777232,   2777233,   2777233,   2777233,   2777233,
      2777233,   2777233,   2777233,   2777233,   2777234,   2777234,
      2777234,   2777234,   2777234,   2777234,   2777234,   2777234,
      2777235,   2777235,   2777235,   2777235,   2777235,   2777235,
      2777235,   2777235,

      444777236, 444777236, 444777236, 444777236, 444777236, 444777236,
      444777236, 444777236, 444777237, 444777237, 444777237, 444777237,
      444777237, 444777237, 444777237, 444777237, 444777238, 444777238,
      444777238, 444777238, 444777238, 444777238, 444777238, 444777238,
      444777239, 444777239, 444777239, 444777239, 444777239, 444777239,
      444777239, 444777239,

      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,

      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,

      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,

      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,
  };
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, FourFreqsFourWidthsB) {
  // Like the previous test, but now use frequencies closer together:
  // 4, 8, 16, 32.  This will cause the cost of multiple profiles to
  // exceed the savings.
  const int in_size = 512, out_size = 66;
  uint32_t in[in_size] = {
      192,       192,       192,       192,       193,       193,
      193,       193,       194,       194,       194,       194,
      195,       195,       195,       195,       196,       196,
      196,       196,       197,       197,       197,       197,
      198,       198,       198,       198,       199,       199,
      199,       199,

      22200,     22200,     22200,     22200,     22201,     22201,
      22201,     22201,     22202,     22202,     22202,     22202,
      22203,     22203,     22203,     22203,     22204,     22204,
      22204,     22204,     22205,     22205,     22205,     22205,
      22206,     22206,     22206,     22206,     22207,     22207,
      22207,     22207,

      2777208,   2777208,   2777208,   2777208,   2777209,   2777209,
      2777209,   2777209,   2777210,   2777210,   2777210,   2777210,
      2777211,   2777211,   2777211,   2777211,   2777212,   2777212,
      2777212,   2777212,   2777213,   2777213,   2777213,   2777213,
      2777214,   2777214,   2777214,   2777214,   2777215,   2777215,
      2777215,   2777215,

      444777216, 444777216, 444777216, 444777216, 444777217, 444777217,
      444777217, 444777217, 444777218, 444777218, 444777218, 444777218,
      444777219, 444777219, 444777219, 444777219, 444777220, 444777220,
      444777220, 444777220, 444777221, 444777221, 444777221, 444777221,
      444777222, 444777222, 444777222, 444777222, 444777223, 444777223,
      444777223, 444777223,

      224,       224,       224,       224,       224,       224,
      224,       224,       225,       225,       225,       225,
      225,       225,       225,       225,       226,       226,
      226,       226,       226,       226,       226,       226,
      227,       227,       227,       227,       227,       227,
      227,       227,

      22228,     22228,     22228,     22228,     22228,     22228,
      22228,     22228,     22229,     22229,     22229,     22229,
      22229,     22229,     22229,     22229,     22230,     22230,
      22230,     22230,     22230,     22230,     22230,     22230,
      22231,     22231,     22231,     22231,     22231,     22231,
      22231,     22231,

      2777232,   2777232,   2777232,   2777232,   2777232,   2777232,
      2777232,   2777232,   2777233,   2777233,   2777233,   2777233,
      2777233,   2777233,   2777233,   2777233,   2777234,   2777234,
      2777234,   2777234,   2777234,   2777234,   2777234,   2777234,
      2777235,   2777235,   2777235,   2777235,   2777235,   2777235,
      2777235,   2777235,

      444777236, 444777236, 444777236, 444777236, 444777236, 444777236,
      444777236, 444777236, 444777237, 444777237, 444777237, 444777237,
      444777237, 444777237, 444777237, 444777237, 444777238, 444777238,
      444777238, 444777238, 444777238, 444777238, 444777238, 444777238,
      444777239, 444777239, 444777239, 444777239, 444777239, 444777239,
      444777239, 444777239,

      240,       240,       240,       240,       240,       240,
      240,       240,       240,       240,       240,       240,
      240,       240,       240,       240,       241,       241,
      241,       241,       241,       241,       241,       241,
      241,       241,       241,       241,       241,       241,
      241,       241,

      22242,     22242,     22242,     22242,     22242,     22242,
      22242,     22242,     22242,     22242,     22242,     22242,
      22242,     22242,     22242,     22242,     22243,     22243,
      22243,     22243,     22243,     22243,     22243,     22243,
      22243,     22243,     22243,     22243,     22243,     22243,
      22243,     22243,

      2777244,   2777244,   2777244,   2777244,   2777244,   2777244,
      2777244,   2777244,   2777244,   2777244,   2777244,   2777244,
      2777244,   2777244,   2777244,   2777244,   2777245,   2777245,
      2777245,   2777245,   2777245,   2777245,   2777245,   2777245,
      2777245,   2777245,   2777245,   2777245,   2777245,   2777245,
      2777245,   2777245,

      444777246, 444777246, 444777246, 444777246, 444777246, 444777246,
      444777246, 444777246, 444777246, 444777246, 444777246, 444777246,
      444777246, 444777246, 444777246, 444777246, 444777247, 444777247,
      444777247, 444777247, 444777247, 444777247, 444777247, 444777247,
      444777247, 444777247, 444777247, 444777247, 444777247, 444777247,
      444777247, 444777247,

      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,

      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,

      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,

      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251,
  };
  ShrinkAndVerify(in, in_size, out_size);
}

// The script that generated the data for the previous two tests:
/*
#!/bin/sh

awk -v freq=$1 'BEGIN {
  prefix[0] = ""
  prefix[1] = "22"
  prefix[2] = "2777"
  prefix[3] = "444777"
  v = 256 - 256/freq

  for (w = 0;  w < 4;  ++w) {
    for (i = 0;  i < 32;  ++i) {
      if (i % 8 == 0 || (w == 3 && i % 4 == 0)) printf("\n   ")
      printf(" %s%d,", prefix[w], v)
      if ((i+1) % freq == 0) ++v
    }
    print
  }
}'
*/

TEST_P(ShrunkArrayTest, ManySmallManyRepeatedManyUniqueThreeWidths) {
  const int in_size = 230, out_size = 63;
  uint32_t in[in_size] = {
      0,         1,         2,         3,         4,         5,
      6,         7,         8,         9,         10,        11,
      12,        13,        14,        15,        16,        17,
      18,        19,        20,        21,        22,        23,
      24,        25,        26,        27,        28,        29,

      0,         1,         2,         3,         4,         5,
      6,         7,         8,         9,         10,        11,
      12,        13,        14,        15,        16,        17,
      18,        19,        20,        21,        22,        23,
      24,        25,        26,        27,        28,        29,

      99000,     99001,     99002,     99003,     99004,     99005,
      99006,     99007,     99008,     99009,     99010,     99011,
      99012,     99013,     99014,     99015,     99016,     99017,
      99018,     99019,     99020,     99021,     99022,     99023,
      99024,     99025,     99026,     99027,     99028,     99029,
      99030,     99031,     99032,     99033,     99034,     99035,
      99036,     99037,     99038,     99039,     99040,     99041,
      99042,     99043,     99044,     99045,     99046,     99047,
      99048,     99049,

      999777000, 999777001, 999777002, 999777003, 999777004, 999777005,
      999777006, 999777007, 999777008, 999777009, 999777010, 999777011,
      999777012, 999777013, 999777014, 999777015, 999777016, 999777017,
      999777018, 999777019,

      77000,     77001,     77002,     77003,     77004,     77005,
      77006,     77007,     77008,     77009,     77010,     77011,
      77012,     77013,     77014,     77015,     77016,     77017,
      77018,     77019,     77020,     77021,     77022,     77023,
      77024,     77025,     77026,     77027,     77028,     77029,
      77030,     77031,     77032,     77033,     77034,     77035,
      77036,     77037,     77038,     77039,     77040,     77041,
      77042,     77043,     77044,     77045,     77046,     77047,
      77048,     77049,

      77000,     77001,     77002,     77003,     77004,     77005,
      77006,     77007,     77008,     77009,     77010,     77011,
      77012,     77013,     77014,     77015,     77016,     77017,
      77018,     77019,     77020,     77021,     77022,     77023,
      77024,     77025,     77026,     77027,     77028,     77029,
      77030,     77031,     77032,     77033,     77034,     77035,
      77036,     77037,     77038,     77039,     77040,     77041,
      77042,     77043,     77044,     77045,     77046,     77047,
      77048,     77049,
  };

  // The optimal solution would be one lexicon of 50 17-bit entries:
  //
  // bits_lex = 50*17 = 850
  // bits_main = 60*5 + 50*17 + 20*30 + 100*6 = 2350
  // (sum = 3200)
  //
  // Unfortunately the lexicon must be filled in order of frequency,
  // breaking ties in favor of smaller values, so it won't consider a
  // lexicon of just the 17-bit repeated values.  It should consider a
  // lexicon of 64 entries, of which 30 are small and 34 are 17-bit:
  //
  // bits_lex = 16*5 + 48*17 = 896
  // bits_main = 60*6 + 50*17 + 20*30 + 68*6 + 32*17 = 2762
  // (sum = 3658)
  //
  // It should also consider a lexicon containing all 80 repeated
  // values, but that's slightly worse:
  //
  // bits_lex = 20*5 + 60*17 = 1120
  // bits_main = 60*7 + 50*17 + 20*30 + 100*7 = 2570
  // (sum = 3690)
  //
  // But it turns out to be better to not use a lexicon at all:
  //
  // bits_lex = 0
  // bits_main = 32*4 + 28*5 + 50*17 + 20*30 + 100*17 = 3418
  // (sum = 3418)

  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, ManyRepeatedMostlyZeros) {
  const int in_size = 180, out_size = 15;
  uint16_t in[in_size] = {
      0, 0, 1000, 0, 0, 1001, 0, 0, 1002, 0, 0, 1003, 0, 0, 1004,
      0, 0, 1005, 0, 0, 1006, 0, 0, 1007, 0, 0, 1008, 0, 0, 1009,
      0, 0, 1010, 0, 0, 1011, 0, 0, 1012, 0, 0, 1013, 0, 0, 1014,
      0, 0, 1015, 0, 0, 1016, 0, 0, 1017, 0, 0, 1018, 0, 0, 1019,

      0, 0, 1000, 0, 0, 1001, 0, 0, 1002, 0, 0, 1003, 0, 0, 1004,
      0, 0, 1005, 0, 0, 1006, 0, 0, 1007, 0, 0, 1008, 0, 0, 1009,
      0, 0, 1010, 0, 0, 1011, 0, 0, 1012, 0, 0, 1013, 0, 0, 1014,
      0, 0, 1015, 0, 0, 1016, 0, 0, 1017, 0, 0, 1018, 0, 0, 1019,

      0, 0, 1000, 0, 0, 1001, 0, 0, 1002, 0, 0, 1003, 0, 0, 1004,
      0, 0, 1005, 0, 0, 1006, 0, 0, 1007, 0, 0, 1008, 0, 0, 1009,
      0, 0, 1010, 0, 0, 1011, 0, 0, 1012, 0, 0, 1013, 0, 0, 1014,
      0, 0, 1015, 0, 0, 1016, 0, 0, 1017, 0, 0, 1018, 0, 0, 1019,
  };
  ShrinkAndVerify(in, in_size, out_size);
}

TEST_P(ShrunkArrayTest, Rebind) {
  const int in1_size = 8, out1_size = 1;
  uint16_t in1[in1_size] = {0xFFFF, 42, 0, 999, 0, 0xFFFF, 999, 42};
  const int in2_size = 2, out2_size = 2;
  uint64_t in2[in2_size] = {0x0123456789ABCDEFll, 0xFEDCBA9876543210ll};

  std::vector<uint64_t> shrunk_vector1, shrunk_vector2;
  uint64_t decode_key1[2], decode_key2[2];

  ShrunkArray::Write(in1, in1_size, 3, decode_key1, &shrunk_vector1);
  EXPECT_EQ(out1_size, shrunk_vector1.size());
  const void* shrunk_array1 = Copy(shrunk_vector1);

  ShrunkArray::Write(in2, in2_size, 3, decode_key2, &shrunk_vector2);
  EXPECT_EQ(out2_size, shrunk_vector2.size());
  const void* shrunk_array2 = Copy(shrunk_vector2);

  reader_->Bind(shrunk_array1, decode_key1);
  for (int i = -in1_size + 1; i < in1_size; ++i) {
    EXPECT_EQ(in1[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  reader_->Bind(shrunk_array2, decode_key2);
  for (int i = -in2_size + 1; i < in2_size; ++i) {
    EXPECT_EQ(in2[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  reader_->Bind(shrunk_array1, decode_key1);
  for (int i = -in1_size + 1; i < in1_size; ++i) {
    EXPECT_EQ(in1[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  reader_->Bind(shrunk_array2, decode_key2);
  for (int i = -in2_size + 1; i < in2_size; ++i) {
    EXPECT_EQ(in2[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  EXPECT_EQ(in1[5], reader_->Get(shrunk_array1, decode_key1, 5));
  EXPECT_EQ(in2[1], reader_->Get(shrunk_array2, decode_key2, 1));
  EXPECT_EQ(in1[3], reader_->Get(shrunk_array1, decode_key1, 3));
  EXPECT_EQ(in2[0], reader_->Get(shrunk_array2, decode_key2, 0));

  Delete(shrunk_array1);
  Delete(shrunk_array2);
}

TEST_P(ShrunkArrayTest, SharedVector) {
  const int in1_size = 8, out1_size = 1;
  uint16_t in1[in1_size] = {0xFFFF, 42, 0, 999, 0, 0xFFFF, 999, 42};
  const int in2_size = 2, out2_size = 2;
  uint64_t in2[in2_size] = {0x0123456789ABCDEFll, 0xFEDCBA9876543210ll};

  uint64_t decode_key1[2], decode_key2[2];
  shrunk_vector_.push_back(0);
  ShrunkArray::Write(in1, in1_size, 3, decode_key1, &shrunk_vector_);
  shrunk_vector_.push_back(1);
  ShrunkArray::Write(in2, in2_size, 3, decode_key2, &shrunk_vector_);
  shrunk_vector_.push_back(2);
  EXPECT_EQ(out1_size + out2_size + 3, shrunk_vector_.size());
  Delete(shrunk_array_);
  shrunk_array_ = Copy(shrunk_vector_);
  reader_->Bind(shrunk_array_, decode_key1);
  for (int i = -in1_size + 1; i < in1_size; ++i) {
    EXPECT_EQ(in1[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  reader_->Bind(shrunk_array_, decode_key2);
  for (int i = -in2_size + 1; i < in2_size; ++i) {
    EXPECT_EQ(in2[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }
}

TEST_P(ShrunkArrayTest, Complexity) {
  const int in_size = 128;
  uint8_t in[in_size] = {
      64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
      64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,

      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  };

  shrunk_vector_.clear();
  ShrunkArray::Write(in, in_size, 3, decode_key_, &shrunk_vector_);
  EXPECT_EQ(9, shrunk_vector_.size());
  Delete(shrunk_array_);
  shrunk_array_ = Copy(shrunk_vector_);
  reader_->Bind(shrunk_array_, decode_key_);
  for (int i = -in_size + 1; i < in_size; ++i) {
    EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  // Save that reader for testing Bind1() below.
  GenericReader* saved_reader = reader_;
  reader_ = NewReader();

  shrunk_vector_.clear();
  ShrunkArray::Write(in, in_size, 2, decode_key_, &shrunk_vector_);
  EXPECT_EQ(12, shrunk_vector_.size());
  Delete(shrunk_array_);
  shrunk_array_ = Copy(shrunk_vector_);
  reader_->Bind(shrunk_array_, decode_key_);
  for (int i = -in_size + 1; i < in_size; ++i) {
    EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  shrunk_vector_.clear();
  ShrunkArray::Write(in, in_size, 1, decode_key_, &shrunk_vector_);
  EXPECT_EQ(14, shrunk_vector_.size());
  Delete(shrunk_array_);
  shrunk_array_ = Copy(shrunk_vector_);
  reader_->Bind(shrunk_array_, decode_key_);
  for (int i = -in_size + 1; i < in_size; ++i) {
    EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }

  // Test Bind1(), and try to distract it with remnants of the
  // complexity 3 reader, and with garbage in decode_key_[1], all of
  // which is supposed to be ignored.

  EXPECT_EQ(0, decode_key_[1]);
  decode_key_[1] = 0xDEADBEEFCAFEBABEll;
  saved_reader->Bind1(shrunk_array_, decode_key_);
  for (int i = -in_size + 1; i < in_size; ++i) {
    EXPECT_EQ(in[abs(i)], reader_->Get(abs(i))) << "i == " << i;
  }
  delete saved_reader;
}

TEST_P(ShrunkArrayTest, SharedGet) {
  // We copy the input data from the FourFreqsFourWidthsA test, because
  // we know it yields a shrunk array with multiple profiles, and hence
  // with an index, which is where the caching in Get() comes into play.
  const int in_size = 512, out_size = 72;
  uint32_t in[in_size] = {
      128,       128,       129,       129,       130,       130,
      131,       131,       132,       132,       133,       133,
      134,       134,       135,       135,       136,       136,
      137,       137,       138,       138,       139,       139,
      140,       140,       141,       141,       142,       142,
      143,       143,

      22144,     22144,     22145,     22145,     22146,     22146,
      22147,     22147,     22148,     22148,     22149,     22149,
      22150,     22150,     22151,     22151,     22152,     22152,
      22153,     22153,     22154,     22154,     22155,     22155,
      22156,     22156,     22157,     22157,     22158,     22158,
      22159,     22159,

      2777160,   2777160,   2777161,   2777161,   2777162,   2777162,
      2777163,   2777163,   2777164,   2777164,   2777165,   2777165,
      2777166,   2777166,   2777167,   2777167,   2777168,   2777168,
      2777169,   2777169,   2777170,   2777170,   2777171,   2777171,
      2777172,   2777172,   2777173,   2777173,   2777174,   2777174,
      2777175,   2777175,

      444777176, 444777176, 444777177, 444777177, 444777178, 444777178,
      444777179, 444777179, 444777180, 444777180, 444777181, 444777181,
      444777182, 444777182, 444777183, 444777183, 444777184, 444777184,
      444777185, 444777185, 444777186, 444777186, 444777187, 444777187,
      444777188, 444777188, 444777189, 444777189, 444777190, 444777190,
      444777191, 444777191,

      224,       224,       224,       224,       224,       224,
      224,       224,       225,       225,       225,       225,
      225,       225,       225,       225,       226,       226,
      226,       226,       226,       226,       226,       226,
      227,       227,       227,       227,       227,       227,
      227,       227,

      22228,     22228,     22228,     22228,     22228,     22228,
      22228,     22228,     22229,     22229,     22229,     22229,
      22229,     22229,     22229,     22229,     22230,     22230,
      22230,     22230,     22230,     22230,     22230,     22230,
      22231,     22231,     22231,     22231,     22231,     22231,
      22231,     22231,

      2777232,   2777232,   2777232,   2777232,   2777232,   2777232,
      2777232,   2777232,   2777233,   2777233,   2777233,   2777233,
      2777233,   2777233,   2777233,   2777233,   2777234,   2777234,
      2777234,   2777234,   2777234,   2777234,   2777234,   2777234,
      2777235,   2777235,   2777235,   2777235,   2777235,   2777235,
      2777235,   2777235,

      444777236, 444777236, 444777236, 444777236, 444777236, 444777236,
      444777236, 444777236, 444777237, 444777237, 444777237, 444777237,
      444777237, 444777237, 444777237, 444777237, 444777238, 444777238,
      444777238, 444777238, 444777238, 444777238, 444777238, 444777238,
      444777239, 444777239, 444777239, 444777239, 444777239, 444777239,
      444777239, 444777239,

      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,       248,       248,       248,       248,
      248,       248,

      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,     22249,     22249,     22249,     22249,
      22249,     22249,

      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,   2777250,   2777250,   2777250,   2777250,
      2777250,   2777250,

      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251, 444777251, 444777251, 444777251, 444777251,
      444777251, 444777251,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,

      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,       254,       254,       254,       254,
      254,       254,
  };

  ShrunkArray::Write(in, in_size, 3, decode_key_, &shrunk_vector_);
  CopyAndBind(out_size);

  class Verifier : public Thread {
   public:
    Verifier(const uint32_t* in, int in_size, const GenericReader* reader,
             bool* verified)
        : Thread(thread::Options().set_joinable(true), "ShrunkArrayVerifier"),
          in_(in),
          in_size_(in_size),
          reader_(reader),
          verified_(verified) {}

   protected:
    void Run() override {
      for (int j = 0; j < 1000; ++j) {
        for (int i = -in_size_ + 1; i < in_size_; ++i) {
          if (reader_->SharedGet(abs(i)) != in_[abs(i)]) {
            *verified_ = false;
            return;
          }
        }
      }
      *verified_ = true;
    }

   private:
    const uint32_t* const in_;
    const int in_size_;
    const GenericReader* const reader_;
    bool* const verified_;
  };

  const int kNumThreads = 10;
  bool verified[kNumThreads];
  Verifier* verifier[kNumThreads];
  for (int i = 0; i < kNumThreads; ++i) {
    verifier[i] = new Verifier(in, in_size, reader_, verified + i);
    verifier[i]->Start();
  }
  for (int i = 0; i < kNumThreads; ++i) {
    verifier[i]->Join();
    delete verifier[i];
    EXPECT_TRUE(verified[i]);
  }
}

TEST_P(ShrunkArrayTest, InvalidKeys) {
  // We need a dummy shrunk array to bind to.
  uint64_t dummy_array[1] = {0};

  // 1. width_main_[3] > 64 in Bind
  {
    uint64_t bad_key[2] = {65, 0};
    EXPECT_DEATH_IF_SUPPORTED(reader_->Bind(dummy_array, bad_key),
                              "width_main_");
  }

  // 2. width_main_[3] > 64 in Bind1
  {
    uint64_t bad_key[2] = {65, 0};
    EXPECT_DEATH_IF_SUPPORTED(reader_->Bind1(dummy_array, bad_key),
                              "width_main_");
  }

  // 3. index_present = true, but offset_lex_[0] not 64-bit aligned
  {
    uint64_t bad_key[2] = {1ULL << 13, 1ULL << 63};
    EXPECT_DEATH_IF_SUPPORTED(reader_->Bind(dummy_array, bad_key),
                              "offset_lex_");
  }

  // 4. Bind1 with non-zero lexicon width in dk0
  {
    uint64_t bad_key[2] = {1ULL << 7, 0};
    EXPECT_DEATH_IF_SUPPORTED(reader_->Bind1(dummy_array, bad_key), "dk0 >> 7");
  }
}

INSTANTIATE_TEST_SUITE_P(PerReader, ShrunkArrayTest, ::testing::Bool());

}  // namespace
