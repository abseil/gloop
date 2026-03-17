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

// Unit Test Party, 08/29/02 :-)

#include "gloop/util/coding/varint.h"

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/unique_array.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

// A straightforward implementation of Length64 for testing and benchmarking
inline int Varint_Length64Old(uint64_t v) {
  // Each byte of output stores 7 bits of "v" until "v" becomes zero
  int nbytes = 0;
  do {
    nbytes++;
    v >>= 7;
  } while (v != 0);
  return nbytes;
}

inline int Varint_Length32Old(uint32_t v) {
  // Each byte of output stores 7 bits of "v" until "v" becomes zero
  int nbytes = 0;
  do {
    nbytes++;
    v >>= 7;
  } while (v != 0);
  return nbytes;
}

static char* Varint_Encode64Old(char* ptr, uint64_t v) {
  static const int B = 128;
  do {
    // Encode next 7 bits + terminator bit
    int bits = static_cast<int>(v & 127);
    v >>= 7;
    *(ptr++) = static_cast<unsigned char>(bits + ((v != 0) ? B : 0));
  } while (v != 0);
  return ptr;
}

TEST(VarintTest, Length32) {
  CHECK_EQ(Varint::Length32(0), 1);
  CHECK_EQ(Varint::Length32(1), 1);
  CHECK_EQ(Varint::Length32(127), 1);
  CHECK_EQ(Varint::Length32(128), 2);
  CHECK_EQ(Varint::Length32(16383), 2);
  CHECK_EQ(Varint::Length32(16384), 3);
  CHECK_EQ(Varint::Length32((uint32_t)1 << 31), Varint::kMax32);
  // Check around each power of two
  for (int i = 0; i < 32; i++) {
    uint32_t v = (1ull << i);
    CHECK_EQ(Varint::Length32(v - 1), Varint_Length32Old(v - 1));
    CHECK_EQ(Varint::Length32(v), Varint_Length32Old(v));
    CHECK_EQ(Varint::Length32(v + 1), Varint_Length32Old(v + 1));
  }
  CHECK_EQ(Varint::Length32(((uint32_t)1 << 21) - 1), 3);
  CHECK_EQ(Varint::Length32((uint32_t)1 << 21), 4);
  CHECK_EQ(Varint::Length32((uint32_t)1 << 31), Varint::kMax32);
  CHECK_EQ(Varint::Length32(~0u), Varint::kMax32);

  char buf[Varint::kMax32];
  ACMRandom rnd(301);
  for (int i = 0; i < 100000; i++) {
    uint32_t v = absl::Uniform<uint32_t>(rnd);
    const char* p = Varint::Encode32(buf, v);
    CHECK_EQ(Varint::Length32(v), Varint_Length32Old(v));
    CHECK_EQ(Varint::Length32(v), p - buf);
    CHECK_EQ(Varint::Length32(v), p - buf);
  }
}

TEST(VarintTest, Length64) {
  CHECK_EQ(Varint::Length64(0), 1);
  CHECK_EQ(Varint::Length64(1), 1);
  CHECK_EQ(Varint::Length64(127), 1);
  CHECK_EQ(Varint::Length64(128), 2);
  CHECK_EQ(Varint::Length64(16383), 2);
  CHECK_EQ(Varint::Length64(16384), 3);
  // Check around each power of two
  for (int i = 0; i < 63; i++) {
    uint64_t v = (1ull << i);
    CHECK_EQ(Varint::Length64(v - 1), Varint_Length64Old(v - 1));
    CHECK_EQ(Varint::Length64(v), Varint_Length64Old(v));
    CHECK_EQ(Varint::Length64(v + 1), Varint_Length64Old(v + 1));
  }
  CHECK_EQ(Varint::Length64(((uint64_t)1 << 21) - 1), 3);
  CHECK_EQ(Varint::Length64((uint64_t)1 << 21), 4);
  CHECK_EQ(Varint::Length64((uint64_t)1 << 63), Varint::kMax64);
  CHECK_EQ(Varint::Length64(~0ull), Varint::kMax64);

  char buf[Varint::kMax64];
  ACMRandom rnd(301);
  for (int i = 0; i < 100000; i++) {
    uint64_t v = absl::Uniform<uint64_t>(rnd);
    const char* p = Varint::Encode64(buf, v);
    CHECK_EQ(Varint::Length64(v), Varint_Length64Old(v));
    CHECK_EQ(Varint::Length64(v), p - buf);
    CHECK_EQ(Varint::Length64(v), p - buf);
  }
}

