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

// Unit tests for cpu_set utility functions.

#include "gloop/util/os/core/cpu_set.h"

#include <cstdint>

#include "gloop/util/os/core/cpu_set_test_util.h"  // IWYU pragma: keep
#include "gloop/util/os/core/test_proto.pb.h"
#include "gtest/gtest.h"

namespace util_os_core {

TEST(CpuSet, UInt64ToCpuSet) {
  cpu_set_t set_0;
  UInt64ToCpuSet(0x0, &set_0);
  EXPECT_EQ(0, CpuSetCountCpus(&set_0));

  cpu_set_t set_1;
  UInt64ToCpuSet(0x1, &set_1);
  EXPECT_EQ(1, CpuSetCountCpus(&set_1));
  EXPECT_TRUE(CpuSetContains(0, &set_1));

  cpu_set_t set_10;
  UInt64ToCpuSet(0x10, &set_10);
  EXPECT_EQ(1, CpuSetCountCpus(&set_10));
  EXPECT_TRUE(CpuSetContains(4, &set_10));

  cpu_set_t set_101;
  UInt64ToCpuSet(0x101, &set_101);
  EXPECT_EQ(2, CpuSetCountCpus(&set_101));
  EXPECT_TRUE(CpuSetContains(0, &set_101));
  EXPECT_TRUE(CpuSetContains(8, &set_101));
  // Test the alternate API.
  EXPECT_EQ(set_101, UInt64ToCpuSet(0x101));

  cpu_set_t set_90010001;
  UInt64ToCpuSet(0x90010001, &set_90010001);
  EXPECT_EQ(4, CpuSetCountCpus(&set_90010001));
  EXPECT_TRUE(CpuSetContains(0, &set_90010001));
  EXPECT_TRUE(CpuSetContains(16, &set_90010001));
  EXPECT_TRUE(CpuSetContains(28, &set_90010001));
  EXPECT_TRUE(CpuSetContains(31, &set_90010001));
}

TEST(CpuSet, CpuSetToHexString) {
  cpu_set_t set_0;
  CpuSetClear(&set_0);
  EXPECT_EQ("0x0", CpuSetToHexString(&set_0, true));
  EXPECT_EQ("0", CpuSetToHexString(&set_0, false));
  EXPECT_EQ("0x0", CpuSetToHexString(&set_0));
  EXPECT_EQ("0x0", CpuSetToHexString(set_0));

  cpu_set_t set_9376;
  UInt64ToCpuSet(0x9376, &set_9376);
  EXPECT_EQ("0x9376", CpuSetToHexString(&set_9376, true));
  EXPECT_EQ("9376", CpuSetToHexString(&set_9376, false));
  EXPECT_EQ("0x9376", CpuSetToHexString(&set_9376));
  EXPECT_EQ("0x9376", CpuSetToHexString(set_9376));

  cpu_set_t set_f;
  UInt64ToCpuSet(0xf, &set_f);
  EXPECT_EQ("0xf", CpuSetToHexString(set_f));

  cpu_set_t set_fa;
  UInt64ToCpuSet(0xfa, &set_fa);
  EXPECT_EQ("0xfa", CpuSetToHexString(&set_fa));
  // Test the alternate API.
  EXPECT_EQ("0xfa", CpuSetToHexString(set_fa));

  cpu_set_t set_fedcba00;
  UInt64ToCpuSet(0xfedcba00, &set_fedcba00);
  EXPECT_EQ("0xfedcba00", CpuSetToHexString(set_fedcba00));

  cpu_set_t set_ones;
  UInt64ToCpuSet(uint64_t{0x1111111111111111}, &set_ones);
  EXPECT_EQ("0x1111111111111111", CpuSetToHexString(set_ones));

  cpu_set_t set_zeros_and_ones;
  UInt64ToCpuSet(uint64_t{0x0001111111111111}, &set_zeros_and_ones);
  EXPECT_EQ("0x1111111111111", CpuSetToHexString(set_zeros_and_ones));
}

TEST(CpuSet, HexStringToCpuSet) {
  cpu_set_t set;
  CpuSetClear(&set);

  EXPECT_FALSE(HexStringToCpuSet("", &set));
  EXPECT_FALSE(HexStringToCpuSet("not a hex string", &set));
  // Test the alternate API.
  EXPECT_DEATH(HexStringToCpuSet(""), "");
  EXPECT_DEATH(HexStringToCpuSet("not a hex string"), "");

  EXPECT_TRUE(HexStringToCpuSet("0", &set));
  EXPECT_EQ(0x0, set);

  EXPECT_FALSE(HexStringToCpuSet("0x", &set));

  EXPECT_TRUE(HexStringToCpuSet("0x0", &set));
  EXPECT_EQ(0x0, set);

  EXPECT_TRUE(HexStringToCpuSet("0x00000000000000000000000000000000", &set));
  EXPECT_EQ(0x0, set);

  EXPECT_TRUE(HexStringToCpuSet("1", &set));
  EXPECT_EQ(0x1, set);

  EXPECT_TRUE(HexStringToCpuSet("3", &set));
  EXPECT_EQ(0x3, set);

  EXPECT_TRUE(HexStringToCpuSet("93", &set));
  EXPECT_EQ(0x93, set);

  EXPECT_TRUE(HexStringToCpuSet("fedcba00", &set));
  EXPECT_EQ(0xfedcba00, set);
  // Test the alternate API.
  EXPECT_EQ(0xfedcba00, HexStringToCpuSet("fedcba00"));

  EXPECT_TRUE(HexStringToCpuSet("0xfedcba00", &set));
  EXPECT_EQ(0xfedcba00, set);

  EXPECT_TRUE(HexStringToCpuSet("0Xfedcba00", &set));
  EXPECT_EQ(0xfedcba00, set);

  EXPECT_TRUE(HexStringToCpuSet("10000000000000000000000000000000", &set));
  EXPECT_EQ(1, CpuSetCountCpus(&set));
  EXPECT_TRUE(CpuSetContains(124, &set));

  EXPECT_TRUE(
      HexStringToCpuSet("000000010000000100000001000000010000000", &set));
  EXPECT_EQ(4, CpuSetCountCpus(&set));
  EXPECT_TRUE(CpuSetContains(28, &set));
  EXPECT_TRUE(CpuSetContains(60, &set));
  EXPECT_TRUE(CpuSetContains(92, &set));
  EXPECT_TRUE(CpuSetContains(124, &set));
}

TEST(CpuSet, CpuSetClear) {
  cpu_set_t set;

  CPU_ZERO(&set);
  CpuSetInsert(0, &set);
  CpuSetInsert(CPU_SETSIZE - 1, &set);
  EXPECT_EQ(2, CpuSetCountCpus(&set));
  CpuSetClear(&set);
  EXPECT_EQ(0, CpuSetCountCpus(&set));
}

TEST(CpuSet, CpuSetClearSubset) {
  cpu_set_t set_0;
  cpu_set_t set_1;
  cpu_set_t set_maxcpu;
  cpu_set_t set_allcpu;
  cpu_set_t result;
  cpu_set_t cleared;
  cpu_set_t expected;

  CpuSetClear(&set_0);
  UInt64ToCpuSet(0x1, &set_1);
  CpuSetClear(&set_maxcpu);
  CpuSetInsert(CPU_SETSIZE - 1, &set_maxcpu);
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id) {
    CpuSetInsert(cpu_id, &set_allcpu);
  }

