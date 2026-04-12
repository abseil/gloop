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

#include "gloop/util/gtl/unaligned.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/casts.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash_testing.h"
#include "absl/numeric/int128.h"
#include "absl/types/span.h"
#include "gloop/gloop_test.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/gtl/requires.h"
#include "gloop/util/gtl/unaligned_internal.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

class NotDefaultConstructible {
 public:
  explicit NotDefaultConstructible(int x) : x_(x) {}
  NotDefaultConstructible(const NotDefaultConstructible&) = default;
  NotDefaultConstructible& operator=(const NotDefaultConstructible&) = default;
  NotDefaultConstructible(NotDefaultConstructible&&) = default;
  NotDefaultConstructible& operator=(NotDefaultConstructible&&) = default;
  int value() const { return x_; }

 private:
  int x_;
};

TEST(NotDefaultConstructible, Traits) {
  EXPECT_FALSE(std::is_default_constructible_v<NotDefaultConstructible>);
  EXPECT_FALSE(
      std::is_trivially_default_constructible_v<NotDefaultConstructible>);
  EXPECT_TRUE(std::is_trivially_copy_constructible_v<NotDefaultConstructible>);
  EXPECT_TRUE(std::is_trivially_destructible_v<NotDefaultConstructible>);
}

class UserDefaultConstructible {
 public:
  UserDefaultConstructible() : x_(5) {}
  explicit UserDefaultConstructible(int x) : x_(x) {}
  UserDefaultConstructible(const UserDefaultConstructible&) = default;
  UserDefaultConstructible& operator=(const UserDefaultConstructible&) =
      default;
  int value() const { return x_; }

 private:
  int x_;
};

TEST(UserDefaultConstructible, Traits) {
  EXPECT_TRUE(std::is_default_constructible_v<UserDefaultConstructible>);
  EXPECT_FALSE(
      std::is_trivially_default_constructible_v<UserDefaultConstructible>);
  EXPECT_TRUE(std::is_trivially_copy_constructible_v<UserDefaultConstructible>);
  EXPECT_TRUE(std::is_trivially_destructible_v<UserDefaultConstructible>);
}

TEST(Unaligned, IsDefaultConstructible) {
  EXPECT_TRUE(std::is_default_constructible_v<Unaligned<int>>);
  EXPECT_TRUE(
      std::is_default_constructible_v<Unaligned<UserDefaultConstructible>>);
  EXPECT_FALSE(
      std::is_default_constructible_v<Unaligned<NotDefaultConstructible>>);
}

TEST(Unaligned, IsTriviallyDefaultConstructible) {
  EXPECT_TRUE(std::is_trivially_default_constructible_v<Unaligned<int>>);
  EXPECT_FALSE(std::is_trivially_default_constructible_v<
               Unaligned<UserDefaultConstructible>>);
  EXPECT_FALSE(std::is_trivially_default_constructible_v<
               Unaligned<NotDefaultConstructible>>);
}

