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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/crc.h"
#include "gtest/gtest.h"

namespace {

class CrcParallelTest : public ::testing::Test {
 protected:
  CrcParallelTest() {
    data_ = {
        "It was the best of times, it was the worst of times.",
        "Batrachomyomachia",
        "Miniaturatomfrequenznormal",
        "Wenn Fliegen hinter Fliegen fliegen, fliegen Fliegen fliegen nach.",
        "\x1\x2\x3\x4\x5\x6\x7\x8\x9\x10",
        "⇒ arbitrary 😃 unicode data ✓",
    };
  }

  void RunTest(CRC* crc, int num_chunks = 6) {
#ifdef ABSL_HAVE_MEMORY_SANITIZER
    GTEST_SKIP() << "Skipping under memory sanitizer";
#endif
    uint64_t lo, hi;
    crc->Empty(&lo, &hi);
    std::vector<int64_t> bytes(num_chunks);
    std::vector<uint64_t> crc_lo(num_chunks);
    std::vector<uint64_t> crc_hi(num_chunks);

    // Forward
    for (int i = 0; i < num_chunks; i++) {
      bytes[i] = data_[i].length();
      crc->Empty(&crc_lo[i], &crc_hi[i]);
      crc->Extend(&crc_lo[i], &crc_hi[i], data_[i].data(), data_[i].length());
      // Calculate the total here; we don't need to redo it later.
      crc->Extend(&lo, &hi, data_[i].data(), data_[i].length());
    }

    uint64_t rlo, rhi;
    rlo = crc_lo[0];
    rhi = crc_hi[0];
    for (int i = 1; i < num_chunks; i++) {
      crc->Concat(&rlo, &rhi, crc_lo[i], crc_hi[i], bytes[i]);
    }
    EXPECT_EQ(lo, rlo) << "CRC lo does not match.";
    EXPECT_EQ(hi, rhi) << "CRC hi does not match.";

    // Reverse
    for (int i = num_chunks - 1; i >= 0; i--) {
      bytes[i] = data_[i].length();
      crc->Empty(&crc_lo[i], &crc_hi[i]);
      crc->Extend(&crc_lo[i], &crc_hi[i], data_[i].data(), data_[i].length());
    }

    rlo = crc_lo[0];
    rhi = crc_hi[0];
    for (int i = 1; i < num_chunks; i++) {
      crc->Concat(&rlo, &rhi, crc_lo[i], crc_hi[i], bytes[i]);
    }
    EXPECT_EQ(lo, rlo) << "CRC lo does not match.";
    EXPECT_EQ(hi, rhi) << "CRC hi does not match.";

    // Interleaved
    for (int i = 1; i < num_chunks; i += 2) {
      bytes[i] = data_[i].length();
      crc->Empty(&crc_lo[i], &crc_hi[i]);
      crc->Extend(&crc_lo[i], &crc_hi[i], data_[i].data(), 5);
    }
    for (int i = 0; i < num_chunks; i += 2) {
      bytes[i] = data_[i].length();
      crc->Empty(&crc_lo[i], &crc_hi[i]);
      crc->Extend(&crc_lo[i], &crc_hi[i], data_[i].data(), 5);
    }
    for (int i = 0; i < num_chunks; i++) {
      absl::string_view rem(data_[i].data() + 5);
      crc->Extend(&crc_lo[i], &crc_hi[i], rem.data(), rem.size());
    }

    rlo = crc_lo[0];
    rhi = crc_hi[0];
    for (int i = 1; i < num_chunks; i++) {
      crc->Concat(&rlo, &rhi, crc_lo[i], crc_hi[i], bytes[i]);
    }
    EXPECT_EQ(lo, rlo) << "CRC lo does not match.";
    EXPECT_EQ(hi, rhi) << "CRC hi does not match.";
  }