  // Test clearing all bits of an empty set.
  CpuSetClearSubset(&set_0, &set_allcpu, &result, &cleared);
  EXPECT_TRUE(CpuSetTestEmpty(&result));
  EXPECT_TRUE(CpuSetTestEmpty(&cleared));

  // Test clearing of no bits from sets with bits.
  CpuSetClearSubset(&set_1, &set_0, &result, &cleared);
  EXPECT_EQ(set_1, result);
  EXPECT_TRUE(CpuSetTestEmpty(&cleared));
  CpuSetClearSubset(&set_maxcpu, &set_0, &result, &cleared);
  EXPECT_EQ(set_maxcpu, result);
  EXPECT_TRUE(CpuSetTestEmpty(&cleared));
  CpuSetClearSubset(&set_allcpu, &set_0, &result, &cleared);
  EXPECT_EQ(set_allcpu, result);
  EXPECT_TRUE(CpuSetTestEmpty(&cleared));

  // Test clearing of all bits from sets with bits.
  CpuSetClearSubset(&set_1, &set_allcpu, &result, &cleared);
  EXPECT_EQ(set_0, result);
  EXPECT_EQ(set_1, cleared);
  CpuSetClearSubset(&set_maxcpu, &set_allcpu, &result, &cleared);
  EXPECT_EQ(set_0, result);
  EXPECT_EQ(set_maxcpu, cleared);
  CpuSetClearSubset(&set_allcpu, &set_allcpu, &result, &cleared);
  EXPECT_TRUE(CpuSetTestEmpty(result));
  EXPECT_EQ(set_allcpu, cleared);