TEST(VarintTest, EncodeParse32) {
  // Test Parse32 and Encode32
  // Encode32 the 28-bit number 1110 0100 1001 1001 1000 0110 0111
  // aka hex E 49 98 67
  // which can be split in groups of 7 bytes as 1110010 0100110 0110000 1100111
  // which should encode to (reverse the bytes, set the MSB to 1):
  // 11100111 10110000 10100110 01110010 aka hex E7 B0 A6 72
  char s[20];
  uint32_t n = 0xe499867, n_decrypt = 0;
  unsigned char n_encrypt[5] = {0xe7, 0xb0, 0xa6, 0x72, '\0'};
  char* end_s = Varint::Encode32(s, n);
  // now end_s - s represents the encryption length
  CHECK_EQ(end_s - s, Varint::Length32(n));
  *end_s = '\0';  // terminate the string
  CHECK_STREQ(s, reinterpret_cast<char*>(n_encrypt));

  // now decrypt it and make sure that we get (a) the original result and
  // (b) the same encryption length
  CHECK_EQ(end_s, Varint::Parse32(s, &n_decrypt));
  CHECK_EQ(n, n_decrypt);

  CHECK_EQ(end_s, Varint::Parse32Inline(s, &n_decrypt));
  CHECK_EQ(n, n_decrypt);
}

TEST(VarintTest, EncodeParse64) {
  // Test Parse64 and Encode64
  // Encode64 the 60-bit number
  // 1110 0100 1001 1001 1000 0110 0111 1001 0100 0111 0000 1101 1001 1000 1101
  // aka hex E 49 98 67 94 70 D9 8D
  // which can be split in groups of 7 bytes as
  // 1110 0100100 1100110 0001100 1111001 0100011 1000011 0110011 0001101
  // which should encode to (reverse the bytes, set the MSB to 1):
  // 10001101 10110011 11000011 10100011 11111001 10001100 11100110 10100100
  // 00001110 aka hex 8D B3 C3 A3 F9 8C E6 A4 0E
  char s[10];
  uint64_t n = 0xe4998679470d98dull, n_decrypt = 0ull;
  unsigned char n_encrypt[10] = {0x8d, 0xb3, 0xc3, 0xa3, 0xf9,
                                 0x8c, 0xe6, 0xa4, 0x0e, '\0'};
  char* end_s = Varint::Encode64(s, n);
  // now end_s - s represents the encryption length
  CHECK_EQ(end_s - s, Varint::Length64(n));
  *end_s = '\0';  // terminate the string
  CHECK_STREQ(s, reinterpret_cast<char*>(n_encrypt));

  // now decrypt it and make sure that we get (a) the original result and
  // (b) the same encryption length
  CHECK_EQ(end_s, Varint::Parse64(s, &n_decrypt));
  CHECK_EQ(n, n_decrypt);
}

TEST(VarintTest, EncodeParseSkipExtensive) {
  // encode/decode all powers of two, alternate between varint32 and varint64
  auto buf = gtl::MakeUniqueArrayForOverwrite<char>(128 * Varint::kMax64);
  char* ptr = buf.data();
  for (int p = 0; p < 64; p++) {
    if (p < 32) ptr = Varint::Encode32(ptr, (1u << p));
    ptr = Varint::Encode64(ptr, (uint64_t{1} << p));
  }

  // decode forward
  const char* ptrc = buf.data();
  uint32_t val32 = 0;
  uint64_t val64 = 0ull;
  for (int p = 0; p < 64; p++) {
    if (p < 32) {
      ptrc = Varint::Parse32(ptrc, &val32);
      CHECK_EQ(val32, (1u << p));
    }
    ptrc = Varint::Parse64(ptrc, &val64);
    CHECK_EQ(val64, (uint64_t{1} << p));
  }

  // decode backward ('ptr' already points just past the end)
  for (int p = 63; p >= 0; --p) {
    if (p < 32) {
      ptrc = Varint::Parse32Backward(ptrc, buf.data(), &val32);
      CHECK_EQ(val32, (1u << p));
    }
    ptrc = Varint::Parse64Backward(ptrc, buf.data(), &val64);
    CHECK_EQ(val64, (uint64_t{1} << p));
  }
  CHECK(ptrc == buf.data());

  // skip forward
  ptrc = buf.data();
  for (int p = 0; p < 64; p++) {
    if (p < 32) ptrc = Varint::Skip32(ptrc);
    ptrc = Varint::Skip64(ptrc);
  }

  // skip backward
  for (int p = 63; p >= 0; --p) {
    if (p < 32) ptrc = Varint::Skip32Backward(ptrc, buf.data());
    ptrc = Varint::Skip64Backward(ptrc, buf.data());
  }
  CHECK(ptrc == buf.data());

  // encode/decode 1000 random numbers in varint32 and varint64
  auto buf32 = gtl::MakeUniqueArrayForOverwrite<char>(1000 * Varint::kMax32);
  auto buf64 = gtl::MakeUniqueArrayForOverwrite<char>(1000 * Varint::kMax64);
  char* ptr32 = buf32.data();
  char* ptr64 = buf64.data();
  ACMRandom rgen(time(nullptr));
  std::vector<uint64_t> values;

  for (int i = 0; i < 1000; ++i) {
    val64 = (uint64_t{absl::Uniform<uint32_t>(rgen)} << 32) |
            absl::Uniform<uint32_t>(rgen);
    values.push_back(val64);
    ptr32 = Varint::Encode32(ptr32, static_cast<uint32_t>(val64));
    ptr64 = Varint::Encode64(ptr64, val64);
  }
  // decode forward
  const char* ptr32c = buf32.data();
  const char* ptr64c = buf64.data();
  for (int i = 0; i < values.size(); ++i) {
    ptr32c = Varint::Parse32(ptr32c, &val32);
    ptr64c = Varint::Parse64(ptr64c, &val64);
    CHECK_EQ(val32, static_cast<uint32_t>(values[i]));
    CHECK_EQ(val64, values[i]);
  }
  // decode backward
  for (int i = values.size() - 1; i >= 0; --i) {
    ptr32c = Varint::Parse32Backward(ptr32c, buf32.data(), &val32);
    ptr64c = Varint::Parse64Backward(ptr64c, buf64.data(), &val64);
    CHECK_EQ(val32, static_cast<uint32_t>(values[i]));
    CHECK_EQ(val64, values[i]);
  }
  CHECK(ptr32c == buf32.data());
  CHECK(ptr64c == buf64.data());
}