TEST(UnalignedLoad, SrcPtrMustBeCharPtrUnsignedCharPtrBytePtrOrVoidPtr) {
  EXPECT_TRUE(gtl::Requires<const void*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
  EXPECT_TRUE(gtl::Requires<const char*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
  EXPECT_TRUE(gtl::Requires<const unsigned char*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
  EXPECT_TRUE(gtl::Requires<const std::byte*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));

  EXPECT_FALSE(gtl::Requires<const double*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
  EXPECT_FALSE(gtl::Requires<const float*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
  EXPECT_FALSE(gtl::Requires<const int*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
  EXPECT_FALSE(gtl::Requires<const uint64_t*>(
      [](auto&& x) -> decltype(UnalignedLoad<int>(x)) {}));
}

TEST(UnalignedLoad, Char) {
  std::byte data{0xfe};
  EXPECT_EQ(UnalignedLoad<char>(&data), static_cast<char>(0xfe));
}

TEST(UnalignedLoad, Int) {
  unsigned char data[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_THAT(UnalignedLoad<int>(&data[0]), 0x44332211);
    EXPECT_THAT(UnalignedLoad<int>(&data[1]), 0x55443322);
  } else {
    EXPECT_THAT(UnalignedLoad<int>(&data[0]), 0x11223344);
    EXPECT_THAT(UnalignedLoad<int>(&data[1]), 0x22334455);
  }
}

TEST(UnalignedLoad, ConstVoidPtrSource) {
  unsigned char data[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
  const void* data_0 = &data[0];
  const void* data_1 = &data[1];
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_THAT(UnalignedLoad<int>(data_0), 0x44332211);
    EXPECT_THAT(UnalignedLoad<int>(data_1), 0x55443322);
  } else {
    EXPECT_THAT(UnalignedLoad<int>(data_0), 0x11223344);
    EXPECT_THAT(UnalignedLoad<int>(data_1), 0x22334455);
  }
}

TEST(UnalignedLoad, NotDefaultConstructible) {
  unsigned char data[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_THAT(UnalignedLoad<NotDefaultConstructible>(&data[0]).value(),
                0x44332211);
    EXPECT_THAT(UnalignedLoad<NotDefaultConstructible>(&data[1]).value(),
                0x55443322);
  } else {
    EXPECT_THAT(UnalignedLoad<NotDefaultConstructible>(&data[0]).value(),
                0x11223344);
    EXPECT_THAT(UnalignedLoad<NotDefaultConstructible>(&data[1]).value(),
                0x22334455);
  }
}

TEST(UnalignedLoad, Uint64t) {
  char data[13] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                   0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_THAT(UnalignedLoad<uint64_t>(&data[0]), 0x0706050403020100);
    EXPECT_THAT(UnalignedLoad<uint64_t>(&data[5]), 0x0c0b0a0908070605);
  } else {
    EXPECT_THAT(UnalignedLoad<uint64_t>(&data[0]), 0x0001020304050607);
    EXPECT_THAT(UnalignedLoad<uint64_t>(&data[5]), 0x05060708090a0b0c);
  }
}

TEST(UnalignedLoad, AbslUint128) {
  char data[20] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                   0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13};
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_EQ(UnalignedLoad<absl::uint128>(&data[0]),
              absl::MakeUint128(/*high=*/0x0f0e0d0c0b0a0908,
                                /*low=*/0x0706050403020100));
    EXPECT_EQ(UnalignedLoad<absl::uint128>(&data[4]),
              absl::MakeUint128(/*high=*/0x131211100f0e0d0c,
                                /*low=*/0x0b0a090807060504));
  } else {
    EXPECT_EQ(UnalignedLoad<absl::uint128>(&data[0]),
              absl::MakeUint128(/*high=*/0x0001020304050607,
                                /*low=*/0x08090a0b0c0d0e0f));
    EXPECT_EQ(UnalignedLoad<absl::uint128>(&data[4]),
              absl::MakeUint128(/*high=*/0x0405060708090a0b,
                                /*low=*/0x0c0d0e0f10111213));
  }
}

TEST(UnalignedLoad, Double) {
  char data[9];
  const double approximate_pi = 3.14159;
  UnalignedStore<double>(approximate_pi, &data[0]);
  EXPECT_DOUBLE_EQ(UnalignedLoad<double>(&data[0]), approximate_pi);
  EXPECT_EQ(UnalignedLoad<uint64_t>(&data[0]),
            absl::bit_cast<uint64_t>(approximate_pi));

  const double approximate_e = 2.71828;
  UnalignedStore<double>(approximate_e, &data[1]);
  EXPECT_DOUBLE_EQ(UnalignedLoad<double>(&data[1]), approximate_e);
  EXPECT_EQ(UnalignedLoad<uint64_t>(&data[1]),
            absl::bit_cast<uint64_t>(approximate_e));
}

TEST(UnalignedLoad, Float) {
  char data[9];
  const float approximate_pi = 3.14159f;
  UnalignedStore<float>(approximate_pi, &data[0]);
  EXPECT_DOUBLE_EQ(UnalignedLoad<float>(&data[0]), approximate_pi);
  EXPECT_EQ(UnalignedLoad<uint32_t>(&data[0]),
            absl::bit_cast<uint32_t>(approximate_pi));

  const float approximate_e = 2.71828f;
  UnalignedStore<float>(approximate_e, &data[1]);
  EXPECT_DOUBLE_EQ(UnalignedLoad<float>(&data[1]), approximate_e);
  EXPECT_EQ(UnalignedLoad<uint32_t>(&data[1]),
            absl::bit_cast<uint32_t>(approximate_e));
}

TEST(UnalignedLoad, ArrayOfInt) {
  char data[12] = {0x11, 0x2f, 0x33, 0x44, 0x55, 0x66,
                   0x77, 0x8f, 0x99, 0xaa, 0xbb, 0xcc};
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_THAT((UnalignedLoad<std::array<int, 3>>(&data[0])),
                ElementsAre(0x44332f11, 0x8f776655, 0xccbbaa99));
    EXPECT_THAT((UnalignedLoad<std::array<int, 2>>(&data[1])),
                ElementsAre(0x5544332f, 0x998f7766));
  } else {
    EXPECT_THAT((UnalignedLoad<std::array<int, 3>>(&data[0])),
                ElementsAre(0x112f3344, 0x5566778f, 0x99aabbcc));
    EXPECT_THAT((UnalignedLoad<std::array<int, 2>>(&data[1])),
                ElementsAre(0x2f334455, 0x66778f99));
  }
}

TEST(UnalignedStore, DestPtrMustBeCharPtrUnsignedCharPtrBytePtrOrVoidPtr) {
  EXPECT_TRUE(gtl::Requires<void*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
  EXPECT_TRUE(gtl::Requires<char*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
  EXPECT_TRUE(gtl::Requires<unsigned char*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
  EXPECT_TRUE(gtl::Requires<std::byte*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));

  EXPECT_FALSE(gtl::Requires<double*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
  EXPECT_FALSE(gtl::Requires<float*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
  EXPECT_FALSE(gtl::Requires<int*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
  EXPECT_FALSE(gtl::Requires<uint64_t*>(
      [](auto&& x) -> decltype(UnalignedStore<int>(100, x)) {}));
}

TEST(UnalignedStore, Char) {
  char data;
  UnalignedStore<char>(0xff, &data);
  EXPECT_EQ(data, static_cast<char>(0xff));
}

TEST(UnalignedStore, VoidPtrDest) {
  char data;
  void* data_as_void_ptr = &data;
  UnalignedStore<char>(0xff, data_as_void_ptr);
  EXPECT_EQ(data, static_cast<char>(0xff));
}

TEST(UnalignedStore, VoidPtrDest2) {
  int data0 = 12345678;
  float data1;
  int data2;
  void* data1_as_void_ptr = &data1;
  void* data2_as_void_ptr = &data2;
  UnalignedStore<int>(data0, data1_as_void_ptr);
  UnalignedStore<float>(data1, data2_as_void_ptr);
  EXPECT_EQ(data2, data0);
}

TEST(UnalignedStore, Int) {
  char data[5];
  if constexpr (LittleEndian::IsLittleEndian()) {
    UnalignedStore<int>(0x44332211, &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 4),
                ElementsAre(0x11, 0x22, 0x33, 0x44));

    UnalignedStore<int>(0x708090a0, &data[1]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 5),
                ElementsAre(0x11, 0xa0, 0x90, 0x80, 0x70));
  } else {
    UnalignedStore<int>(0x44332211, &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 4),
                ElementsAre(0x44, 0x33, 0x22, 0x11));

    UnalignedStore<int>(0x708090a0, &data[1]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 5),
                ElementsAre(0x44, 0x70, 0x80, 0x90, 0xa0));
  }
}

TEST(UnalignedStore, NotDefaultConstructible) {
  char data[5];
  if constexpr (LittleEndian::IsLittleEndian()) {
    UnalignedStore<NotDefaultConstructible>(NotDefaultConstructible(0x44332211),
                                            &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 4),
                ElementsAre(0x11, 0x22, 0x33, 0x44));

    UnalignedStore<NotDefaultConstructible>(NotDefaultConstructible(0x708090a0),
                                            &data[1]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 5),
                ElementsAre(0x11, 0xa0, 0x90, 0x80, 0x70));
  } else {
    UnalignedStore<NotDefaultConstructible>(NotDefaultConstructible(0x44332211),
                                            &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 4),
                ElementsAre(0x44, 0x33, 0x22, 0x11));

    UnalignedStore<NotDefaultConstructible>(NotDefaultConstructible(0x708090a0),
                                            &data[1]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 5),
                ElementsAre(0x44, 0x70, 0x80, 0x90, 0xa0));
  }
}

