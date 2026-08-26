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

#include "gloop/util/coding/huffcoding.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <cstdint>
#include <vector>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gloop/util/coding/bitcoding.h"
#include "gloop/util/coding/tablecoding.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/gtl/unique_array.h"
#include "gloop/util/hash/hash.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

void Build(const char* label, int* count, int N, int max_length) {
  HuffmanCode* h = HuffmanCode::Create(count, N, max_length);
  h->Dump(label);

  // Check save and restore
  std::vector<char> buffer;
  h->Save(&buffer);
  int consumed;
  HuffmanCode* h2 = HuffmanCode::Restore(&buffer[0], buffer.size(), &consumed);
  EXPECT_EQ(buffer.size(), consumed);
  std::vector<char> buffer2;
  h2->Save(&buffer2);
  EXPECT_EQ(buffer2.size(), buffer.size());
  EXPECT_EQ(0, memcmp(&buffer[0], &buffer2[0], buffer.size()));
  // h2->Dump(label);
  delete h2;

  TableEncoder te;
  h->InitializeEncoder(&te);
  switch (max_length) {
    case 1: {
      TableDecoder<1> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 2: {
      TableDecoder<2> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 3: {
      TableDecoder<3> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 4: {
      TableDecoder<4> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 5: {
      TableDecoder<5> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 6: {
      TableDecoder<6> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 7: {
      TableDecoder<7> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 8: {
      TableDecoder<8> td;
      td.InitializeFromEncoder(&te);
      break;
    }
    case 9: {
      TableDecoder<9> td;
      td.InitializeFromEncoder(&te);
      break;
    }
  }

  // Check that decoder works
  if (max_length <= 10) {
    TableDecoder<10> td;
    td.InitializeFromEncoder(&te);
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    for (int sym = 0; sym < N; sym++) {
      uint32_t enc_length = te.EncodingLength(sym);
      uint32_t enc_bits = te.Encoding(sym);

      // Get random high bits to make up max_length bits
      for (int i = 0; i < 100; i++) {
        uint32_t random_bits = absl::Uniform<uint32_t>(rnd);
        uint32_t bits =
            (enc_length > 31 ? 0 : random_bits << enc_length) | enc_bits;
        EXPECT_EQ(enc_length, td.EncodingLength(bits));
        int s = td.EncodedSymbol(bits);
        EXPECT_EQ(sym, s) << ": got " << s << " expected " << sym;
      }
    }

    // Encode a bunch of symbols
    rnd.Reset(401);
    const size_t buf_size = 1000 * 8;
    auto buf = gtl::MakeUniqueArrayForOverwrite<char>(buf_size);
    BitEncoder be(buf.data(), buf_size);
    for (int i = 0; i < 1000; i++) {
      te.PutSymbol(&be, absl::Uniform<uint32_t>(rnd) % N);
    }
    be.Flush(0);

    // Decode the same symbols back
    rnd.Reset(401);
    BitDecoder bd(buf.data(), be.Bits() / 8);
    for (int i = 0; i < 1000; i++) {
      int expected = absl::Uniform<uint32_t>(rnd) % N;
      EXPECT_EQ(expected, td.GetSymbol(&bd));
    }
  }

  delete h;
}

TEST(HuffCoding, Test) {
  int count[32];

  // Example from Cormen/Leisersen/Rivest
  count[0] = 5;
  count[1] = 9;
  count[2] = 12;
  count[3] = 13;
  count[4] = 16;
  count[5] = 45;
  Build("sample/4", count, 6, 4);
  Build("sample/3", count, 6, 3);

  // Try fibonacci counts
  count[0] = 1;
  count[1] = 1;
  for (int i = 2; i < 32; i++) {
    count[i] = count[i - 2] + count[i - 1];
  }
  Build("fib/8", count, 32, 8);
  Build("fib/5", count, 32, 5);
}

std::vector<char> MakeSampleCode() {
  // Example from Cormen/Leisersen/Rivest
  int count[6]{5, 9, 12, 13, 16, 45};
  auto* src = HuffmanCode::Create(count, 6, 4);
  std::vector<char> code;
  src->Save(&code);
  delete src;
  return code;
}

namespace {

TEST(HuffCoding, RestoreErrorsInternalCorruption) {
  std::vector<char> code = MakeSampleCode();
  for (size_t i = 0; i < code.size(); i++) {
    std::vector<char> mutated = code;
    mutated[i] = code[i] ^ 1;
    int len;
    EXPECT_EQ(HuffmanCode::SafeRestore(mutated.data(), mutated.size(), &len),
              nullptr);
  }
}

TEST(HuffCoding, RestoreErrorsTruncation) {
  std::vector<char> code = MakeSampleCode();
  std::vector<char> mutated = code;
  while (!mutated.empty()) {
    mutated.pop_back();
    int len;
    EXPECT_EQ(HuffmanCode::SafeRestore(mutated.data(), mutated.size(), &len),
              nullptr);
  }
}

TEST(HuffCoding, RestoreErrorsLengthOverflow) {
  std::vector<char> code = MakeSampleCode();
  std::vector<char> mutated = code;
  LittleEndian::Store32(&mutated[4], 0xffffffffu);
  int len;
  EXPECT_EQ(HuffmanCode::SafeRestore(mutated.data(), mutated.size(), &len),
            nullptr);
}

TEST(HuffCoding, RestoreErrorsNumZero) {
  std::vector<char> invalid_code(12);
  LittleEndian::Store32(&invalid_code[0], 0x7af663fbu);  // kMagicNumber
  LittleEndian::Store32(&invalid_code[4], 0);            // num = 0
  uint32_t checksum = HashTo32(absl::string_view(invalid_code.data(), 8));
  LittleEndian::Store32(&invalid_code[8], checksum);
  int len;
  EXPECT_EQ(
      HuffmanCode::SafeRestore(invalid_code.data(), invalid_code.size(), &len),
      nullptr);
}

TEST(HuffCoding, RestoreErrorsNumOne) {
  std::vector<char> invalid_code(13);
  LittleEndian::Store32(&invalid_code[0], 0x7af663fbu);  // kMagicNumber
  LittleEndian::Store32(&invalid_code[4], 1);            // num = 1
  invalid_code[8] = 1;                                   // length = 1
  uint32_t checksum = HashTo32(absl::string_view(invalid_code.data(), 9));
  LittleEndian::Store32(&invalid_code[9], checksum);
  int len;
  EXPECT_EQ(
      HuffmanCode::SafeRestore(invalid_code.data(), invalid_code.size(), &len),
      nullptr);
}

TEST(HuffCoding, RestoreErrorsZeroLengthSymbol) {
  std::vector<char> code = MakeSampleCode();
  std::vector<char> mutated_zero = code;
  mutated_zero[8] = 0;
  uint32_t checksum = HashTo32(absl::string_view(mutated_zero.data(), 14));
  LittleEndian::Store32(&mutated_zero[14], checksum);
  int len;
  EXPECT_EQ(
      HuffmanCode::SafeRestore(mutated_zero.data(), mutated_zero.size(), &len),
      nullptr);
}

TEST(HuffCoding, TableDecoderMaxLenCrash) {
  std::vector<char> code = MakeSampleCode();
  int len;
  auto h = absl::WrapUnique(
      HuffmanCode::SafeRestore(code.data(), code.size(), &len));
  ASSERT_NE(h, nullptr);
  TableEncoder te;
  h->InitializeEncoder(&te);

  TableDecoder<3> td;
  EXPECT_DEATH_IF_SUPPORTED(td.InitializeFromEncoder(&te),
                            "enc_length <= maxlen");
}

void BM_HuffmanRestore(benchmark::State& b) {
  std::vector<char> code = MakeSampleCode();
  for (auto unused : b) {
    int len;
    delete HuffmanCode::SafeRestore(code.data(), code.size(), &len);
    CHECK_EQ(len, code.size());
  }
}
BENCHMARK(BM_HuffmanRestore);

}  // namespace