TEST(VarintTest, Parse32WithLimitEmpty) {
  const char storage[1] = {0};
  uint32_t val = 0;
  EXPECT_EQ(Varint::Parse32WithLimit(storage, storage, &val), nullptr);
}

TEST(VarintTest, Parse32WithLimitBeforePtr) {
  const char storage[1] = {0};
  uint32_t val = 0;
  // This is not a valid STL range.  Parse32WithLimit happens to support it,
  // but it's not clear whether we should promise this.
  EXPECT_EQ(Varint::Parse32WithLimit(storage + 1, storage, &val), nullptr);
}

TEST(VarintTest, Parse32WithLimitNull) {
  uint32_t val = 0;
  EXPECT_EQ(Varint::Parse32WithLimit(nullptr, nullptr, &val), nullptr);
}

TEST(VarintTest, Parse32WithLimit) {
  unsigned char s_storage[10] = {0x80, 0x81, 0x82, 0x83, 0x84,
                                 0x85, 0x86, 0x87, 0x88, '\0'};
  char* s = reinterpret_cast<char*>(s_storage);
  uint32_t bogus = 0;
  // Parsing should fail here and return nullptr, because s is too long
  CHECK_EQ(Varint::Parse32WithLimit(s, s + 10, &bogus), nullptr);
  bogus = 0;

  // Should fail because it doesn't terminate even though
  // we have exactly the right length
  unsigned char t_storage[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  char* t = reinterpret_cast<char*>(t_storage);
  CHECK_EQ(Varint::Parse32WithLimit(t, t + Varint::kMax32, &bogus), nullptr);
  bogus = 0;

  // Should fail because it doesn't terminate, slow path.
  unsigned char t2_storage[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  char* t2 = reinterpret_cast<char*>(t2_storage);
  CHECK_EQ(Varint::Parse32WithLimit(t2, t2 + 4, &bogus), nullptr);
  bogus = 0;

  // Should succeed
  unsigned char u_storage[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
  char* u = reinterpret_cast<char*>(u_storage);
  CHECK_EQ(Varint::Parse32WithLimit(u, u + Varint::kMax32, &bogus), u + 5);
  CHECK_EQ(bogus, 0xFFFFFFFF);
  bogus = 0;

  // Should succeed, slow path
  unsigned char u2_storagte[4] = {0xFF, 0xFF, 0xFF, 0x7F};
  char* u2 = reinterpret_cast<char*>(u2_storagte);
  CHECK_EQ(Varint::Parse32WithLimit(u2, u2 + 4, &bogus), u2 + 4);
  CHECK_EQ(bogus, 0x0FFFFFFF);
  bogus = 0;

  // Should fail because it's a bad code word
  unsigned char v_storage[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  char* v = reinterpret_cast<char*>(v_storage);
  CHECK_EQ(Varint::Parse32WithLimit(v, v + Varint::kMax32, &bogus), nullptr);
}

TEST(VarintTest, Parse64WithLimitEmpty) {
  const char storage[1] = {0};
  uint64_t val = 0;
  EXPECT_EQ(Varint::Parse64WithLimit(storage, storage, &val), nullptr);
}

TEST(VarintTest, Parse64WithLimitBeforePtr) {
  const char storage[1] = {0};
  uint64_t val = 0;
  // This is not a valid STL range.  Parse64WithLimit happens to support it,
  // but it's not clear whether we should promise this.
  EXPECT_EQ(Varint::Parse64WithLimit(storage + 1, storage, &val), nullptr);
}

TEST(VarintTest, Parse64WithLimitNull) {
  uint64_t val = 0;
  EXPECT_EQ(Varint::Parse64WithLimit(nullptr, nullptr, &val), nullptr);
}

TEST(VarintTest, Parse64WithLimit) {
  unsigned char s_storagte[15] = {0x80, 0x81, 0x82, 0x83, 0x84,
                                  0x85, 0x86, 0x87, 0x88, 0x89,
                                  0x80, 0x8A, 0x8B, 0x8C, 0x0};
  char* s = reinterpret_cast<char*>(s_storagte);
  uint64_t bogus = 0;
  // Parsing will fail because this is too long
  EXPECT_EQ(nullptr, Varint::Parse64WithLimit(s, s + 15, &bogus));
  bogus = 0;

  // Should fail because it doesn't terminate even though
  // we have exactly the right length
  unsigned char t_storage[10] = {0x80, 0x81, 0x82, 0x83, 0x84,
                                 0x85, 0x86, 0x87, 0x88, 0x89};
  char* t = reinterpret_cast<char*>(t_storage);
  EXPECT_EQ(nullptr, Varint::Parse64WithLimit(t, t + Varint::kMax64, &bogus));
  bogus = 0;

  // Should fail because it doesn't terminate, slow path.
  unsigned char t2_storage[9] = {0x80, 0x81, 0x82, 0x83, 0x84,
                                 0x85, 0x86, 0x87, 0x88};
  char* t2 = reinterpret_cast<char*>(t2_storage);
  EXPECT_EQ(nullptr, Varint::Parse64WithLimit(t2, t2 + 9, &bogus));
  bogus = 0;

  // Should succeed
  unsigned char u_storage[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0x1};
  char* u = reinterpret_cast<char*>(u_storage);
  EXPECT_EQ(u + 10, Varint::Parse64WithLimit(u, u + Varint::kMax64, &bogus));
  EXPECT_EQ(uint64_t{0xFFFFFFFFFFFFFFFFu}, bogus);
  bogus = 0;

  // Should succeed, slow path
  unsigned char u2_storage[9] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0x7F};
  char* u2 = reinterpret_cast<char*>(u2_storage);
  CHECK_EQ(u2 + 9, Varint::Parse64WithLimit(u2, u2 + 9, &bogus));
  CHECK_EQ(bogus, uint64_t{0x7FFFFFFFFFFFFFFF});
  bogus = 0;

  // Should fail because it's a bad code word
  unsigned char v_storage[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  char* v = reinterpret_cast<char*>(v_storage);
  EXPECT_EQ(nullptr, Varint::Parse64WithLimit(v, v + Varint::kMax64, &bogus));
}

TEST(VarintTest, Skip32) {
  // Should fail because it's a bad code word
  unsigned char v[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  CHECK_EQ(Varint::Skip32(reinterpret_cast<char*>(v)), nullptr);
}

TEST(VarintTest, Skip32WithLimitEmpty) {
  const char storage[1] = {0};
  EXPECT_EQ(Varint::Skip32WithLimit(storage, storage), nullptr);
}

TEST(VarintTest, Skip32WithLimitBeforePtr) {
  const char storage[1] = {0};
  EXPECT_EQ(Varint::Skip32WithLimit(storage + 1, storage), nullptr);
}

TEST(VarintTest, Skip32WithLimitNullLimit) {
  const char storage[1] = {0};
  EXPECT_EQ(Varint::Skip32WithLimit(storage, nullptr), nullptr);
}

TEST(VarintTest, Skip32WithLimitNull) {
  EXPECT_EQ(Varint::Skip32WithLimit(nullptr, nullptr), nullptr);
}

TEST(VarintTest, Skip32WithLimitLongerThanMax) {
  unsigned char storage[10] = {0x80, 0x81, 0x82, 0x83, 0x84,
                               0x85, 0x86, 0x87, 0x88, '\0'};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip32WithLimit(v, v + 10), nullptr);
}

TEST(VarintTest, Skip32WithLimitInvalidLastByte) {
  unsigned char storage[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip32WithLimit(v, v + 5), nullptr);
}

TEST(VarintTest, Skip32WithLimitInvalidMissingLastByte) {
  unsigned char storage[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip32WithLimit(v, v + 4), nullptr);
}

TEST(VarintTest, Skip32WithLimitValidOneByte) {
  unsigned char storage[1] = {0x01};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip32WithLimit(v, v + 1), v + 1);
}

TEST(VarintTest, Skip32WithLimitValidVariableLength) {
  unsigned char storage[4] = {0xFF, 0xFF, 0xFF, 0x7F};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip32WithLimit(v, v + 4), v + 4);
}

TEST(VarintTest, Skip32WithLimitValidMaxLength) {
  unsigned char storage[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip32WithLimit(v, v + 5), v + 5);
}

TEST(VarintTest, Skip64) {
  // Should fail because it's a bad code word
  unsigned char v[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                         0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  CHECK_EQ(Varint::Skip64(reinterpret_cast<char*>(v)), nullptr);
}

TEST(VarintTest, Skip64WithLimitEmpty) {
  const char storage[1] = {0};
  EXPECT_EQ(Varint::Skip64WithLimit(storage, storage), nullptr);
}

TEST(VarintTest, Skip64WithLimitBeforePtr) {
  const char storage[1] = {0};
  EXPECT_EQ(Varint::Skip64WithLimit(storage + 1, storage), nullptr);
}

TEST(VarintTest, Skip64WithLimitNullLimit) {
  const char storage[1] = {0};
  EXPECT_EQ(Varint::Skip64WithLimit(storage, nullptr), nullptr);
}

TEST(VarintTest, Skip64WithLimitNull) {
  EXPECT_EQ(Varint::Skip64WithLimit(nullptr, nullptr), nullptr);
}

TEST(VarintTest, Skip64WithLimitLongerThanMax) {
  unsigned char storage[20] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86,
                               0x87, 0x88, 0xFF, 0x80, 0x81, 0x82, 0x83,
                               0x84, 0x85, 0x86, 0x87, 0x88, '\0'};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip64WithLimit(v, v + 20), nullptr);
}

TEST(VarintTest, Skip64WithLimitInvalidLastByte) {
  unsigned char storage[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                               0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip64WithLimit(v, v + 10), nullptr);
}

TEST(VarintTest, Skip64WithLimitInvalidMissingLastByte) {
  unsigned char storage[9] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                              0xFF, 0xFF, 0xFF, 0xFF};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip64WithLimit(v, v + 9), nullptr);
}