TEST(UnalignedStore, Uint64t) {
  char data[13];
  if constexpr (LittleEndian::IsLittleEndian()) {
    UnalignedStore<uint64_t>(0x0123456789abcdefULL, &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 8),
                ElementsAre(0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01));

    UnalignedStore<uint64_t>(0xff23456789abcdefULL, &data[5]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 13),
                ElementsAre(0xef, 0xcd, 0xab, 0x89, 0x67, 0xef, 0xcd, 0xab,
                            0x89, 0x67, 0x45, 0x23, 0xff));
  } else {
    UnalignedStore<uint64_t>(0x0123456789abcdefULL, &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 8),
                ElementsAre(0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef));

    UnalignedStore<uint64_t>(0xff23456789abcdefULL, &data[5]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 13),
                ElementsAre(0x01, 0x23, 0x45, 0x67, 0x89, 0xff, 0x23, 0x45,
                            0x67, 0x89, 0xab, 0xcd, 0xef));
  }
}

TEST(UnalignedStore, AbslUint128) {
  char data[20];
  if constexpr (LittleEndian::IsLittleEndian()) {
    UnalignedStore<absl::uint128>(absl::MakeUint128(/*high=*/0x7777000100020003,
                                                    /*low=*/0x7fff000a000b000c),
                                  &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 16),
                ElementsAre(0x0c, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0xff, 0x7f,
                            0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x77, 0x77));

    UnalignedStore<absl::uint128>(absl::MakeUint128(/*high=*/0x7888001100220033,
                                                    /*low=*/0x7fff0ffa0ffb0ffc),
                                  &data[4]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 20),
                ElementsAre(0x0c, 0x00, 0x0b, 0x00, 0xfc, 0x0f, 0xfb, 0x0f,
                            0xfa, 0x0f, 0xff, 0x7f, 0x33, 0x00, 0x22, 0x00,
                            0x11, 0x00, 0x88, 0x78));
  } else {
    UnalignedStore<absl::uint128>(absl::MakeUint128(/*high=*/0x7777000100020003,
                                                    /*low=*/0x7fff000a000b000c),
                                  &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 16),
                ElementsAre(0x77, 0x77, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                            0x7f, 0xff, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x0c));

    UnalignedStore<absl::uint128>(absl::MakeUint128(/*high=*/0x7888001100220033,
                                                    /*low=*/0x7fff0ffa0ffb0ffc),
                                  &data[4]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 20),
                ElementsAre(0x77, 0x77, 0x00, 0x01, 0x78, 0x88, 0x00, 0x11,
                            0x00, 0x22, 0x00, 0x33, 0x7f, 0xff, 0x0f, 0xfa,
                            0x0f, 0xfb, 0x0f, 0xfc));
  }
}