  void RunZeroesTest(CRC* crc, int num_chunks = 6, int num_zeroes = 16) {
#ifdef ABSL_HAVE_MEMORY_SANITIZER
    GTEST_SKIP() << "Skipping under memory sanitizer";
#endif
    uint64_t lo, hi;
    crc->Empty(&lo, &hi);
    std::vector<int64_t> bytes(num_chunks);
    std::vector<uint64_t> crc_lo(num_chunks);
    std::vector<uint64_t> crc_hi(num_chunks);

    for (int i = 0; i < num_chunks; i++) {
      crc->Empty(&crc_lo[i], &crc_hi[i]);
      if (i % 2 == 0) {
        bytes[i] = data_[i].length();
        crc->Extend(&crc_lo[i], &crc_hi[i], data_[i].data(), data_[i].length());
        crc->Extend(&lo, &hi, data_[i].data(), data_[i].length());
      } else {
        bytes[i] = data_[i].length() + num_zeroes;
        absl::string_view half = absl::string_view(data_[i].data(), 5 + 2 * i);
        crc->Extend(&crc_lo[i], &crc_hi[i], half.data(), half.size());
        crc->Extend(&lo, &hi, half.data(), half.size());
        crc->ExtendByZeroes(&crc_lo[i], &crc_hi[i], num_zeroes);
        crc->ExtendByZeroes(&lo, &hi, num_zeroes);

        absl::string_view rem(data_[i].data() + (5 + 2 * i));
        crc->Extend(&lo, &hi, rem.data(), rem.size());
        crc->Extend(&crc_lo[i], &crc_hi[i], rem.data(), rem.size());
      }
    }

    uint64_t rlo, rhi;
    rlo = crc_lo[0];
    rhi = crc_hi[0];
    for (int i = 1; i < num_chunks; i++) {
      crc->Concat(&rlo, &rhi, crc_lo[i], crc_hi[i], bytes[i]);
    }
    EXPECT_EQ(lo, rlo) << "CRC lo does not match.";
    EXPECT_EQ(hi, rhi) << "CRC hi does not match.";
  }

  void RunFullTests(CRC* crc) {
#ifdef ABSL_HAVE_MEMORY_SANITIZER
    GTEST_SKIP() << "Skipping under memory sanitizer";
#endif
    for (int i = 1; i <= data_.size(); i++) {
      RunTest(crc, i);
    }
    RunZeroesTest(crc);
    RunZeroesTest(crc, 6, 257);
    RunZeroesTest(crc, 3, 1700);
  }

  std::vector<std::string> data_;
};

TEST_F(CrcParallelTest, CRC8) { RunFullTests(CRC::Default(8, 0)); }

TEST_F(CrcParallelTest, TestEverything) {
  for (int i = 0; i < CRC::N_POLYS; i++) {
    std::unique_ptr<CRC> crc(
        CRC::New(CRC::POLYS[i].lo, CRC::POLYS[i].hi, CRC::POLYS[i].degree, 0));
    LOG(INFO) << "Testing CRC poly #" << i;
    RunFullTests(crc.get());
  }
}

TEST_F(CrcParallelTest, TestStandards) {
  std::vector<CRC::CRCName> names({
      CRC::CRCName::CRC_8_ATM,       CRC::CRCName::CRC_8_CCITT,
      CRC::CRCName::CRC_8_DALLAS,    CRC::CRCName::CRC_8,
      CRC::CRCName::CRC_8_SAE,       CRC::CRCName::CRC_10,
      CRC::CRCName::CRC_11,          CRC::CRCName::CRC_12,
      CRC::CRCName::CRC_15_CAN,      CRC::CRCName::CRC_16_CCITT,
      CRC::CRCName::CRC_16_DNP,      CRC::CRCName::CRC_16,
      CRC::CRCName::CRC_24_RADIX_64, CRC::CRCName::CRC_30,
      CRC::CRCName::CRC_32,          CRC::CRCName::CRC_32C,
      CRC::CRCName::CRC_32K,         CRC::CRCName::CRC_64_ISO,
      CRC::CRCName::CRC_64_ECMA,
  });

  for (int i = 0; i < names.size(); i++) {
    LOG(INFO) << "Testing standard CRC polynomial " << names[i];
    RunFullTests(CRC::Standard(names[i], 0));
  }
}

}  // namespace