TEST(VarintTest, Skip64WithLimitValidOneByte) {
  unsigned char storage[1] = {0x01};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip64WithLimit(v, v + 1), v + 1);
}

TEST(VarintTest, Skip64WithLimitValidVariableLength) {
  unsigned char storage[4] = {0xFF, 0xFF, 0xFF, 0x7F};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip64WithLimit(v, v + 4), v + 4);
}

TEST(VarintTest, Skip64WithLimitValidMaxLength) {
  unsigned char storage[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                               0xFF, 0xFF, 0xFF, 0xFF, 0x01};
  char* v = reinterpret_cast<char*>(storage);
  EXPECT_EQ(Varint::Skip64WithLimit(v, v + 10), v + 10);
}

class VarintTestWithParam : public ::testing::TestWithParam<bool> {};
TEST_P(VarintTestWithParam, Append) {
  std::string s;
  for (uint32_t i = 3; i < 40000000; i *= 2) {
    Varint::Append32(&s, i);
    Varint::Append64(&s, static_cast<uint64_t>(i) << ((16 + i) % 64));
  }
  const char* p = s.data();
  for (uint32_t i = 3; i < 40000000; i *= 2) {
    uint32_t val = 0;
    bool inlined = GetParam();
    if (inlined) {
      p = Varint::Parse32Inline(p, &val);
    } else {
      p = Varint::Parse32(p, &val);
    }
    CHECK(p != nullptr);
    CHECK_EQ(i, val);
    LOG(INFO) << i << " " << val;
    uint64_t val64 = 0ull;
    p = Varint::Parse64(p, &val64);
    CHECK(p != nullptr);
    CHECK_EQ(static_cast<uint64_t>(i) << ((16 + i) % 64), val64);
    LOG(INFO) << i << " " << val64;
  }
  CHECK_EQ(p, s.data() + s.size());
}
INSTANTIATE_TEST_SUITE_P(ConditionallyInline, VarintTestWithParam,
                         ::testing::Values(false, true));