TEST(UnalignedStore, Double) {
  char data1[15];
  char data2[15];
  const double approximate_pi = 3.14159;
  UnalignedStore<double>(approximate_pi, &data1[0]);
  UnalignedStore<uint64_t>(absl::bit_cast<uint64_t>(approximate_pi), &data2[0]);
  EXPECT_THAT(absl::Span<const char>(&data1[0], 8),
              ElementsAreArray(absl::Span<const char>(&data2[0], 8)));

  const double approximate_e = 2.71828;
  UnalignedStore<double>(approximate_e, &data1[7]);
  UnalignedStore<uint64_t>(absl::bit_cast<uint64_t>(approximate_e), &data2[7]);
  EXPECT_THAT(absl::Span<const char>(&data1[0], 15),
              ElementsAreArray(absl::Span<const char>(&data2[0], 15)));
}

TEST(UnalignedStore, Float) {
  char data1[6];
  char data2[6];
  const float approximate_pi = 3.14159f;
  UnalignedStore<float>(approximate_pi, &data1[0]);
  UnalignedStore<uint32_t>(absl::bit_cast<uint32_t>(approximate_pi), &data2[0]);
  EXPECT_THAT(absl::Span<const char>(&data1[0], 4),
              ElementsAreArray(absl::Span<const char>(&data2[0], 4)));

  const float approximate_e = 2.71828f;
  UnalignedStore<float>(approximate_e, &data1[2]);
  UnalignedStore<uint32_t>(absl::bit_cast<uint32_t>(approximate_e), &data2[2]);
  EXPECT_THAT(absl::Span<const char>(&data1[0], 6),
              ElementsAreArray(absl::Span<const char>(&data2[0], 6)));
}