  // Test clearing of bits which don't correspond.
  CpuSetClearSubset(&set_1, &set_maxcpu, &result, &cleared);
  EXPECT_EQ(set_1, result);
  EXPECT_TRUE(CpuSetTestEmpty(&cleared));

  // Test clearing when in and result point to the same memory.
  result = set_allcpu;
  expected = result;
  CpuSetRemove(0x0, &expected);
  CpuSetClearSubset(&result, &set_1, &result, &cleared);
  EXPECT_EQ(expected, result);
  EXPECT_EQ(set_1, cleared);

  // Basic test for simplified form.
  CpuSetClearSubset(&set_1, &set_maxcpu, &result);
  EXPECT_EQ(set_1, result);
}

TEST(CpuSet, CpuSetRemove) {
  cpu_set_t set = UInt64ToCpuSet(1);

  CpuSetRemove(0, &set);
  EXPECT_EQ(0x0, set);
  EXPECT_DEATH(CpuSetRemove(-1, &set), "");
  EXPECT_DEATH(CpuSetRemove(CPU_SETSIZE, &set), "");
}

TEST(CpuSet, CpuSetInsert) {
  cpu_set_t set = CpuSetMakeEmpty();

  CpuSetInsert(0, &set);
  EXPECT_EQ(0x1, set);
  EXPECT_DEATH(CpuSetInsert(-1, &set), "");
  EXPECT_DEATH(CpuSetInsert(CPU_SETSIZE, &set), "");
}

TEST(CpuSet, CpuSetContains) {
  cpu_set_t set = UInt64ToCpuSet(1);

  EXPECT_TRUE(CpuSetContains(0, &set));
  EXPECT_FALSE(CpuSetContains(1, &set));
  EXPECT_DEATH(CpuSetContains(-1, &set), "");
  EXPECT_DEATH(CpuSetContains(CPU_SETSIZE, &set), "");
}

TEST(CpuSet, CpuSetCountCpus) {
  cpu_set_t set = UInt64ToCpuSet(0x5);

  EXPECT_EQ(2, CpuSetCountCpus(&set));
  // Test the alternate API.
  EXPECT_EQ(2, CpuSetCountCpus(set));
}

TEST(CpuSet, CpuSetTestEmpty) {
  cpu_set_t set_0;
  cpu_set_t set_1;
  cpu_set_t set_maxcpu;

  CpuSetClear(&set_0);
  UInt64ToCpuSet(0x1, &set_1);
  CpuSetClear(&set_maxcpu);
  CpuSetInsert(CPU_SETSIZE - 1, &set_maxcpu);

  EXPECT_TRUE(CpuSetTestEmpty(&set_0));
  EXPECT_FALSE(CpuSetTestEmpty(&set_1));
  EXPECT_FALSE(CpuSetTestEmpty(&set_maxcpu));
  // Test the alternate API.
  EXPECT_TRUE(CpuSetTestEmpty(set_0));
  EXPECT_FALSE(CpuSetTestEmpty(set_1));
  EXPECT_FALSE(CpuSetTestEmpty(set_maxcpu));
}