TEST(VarintTest, Skip64Backward) {
  unsigned char s_storage[12] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
                                 0x86, 0x87, 0x88, 0x89, 0x80, 0x00};
  char* s = reinterpret_cast<char*>(s_storage);
  EXPECT_EQ(nullptr, Varint::Skip64Backward(s + 11, s));
}

static void CheckFastDecodeDeltas(uint64_t* deltas, int num_deltas,
                                  int64_t goal) {
  absl::FixedArray<char, 0> buf(num_deltas * Varint::kMax64);
  char* p = buf.data();
  int64_t sum = 0;
  // Calculate the correct sum and pointer position.
  for (int i = 0; i < num_deltas; i++) {
    sum += deltas[i];
    CHECK_LE(sum, std::numeric_limits<int64_t>::max());
    p = Varint::Encode64(p, deltas[i]);
    if (sum >= goal) break;
  }
  CHECK_GE(sum, goal);

  int64_t sum2;
  const char* p2 = Varint::FastDecodeDeltas(buf.data(), goal, &sum2);
  CHECK_EQ(sum2, sum);
  CHECK_EQ(p2, p);
}

TEST(VarintTest, FastDecodeDeltas) {
  // basic stuff
  uint64_t d1[] = {8, 0, 11, 12345, 1234567};
  CheckFastDecodeDeltas(d1, 5, 1);
  CheckFastDecodeDeltas(d1, 5, 19);
  CheckFastDecodeDeltas(d1, 5, 12300);
  CheckFastDecodeDeltas(d1, 5, 1234500);

  // large 32 bit deltas
  uint64_t d2[] = {std::numeric_limits<uint32_t>::max() - 1000, 100, 900};
  CheckFastDecodeDeltas(d2, 3, std::numeric_limits<uint32_t>::max());

  // large 64 bit deltas
  uint64_t d3[] = {999, 1, std::numeric_limits<int64_t>::max() - 1001, 1};
  CheckFastDecodeDeltas(d3, 4, std::numeric_limits<int64_t>::max());
}