TEST(UnalignedStore, ArrayOfInt) {
  char data[12];
  if constexpr (LittleEndian::IsLittleEndian()) {
    UnalignedStore<std::array<int, 3>>(
        std::array<int, 3>({0x44332211, 0x78776655, 0x7cbbaa99}), &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 12),
                ElementsAre(0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x78,
                            0x99, 0xaa, 0xbb, 0x7c));

    UnalignedStore<std::array<int, 2>>(
        std::array<int, 2>({0x7f00ff00, 0x7fff0000}), &data[1]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 12),
                ElementsAre(0x11, 0x00, 0xff, 0x00, 0x7f, 0x00, 0x00, 0xff,
                            0x7f, 0xaa, 0xbb, 0x7c));
  } else {
    UnalignedStore<std::array<int, 3>>(
        std::array<int, 3>({0x44332211, 0x78776655, 0x7cbbaa99}), &data[0]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 12),
                ElementsAre(0x44, 0x33, 0x22, 0x11, 0x78, 0x77, 0x66, 0x55,
                            0x7c, 0xbb, 0xaa, 0x99));

    UnalignedStore<std::array<int, 2>>(
        std::array<int, 2>({0x7f00ff00, 0x7fff0000}), &data[1]);
    EXPECT_THAT(absl::Span<const char>(&data[0], 12),
                ElementsAre(0x44, 0x7f, 0x00, 0xff, 0x00, 0x7f, 0xff, 0x00,
                            0x00, 0xbb, 0xaa, 0x99));
  }
}

TEST(Unaligned, Char) {
  Unaligned<char> u(0xff);
  EXPECT_EQ(u.Load(), static_cast<char>(0xff));
  u.Store(0xab);
  EXPECT_EQ(u.Load(), static_cast<char>(0xab));
}

TEST(Unaligned, Offset) {
  struct Unpadded {
    char a;
    Unaligned<int64_t> b;
    char c;
    Unaligned<absl::int128> d;
    char e[5];
    Unaligned<int16_t> f;
  };
  EXPECT_EQ(sizeof(Unpadded), 1 + 8 + 1 + 16 + 5 + 2);
  EXPECT_EQ(offsetof(Unpadded, a), 0);
  EXPECT_EQ(offsetof(Unpadded, b), 1);
  EXPECT_EQ(offsetof(Unpadded, c), 1 + 8);
  EXPECT_EQ(offsetof(Unpadded, d), 1 + 8 + 1);
  EXPECT_EQ(offsetof(Unpadded, e), 1 + 8 + 1 + 16);
  EXPECT_EQ(offsetof(Unpadded, f), 1 + 8 + 1 + 16 + 5);
}

TEST(Unaligned, UserDefaultConstructible) {
  EXPECT_FALSE(
      std::is_trivially_default_constructible_v<UserDefaultConstructible>);
  EXPECT_TRUE(std::is_default_constructible_v<UserDefaultConstructible>);
  Unaligned<UserDefaultConstructible> u;
  EXPECT_EQ(u.Load().value(), UserDefaultConstructible().value());
  u.Store(UserDefaultConstructible(123));
  EXPECT_EQ(u.Load().value(), 123);
}

TEST(Unaligned, NotDefaultConstructible) {
  EXPECT_FALSE(std::is_default_constructible_v<NotDefaultConstructible>);
  Unaligned<NotDefaultConstructible> u(NotDefaultConstructible(100));
  EXPECT_EQ(u.Load().value(), 100);
  u.Store(NotDefaultConstructible(123));
  EXPECT_EQ(u.Load().value(), 123);
}

struct PackedTrivial {
  Unaligned<double> a;
  Unaligned<int> b;
  char c;
};

template <typename T>
class TrivialUnalignedTest : public ::testing::Test {};
using TrivialTypes = ::testing::Types<char, int, int64_t, float, double,
                                      std::array<int, 3>, PackedTrivial>;