TEST(CpuSet, CpuSetTestEqual) {
  cpu_set_t set_0;
  cpu_set_t set_1;
  cpu_set_t set_maxcpu;

  CpuSetClear(&set_0);
  UInt64ToCpuSet(0x1, &set_1);
  CpuSetClear(&set_maxcpu);
  CpuSetInsert(CPU_SETSIZE - 1, &set_maxcpu);

  EXPECT_TRUE(CpuSetTestEqual(&set_0, &set_0));
  EXPECT_TRUE(CpuSetTestEqual(&set_1, &set_1));
  EXPECT_TRUE(CpuSetTestEqual(&set_maxcpu, &set_maxcpu));
  EXPECT_FALSE(CpuSetTestEqual(&set_0, &set_1));
  EXPECT_FALSE(CpuSetTestEqual(&set_0, &set_maxcpu));
  EXPECT_FALSE(CpuSetTestEqual(&set_1, &set_maxcpu));
  // Test the alternate API.
  EXPECT_TRUE(CpuSetTestEqual(set_0, set_0));
  EXPECT_TRUE(CpuSetTestEqual(set_1, set_1));
  EXPECT_TRUE(CpuSetTestEqual(set_maxcpu, set_maxcpu));
  EXPECT_FALSE(CpuSetTestEqual(set_0, set_1));
  EXPECT_FALSE(CpuSetTestEqual(set_0, set_maxcpu));
  EXPECT_FALSE(CpuSetTestEqual(set_1, set_maxcpu));
  // Test that EXPECT_EQ()/EXPECT_NE() work with the operator overloading.
  EXPECT_EQ(set_0, set_0);
  EXPECT_EQ(set_1, set_1);
  EXPECT_EQ(set_maxcpu, set_maxcpu);
  EXPECT_NE(set_0, set_1);
  EXPECT_NE(set_0, set_maxcpu);
  EXPECT_NE(set_1, set_maxcpu);
  // Test compare-with-integer variants of EXPECT_EQ()/EXPECT_NEW().
  EXPECT_EQ(0x0, set_0);
  EXPECT_EQ(0x1, set_1);
  EXPECT_NE(0x0, set_1);
  EXPECT_NE(0x0, set_maxcpu);
  EXPECT_NE(0x1, set_maxcpu);
  EXPECT_NE(0x2, set_1);
}

TEST(CpuSet, CpuSetCompare) {
  cpu_set_t set_0;
  cpu_set_t set_1;
  cpu_set_t set_2;
  cpu_set_t set_fffffffffffffffe;
  cpu_set_t set_ffffffffffffffff;
  cpu_set_t set_10000000000000000;
  cpu_set_t set_maxcpu;

  CpuSetClear(&set_0);
  UInt64ToCpuSet(0x1, &set_1);
  UInt64ToCpuSet(0x2, &set_2);
  UInt64ToCpuSet(0xfffffffffffffffe, &set_fffffffffffffffe);
  UInt64ToCpuSet(0xffffffffffffffff, &set_ffffffffffffffff);
  CpuSetClear(&set_10000000000000000);
  CpuSetInsert(64, &set_10000000000000000);
  CpuSetClear(&set_maxcpu);
  CpuSetInsert(CPU_SETSIZE - 1, &set_maxcpu);

  EXPECT_EQ(0, CpuSetCompare(&set_0, &set_0));
  EXPECT_EQ(0, CpuSetCompare(&set_1, &set_1));
  EXPECT_EQ(0, CpuSetCompare(&set_2, &set_2));
  EXPECT_EQ(0, CpuSetCompare(&set_fffffffffffffffe, &set_fffffffffffffffe));
  EXPECT_EQ(0, CpuSetCompare(&set_ffffffffffffffff, &set_ffffffffffffffff));
  EXPECT_EQ(0, CpuSetCompare(&set_10000000000000000, &set_10000000000000000));

  EXPECT_EQ(-1, CpuSetCompare(&set_0, &set_1));
  EXPECT_EQ(1, CpuSetCompare(&set_1, &set_0));

  EXPECT_EQ(-1, CpuSetCompare(&set_1, &set_2));
  EXPECT_EQ(1, CpuSetCompare(&set_2, &set_1));

  EXPECT_EQ(-1, CpuSetCompare(&set_fffffffffffffffe, &set_10000000000000000));
  EXPECT_EQ(1, CpuSetCompare(&set_ffffffffffffffff, &set_fffffffffffffffe));

  EXPECT_EQ(-1, CpuSetCompare(&set_ffffffffffffffff, &set_10000000000000000));
  EXPECT_EQ(1, CpuSetCompare(&set_10000000000000000, &set_ffffffffffffffff));

  EXPECT_EQ(-1, CpuSetCompare(&set_10000000000000000, &set_maxcpu));
  EXPECT_EQ(1, CpuSetCompare(&set_maxcpu, &set_10000000000000000));
}

TEST(CpuSet, CpuSetLessThan) {
  cpu_set_t set_0;
  cpu_set_t set_1;

  CpuSetClear(&set_0);
  UInt64ToCpuSet(0x1, &set_1);

  EXPECT_FALSE(CpuSetLessThan()(set_0, set_0));  // 0x0 < 0x0
  EXPECT_FALSE(CpuSetLessThan()(set_1, set_1));  // 0x1 < 0x1

  EXPECT_TRUE(CpuSetLessThan()(set_0, set_1));   // 0x0 < 0x1
  EXPECT_FALSE(CpuSetLessThan()(set_1, set_0));  // 0x1 < 0x0
}