// Helper routine to initialize an array of N values based on "bit_len".
// If "bit_len" is 0, random values are chosen.  Otherwise, the values
// are all the same constant value of "1ull << bit_len"
static void InitArray(uint64_t* a, const int N, const int bit_len,
                      benchmark::State& state) {
  ACMRandom rnd(301);
  bool label_set = false;
  for (int i = 0; i < N; i++) {
    if (bit_len == 0) {
      a[i] = absl::Uniform<uint32_t>(rnd);
      if (!label_set) {
        state.SetLabel("Random values");
        label_set = true;
      }
    } else {
      a[i] = (1ull << bit_len);
    }
  }
}

class VarintBMHelper {
 public:
  VarintBMHelper() {}

  // This type is neither copyable nor movable.
  VarintBMHelper(const VarintBMHelper&) = delete;
  VarintBMHelper& operator=(const VarintBMHelper&) = delete;
  virtual ~VarintBMHelper() {}

  // Helper routine to initialize a char buffer with varints of size
  // (0 to (count - 1)) << shift_bits
  void InitVarints(int shift_bits) {
    char* buf = buf_;
    for (int i = 0; i < kCount; i++) {
      int val = i << shift_bits;
      buf = Varint::Encode32(buf, val);
    }
  }

  void RunParser(benchmark::State& state) {
    uint32_t val;
    const char* bptr = nullptr;
    while (state.KeepRunningBatch(kCount)) {
      // For each iteration we parse all values in buf
      bptr = buf_;
      for (int i = 0; i < kCount; i++) {
        bptr = Varint::Parse32(bptr, &val);
      }
    }
    benchmark::DoNotOptimize(bptr);
  }

  void RunInlineParser(benchmark::State& state) {
    uint32_t val;

    const char* bptr = nullptr;
    while (state.KeepRunningBatch(kCount)) {
      // For each iteration we parse all values in buf
      bptr = buf_;
      for (int i = 0; i < kCount; i++) {
        bptr = Varint::Parse32Inline(bptr, &val);
      }
    }
    benchmark::DoNotOptimize(bptr);
  }

 private:
  static constexpr int kCount = 128;
  char buf_[Varint::kMax32 * kCount];
};

static void BM_Parse32(benchmark::State& state) {
  VarintBMHelper helper;
  helper.InitVarints(state.range(0));
  helper.RunParser(state);
}
BENCHMARK(BM_Parse32)->Arg(0)->Arg(8)->Arg(16)->Arg(24);

static void BM_Parse32Inline(benchmark::State& state) {
  VarintBMHelper helper;
  helper.InitVarints(state.range(0));
  helper.RunInlineParser(state);
}
BENCHMARK(BM_Parse32Inline)->Arg(0)->Arg(8)->Arg(16)->Arg(24);