TYPED_TEST_SUITE(TrivialUnalignedTest, TrivialTypes);

TYPED_TEST(TrivialUnalignedTest, Traits) {
  using UnalignedType = Unaligned<TypeParam>;
  EXPECT_TRUE(std::is_trivial_v<TypeParam>);
  EXPECT_TRUE(std::is_trivial_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_default_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copy_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copy_assignable_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_move_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_move_assignable_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_destructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copyable_v<UnalignedType>);
  EXPECT_TRUE(internal_unaligned::IsBitCastableTo<UnalignedType>);
}

TEST(Unaligned, UserDefaultConstructibleTraits) {
  using UnalignedType = Unaligned<UserDefaultConstructible>;
  EXPECT_FALSE(std::is_trivial_v<UserDefaultConstructible>);
  EXPECT_FALSE(std::is_trivial_v<UnalignedType>);
  EXPECT_FALSE(std::is_trivially_default_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_default_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copy_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copy_assignable_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_move_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_move_assignable_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_destructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copyable_v<UnalignedType>);
  EXPECT_TRUE(internal_unaligned::IsBitCastableTo<UnalignedType>);
}

TEST(Unaligned, NotDefaultConstructibleTraits) {
  using UnalignedType = Unaligned<NotDefaultConstructible>;
  EXPECT_FALSE(std::is_trivial_v<NotDefaultConstructible>);
  EXPECT_FALSE(std::is_trivial_v<UnalignedType>);
  EXPECT_FALSE(std::is_trivially_default_constructible_v<UnalignedType>);
  EXPECT_FALSE(std::is_default_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copy_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copy_assignable_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_move_constructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_move_assignable_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_destructible_v<UnalignedType>);
  EXPECT_TRUE(std::is_trivially_copyable_v<UnalignedType>);
  EXPECT_TRUE(internal_unaligned::IsBitCastableTo<UnalignedType>);
}

TEST(Unaligned, StructOfInt64tAndInt32t) {
  struct Unpadded {
    Unaligned<int64_t> x;
    Unaligned<int32_t> y;
  };
  EXPECT_EQ(sizeof(Unpadded), 12);
  EXPECT_EQ(alignof(Unpadded), 1);
  EXPECT_EQ(sizeof(std::array<Unpadded, 3>), 36);
}

TEST(Unaligned, StructSerializationAndParsing) {
  struct Unpadded {
    Unaligned<uint32_t> x;
    unsigned char c;
  };
  ASSERT_EQ(sizeof(Unpadded), 5);

  char buffer[8];
  std::memset(buffer, 1, sizeof(buffer));
  EXPECT_THAT(absl::Span<const char>(&buffer[0], 8),
              ElementsAre(0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01));

  auto u1 = UnalignedLoad<Unpadded>(buffer);
  EXPECT_EQ(u1.x.Load(), 0x01010101);
  EXPECT_EQ(u1.c, 0x01);
  u1.x.Store(0xaabbccdd);
  u1.c = 0xff;
  UnalignedStore<Unpadded>(u1, buffer + 2);
  if constexpr (LittleEndian::IsLittleEndian()) {
    EXPECT_THAT(absl::Span<const char>(&buffer[0], 8),
                ElementsAre(0x01, 0x01, 0xdd, 0xcc, 0xbb, 0xaa, 0xff, 0x01));
  } else {
    EXPECT_THAT(absl::Span<const char>(&buffer[0], 8),
                ElementsAre(0x01, 0x01, 0xaa, 0xbb, 0xcc, 0xdd, 0xff, 0x01));
  }
}

TEST(Unaligned, MixingUnalignedAndRegular) {
  struct MyStruct {
    Unaligned<int64_t> a;
    int32_t b;
  };
  // Alignment requirement of a struct is dictated by the largest alignment
  // requirement of its member. In this case that is the member `b`.
  EXPECT_EQ(alignof(MyStruct), alignof(int32_t));
  EXPECT_EQ(sizeof(MyStruct), sizeof(int64_t) + sizeof(int32_t));
}

TEST(Unaligned, DoubleZerosEquality) {
  EXPECT_EQ(gtl::Unaligned<double>(-0.0), gtl::Unaligned<double>(0.0));
}