TEST(CpuSet, CpuSetMakeEmpty) {
  cpu_set_t set_zero = CpuSetMakeEmpty();
  EXPECT_TRUE(CpuSetTestEmpty(set_zero));
}

TEST(CpuSet, ProtobufToCpuSet) {
  cpu_set_t set;
  TestProto pb, pb_zero;

  // Test with no words.
  set = ProtobufToCpuSet(pb_zero.cpus());
  EXPECT_TRUE(CpuSetTestEmpty(set));

  // Test with one zero word.
  pb_zero.add_cpus(0x0);
  set = ProtobufToCpuSet(pb_zero.cpus());
  EXPECT_TRUE(CpuSetTestEmpty(set));

  // Test with just one word.
  pb.add_cpus(0x1);
  set = ProtobufToCpuSet(pb.cpus());
  EXPECT_EQ(0x1, set);

  // Test with a second word
  pb.add_cpus(0xdeadbeef);
  set = ProtobufToCpuSet(pb.cpus());
  EXPECT_TRUE(CpuSetContains(64, &set));
  CpuSetRemove(64, &set);
  EXPECT_EQ(0xdeadbeef, set);
}

TEST(CpuSet, CpuSetToProtoConversionsAreReversible) {
  cpu_set_t set;
  TestProto pb;

  // Test with just one word.
  UInt64ToCpuSet(0xdeadbeef, &set);
  CpuSetToProtobuf(set, pb.mutable_cpus());
  EXPECT_EQ(set, ProtobufToCpuSet(pb.cpus()));

  // Add 3 more words
  CpuSetInsert(64, &set);
  CpuSetInsert(255, &set);
  CpuSetInsert(255, &set);
  CpuSetToProtobuf(set, pb.mutable_cpus());
  EXPECT_EQ(set, ProtobufToCpuSet(pb.cpus()));

  // Add a large cpumask
  cpu_set_t mask = HexStringToCpuSet(
      "0x3ffffe000000000000000000000000003ffffe0000000000000000");
  pb.Clear();
  CpuSetToProtobuf(mask, pb.mutable_cpus());
  EXPECT_EQ(mask, ProtobufToCpuSet(pb.cpus()));
}

TEST(CpuSet, CpuSetToProtobuf) {
  cpu_set_t set;
  TestProto pb_zero, pb_one_word, pb_two_words, pb_four_words;
  TestProto pb_written_twice;

  // Test with zero word.
  CpuSetClear(&set);
  CpuSetToProtobuf(set, pb_zero.mutable_cpus());
  EXPECT_EQ(0, pb_one_word.cpus_size());

  // Test with just one word.
  UInt64ToCpuSet(0xdeadbeef, &set);
  CpuSetToProtobuf(set, pb_one_word.mutable_cpus());
  EXPECT_EQ(1, pb_one_word.cpus_size());
  EXPECT_EQ(0xdeadbeef, pb_one_word.cpus(0));

  // Test with a second word which just has the low bit set.
  CpuSetInsert(64, &set);
  CpuSetToProtobuf(set, pb_two_words.mutable_cpus());
  EXPECT_EQ(2, pb_two_words.cpus_size());
  EXPECT_EQ(0x1, pb_two_words.cpus(0));
  EXPECT_EQ(0xdeadbeef, pb_two_words.cpus(1));

  // Test with word2=0 and word3=0x8000000000000004.
  CpuSetInsert(194, &set);
  CpuSetInsert(255, &set);
  CpuSetToProtobuf(set, pb_four_words.mutable_cpus());
  EXPECT_EQ(4, pb_four_words.cpus_size());
  EXPECT_EQ(0x8000000000000004, pb_four_words.cpus(0));
  EXPECT_EQ(0x0, pb_four_words.cpus(1));
  EXPECT_EQ(0x1, pb_four_words.cpus(2));
  EXPECT_EQ(0xdeadbeef, pb_four_words.cpus(3));

  // Test that writing twice results only in one cpu_set_t being stored (i.e.
  // that each write clears the protobuf).
  CpuSetToProtobuf(UInt64ToCpuSet(0xdeadbeef), pb_written_twice.mutable_cpus());
  CpuSetToProtobuf(UInt64ToCpuSet(0xfedbeef), pb_written_twice.mutable_cpus());
  EXPECT_EQ(1, pb_written_twice.cpus_size());
  EXPECT_EQ(0xfedbeef, pb_written_twice.cpus(0));
}

}  // namespace util_os_core