static void BM_Length32(benchmark::State& state) {
  // Smaller than L1 cache, and still performs a 32 bit load
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  int result = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      result += Varint::Length32(v);
    }
  }

  benchmark::DoNotOptimize(result);
}
BENCHMARK(BM_Length32)->Arg(0)->Arg(1)->Arg(15)->Arg(22)->Arg(29);

static void BM_Length32Old(benchmark::State& state) {
  // Smaller than L1 cache, and still performs a 32 bit load
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  int result = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      result += Varint_Length32Old(v);
    }
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(BM_Length32Old)->Arg(0)->Arg(1)->Arg(8)->Arg(15)->Arg(22)->Arg(29);

static void BM_Length64(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  int result = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      result += Varint::Length64(v);
    }
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(BM_Length64)->Arg(0)->Arg(1)->Arg(8)->Arg(50)->Arg(63);

static void BM_Length64Old(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  int result = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      result += Varint_Length64Old(v);
    }
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(BM_Length64Old)->Arg(0)->Arg(1)->Arg(8)->Arg(50)->Arg(63);

static void BM_Encode32(benchmark::State& state) {
  // Smaller than L1 cache, and still performs a 32 bit load
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  char buf[Varint::kMax64];
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      Varint::Encode32(buf, v);
    }
  }
}
BENCHMARK(BM_Encode32)->Arg(0)->Arg(1)->Arg(8)->Arg(24)->Arg(30);

static void BM_Encode64(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  char buf[Varint::kMax64];
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      Varint::Encode64(buf, v);
    }
  }
}
BENCHMARK(BM_Encode64)->Arg(0)->Arg(1)->Arg(8)->Arg(50)->Arg(63);

static void BM_Encode64Old(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  char buf[Varint::kMax64];
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      Varint_Encode64Old(buf, v);
    }
  }
}
BENCHMARK(BM_Encode64Old)->Arg(0)->Arg(1)->Arg(8)->Arg(50)->Arg(63);