TEST(Unaligned, Constexpr) {
  constexpr Unaligned<int> u1(1);
  constexpr Unaligned<int> u2(2);
  static_assert(u1.Load() == 1);
  static_assert(u2.Load() == 2);
  static_assert(u1 != u2);
  static_assert(!(u1 == u2));
  static_assert(u1 < u2);
  static_assert(u2 > u1);
  static_assert(u1 <= u2);
  static_assert(u2 >= u1);
  static_assert((u1 <=> u2) == std::strong_ordering::less);
  static_assert((u2 <=> u1) == std::strong_ordering::greater);
  constexpr Unaligned<int> u1_copy = u1;
  static_assert(u1 == u1_copy);
  static_assert((u1 <=> u1_copy) == std::strong_ordering::equal);
}

TEST(Unaligned, ConstexprStore) {
  constexpr Unaligned<int> u42 = []() {
    Unaligned<int> u(1);
    u.Store(42);
    return u;
  }();
  static_assert(u42.Load() == 42);
}

TEST(Unaligned, HashCorrectness) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      gtl::Unaligned<double>(-0.0),
      gtl::Unaligned<double>(0.0),
      gtl::Unaligned<double>(1.0),
      gtl::Unaligned<double>(10.0),
  }));
}

TEST(Unaligned, AsFlatHashMapKey) {
  absl::flat_hash_map<Unaligned<int64_t>, int> map;
  map.emplace(1, 2);
  map[gtl::Unaligned<int64_t>(3)] = 4;

  EXPECT_THAT(map, UnorderedElementsAre(Pair(Unaligned<int64_t>(1), 2),
                                        Pair(Unaligned<int64_t>(3), 4)));

  EXPECT_NE(map.find(Unaligned<int64_t>(3)), map.end());
  EXPECT_EQ(map.find(Unaligned<int64_t>(5)), map.end());
}

}  // namespace

// Tests for internal stuff:
namespace internal_unaligned {
namespace {

class NotTriviallyCopyable {
 public:
  NotTriviallyCopyable() = default;
  explicit NotTriviallyCopyable(int x) : x_(x) {}
  NotTriviallyCopyable(const NotTriviallyCopyable& o) { x_ = o.x_; }
  NotTriviallyCopyable& operator=(const NotTriviallyCopyable& o) {
    // '* 1' is there to make ClangTidy happy. Otherwise the implementation
    // becomes exactly what the '= default' would do and thus ClangTidy
    // complains about it. http://screen/9Cv6KuKiQFmGwhC
    x_ = o.x_ * 1;
    return *this;
  }
  int value() const { return x_; }

 private:
  int x_;
};

TEST(NotTriviallyCopyable, Traits) {
  EXPECT_TRUE(std::is_trivially_constructible_v<NotTriviallyCopyable>);
  EXPECT_FALSE(std::is_trivially_copyable_v<NotTriviallyCopyable>);
  EXPECT_TRUE(std::is_trivially_destructible_v<NotTriviallyCopyable>);
}

TEST(IsBitCastableTo, VariousTypes) {
  EXPECT_TRUE(IsBitCastableTo<char>);
  EXPECT_TRUE(IsBitCastableTo<int>);
  EXPECT_TRUE(IsBitCastableTo<uint64_t>);
  EXPECT_TRUE(IsBitCastableTo<double>);
  EXPECT_TRUE(IsBitCastableTo<absl::uint128>);
  EXPECT_TRUE((IsBitCastableTo<std::array<int, 3>>));
  EXPECT_TRUE(IsBitCastableTo<NotDefaultConstructible>);

  EXPECT_FALSE(IsBitCastableTo<std::string>);
  EXPECT_FALSE(IsBitCastableTo<std::vector<int>>);
  EXPECT_FALSE(IsBitCastableTo<NotTriviallyCopyable>);

  EXPECT_TRUE(IsBitCastableTo<Unaligned<int>>);
  EXPECT_TRUE(IsBitCastableTo<Unaligned<UserDefaultConstructible>>);
  EXPECT_TRUE(IsBitCastableTo<Unaligned<NotDefaultConstructible>>);
}

}  // namespace
}  // namespace internal_unaligned
}  // namespace gtl