// Measures the latency of only the length / next pointer computation,
// since the value is not needed to parse the next input.
static void BM_Parse64(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  std::string s;
  for (int i = 0; i < kBatchSize; i++) {
    Varint::Append64(&s, vals[i]);
  }
  while (state.KeepRunningBatch(kBatchSize)) {
    const char* p = s.data();
    for (int i = 0; i < kBatchSize; ++i) {
      uint64_t v;
      p = Varint::Parse64(p, &v);
      benchmark::DoNotOptimize(v);
    }
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_Parse64)->Arg(0)->Arg(1)->Arg(8)->Arg(50)->Arg(63);

// Measures the latency of only the value computation, since the pointer
// is not needed to parse the next input.
static void BM_Parse64ValueLatency(benchmark::State& state) {
  constexpr int kNumVarints = 128;
  uint64_t vals[kNumVarints];
  InitArray(vals, kNumVarints, state.range(0), state);
  std::string varints[kNumVarints];
  for (int i = 0; i < kNumVarints; i++) {
    Varint::Append64(&varints[i], vals[i]);
  }
  int index = 0;
  for (auto s : state) {
    uint64_t v;
    const char* p = Varint::Parse64(varints[index].data(), &v);
    index = v % kNumVarints;
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_Parse64ValueLatency)->Arg(0)->Arg(1)->Arg(8)->Arg(50)->Arg(63);

static void BM_Parse64WithLimitBeyondEnd(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  std::string s;
  for (int i = 0; i < kBatchSize; i++) {
    Varint::Append64(&s, vals[i]);
  }
  // Append more bytes than the longest varint, so we can always take the path
  // that can read past the end of the varint being decoded.
  s.resize(s.size() + Varint::kMax64 + 1);
  const char* const end = s.data() + s.size();
  while (state.KeepRunningBatch(kBatchSize)) {
    const char* p = s.data();
    for (int i = 0; i < kBatchSize; ++i) {
      uint64_t v;
      p = Varint::Parse64WithLimit(p, end, &v);
      benchmark::DoNotOptimize(v);
    }
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_Parse64WithLimitBeyondEnd)
    ->Arg(0)
    ->Arg(1)
    ->Arg(8)
    ->Arg(50)
    ->Arg(63);

static void BM_Parse64WithLimitBeyondEndValueLatency(benchmark::State& state) {
  constexpr int kNumVarints = 128;
  uint64_t vals[kNumVarints];
  InitArray(vals, kNumVarints, state.range(0), state);
  std::string varints[kNumVarints];
  for (int i = 0; i < kNumVarints; i++) {
    Varint::Append64(&varints[i], vals[i]);
    // Resize the string to the maximum length of a varint, so we can always
    // take the path that can read past the end of the varint being decoded.
    varints[i].resize(Varint::kMax64);
  }
  int index = 0;
  for (auto s : state) {
    const char* p = varints[index].data();
    const char* end = p + varints[index].size();
    uint64_t v;
    p = Varint::Parse64WithLimit(p, end, &v);
    index = v % kNumVarints;
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_Parse64WithLimitBeyondEndValueLatency)
    ->Arg(0)
    ->Arg(1)
    ->Arg(8)
    ->Arg(50)
    ->Arg(63);

static void BM_Parse64WithLimitMinLimit(benchmark::State& state) {
  constexpr int kBatchSize = 1024;
  uint64_t vals[kBatchSize];
  InitArray(vals, kBatchSize, state.range(0), state);
  std::string s;
  // Lengths of each varint in s.  These will all be the same except when
  // state.range(0) == 0.
  int lengths[kBatchSize];
  for (int i = 0; i < kBatchSize; i++) {
    int old_size = s.size();
    Varint::Append64(&s, vals[i]);
    lengths[i] = s.size() - old_size;
  }
  while (state.KeepRunningBatch(kBatchSize)) {
    const char* p = s.data();
    for (int i = 0; i < kBatchSize; ++i) {
      uint64_t v;
      p = Varint::Parse64WithLimit(p, p + lengths[i], &v);
      benchmark::DoNotOptimize(v);
    }
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_Parse64WithLimitMinLimit)
    ->Arg(0)
    ->Arg(1)
    ->Arg(8)
    ->Arg(50)
    ->Arg(63);

static void BM_Parse64WithLimitMinLimitValueLatency(benchmark::State& state) {
  constexpr int kNumVarints = 128;
  uint64_t vals[kNumVarints];
  InitArray(vals, kNumVarints, state.range(0), state);
  std::string varints[kNumVarints];
  // Lengths of each varint in varints.  These will all be the same except when
  // state.range(0) == 0.
  int lengths[kNumVarints];
  for (int i = 0; i < kNumVarints; i++) {
    Varint::Append64(&varints[i], vals[i]);
    lengths[i] = varints[i].size();
  }
  int index = 0;
  for (auto s : state) {
    const char* p = varints[index].data();
    const char* end = p + lengths[index];
    uint64_t v;
    p = Varint::Parse64WithLimit(p, end, &v);
    index = v % kNumVarints;
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_Parse64WithLimitMinLimitValueLatency)
    ->Arg(0)
    ->Arg(1)
    ->Arg(8)
    ->Arg(50)
    ->Arg(63);

static void BM_Append32(benchmark::State& state) {
  uint32_t val = 1 << state.range(0);
  std::string str;
  for (auto _ : state) {
    str.clear();
    Varint::Append32(&str, val);
  }
}

BENCHMARK(BM_Append32)
    ->Arg(1)   // Fast path
    ->Arg(7);  // Slow path

static void BM_Append64(benchmark::State& state) {
  uint64_t val = 1LL << state.range(0);
  std::string str;
  for (auto _ : state) {
    str.clear();
    Varint::Append64(&str, val);
  }
}

BENCHMARK(BM_Append64)
    ->Arg(0)    // Fast path
    ->Arg(7)    // Mid path
    ->Arg(33);  // Slow path

// Benchmark with random-length values
static void BM_Append32_Random(benchmark::State& state) {
  ACMRandom rnd(301);
  std::vector<uint32_t> vals;
  constexpr int kBatchSize = 1024;
  for (int i = 0; i < kBatchSize; i++) {
    const int bytes = absl::Uniform<int32_t>(rnd, 0, Varint::kMax32) + 1;
    const int shift = std::min(7 * bytes - 1, 31);
    vals.push_back(uint32_t{1} << shift);
  }
  std::string str;
  str.reserve(Varint::kMax32);
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint32_t v : vals) {
      str.clear();
      Varint::Append32(&str, v);
    }
  }
}
BENCHMARK(BM_Append32_Random);

// Benchmark with random-length values
static void BM_Append64_Random(benchmark::State& state) {
  ACMRandom rnd(301);
  std::vector<uint64_t> vals;
  constexpr int kBatchSize = 1024;
  for (int i = 0; i < kBatchSize; i++) {
    const int bytes = absl::Uniform<int32_t>(rnd, 0, Varint::kMax64) + 1;
    const int shift = std::min(7 * bytes - 1, 63);
    vals.push_back(uint64_t{1} << shift);
  }
  std::string str;
  str.reserve(Varint::kMax64);
  while (state.KeepRunningBatch(kBatchSize)) {
    for (uint64_t v : vals) {
      str.clear();
      Varint::Append64(&str, v);
    }
  }
}
BENCHMARK(BM_Append64_Random);
