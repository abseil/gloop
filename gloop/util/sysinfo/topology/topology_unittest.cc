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

// Unit test for system topology

#include "gloop/util/sysinfo/topology/topology.h"

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "absl/base/internal/cpu_detect.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/sysinfo.h"
#include "gloop/testing/production_stub/testvalue.h"
#include "gloop/util/os/core/cpu_set.h"
#include "gloop/util/os/core/cpu_set_test_util.h"  // IWYU pragma: keep
#include "gloop/util/sysinfo/topology/topology_converter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

using absl::base_internal::CpuType;
using util_os_core::CpuSetContains;
using util_os_core::CpuSetCountCpus;
using util_os_core::CpuSetInsert;
using util_os_core::CpuSetMakeEmpty;
using util_os_core::CpuSetRemove;
using util_os_core::HexStringToCpuSet;

using ::testing::UnorderedElementsAre;

ABSL_FLAG(bool, test_system, false, "Print best topology for system");
ABSL_FLAG(std::string, test_datadir, "", "Test data directory");
ABSL_FLAG(std::string, test_data_filter, "",
          "Only run tests against testdata path having this name");

ABSL_FLAG(bool, test_cpuinfo, false,
          "Dump topology generated from /proc/cpuinfo");
ABSL_FLAG(bool, test_siblingmap, false,
          "Dump topology generated from cpu_sibling_map");

ABSL_FLAG(std::string, test_overrides, "",
          "Use the given platform prefix for file overrides");

ABSL_FLAG(bool, update_golden, false,
          "Update golden files rather than checking against them");

const char* kNoTopologyStr = "No topology map generated\n";

// Manually check a set of files to see what output SysTopology generates
void ManualCheck(const std::string& testdata_path,
                 SysTopologyGenerator::Generator generator) {
  SysTopologyGenerator manual_generator(
      (absl::GetFlag(FLAGS_test_overrides).empty()
           ? std::string()
           : testdata_path + absl::GetFlag(FLAGS_test_overrides)));
  SysTopology* st = (manual_generator.*generator)();
  if (st) {
    printf("%s", st->ToString().c_str());
    delete st;
  } else {
    printf("%s", kNoTopologyStr);
  }
  exit(0);
}

// ------------------------------------------------------------------------
// CountCoresTest

cpu_set_t GenerateCpuSetAll() {
  cpu_set_t cpu_set = CpuSetMakeEmpty();
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id) {
    CpuSetInsert(cpu_id, &cpu_set);
  }
  return cpu_set;
}

cpu_set_t GenerateCpuSetEveryOther() {
  cpu_set_t cpu_set = CpuSetMakeEmpty();
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; cpu_id += 2) {
    CpuSetInsert(cpu_id, &cpu_set);
  }
  return cpu_set;
}

void ClearOneCpuFromSet(cpu_set_t* cpu_set) {
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id) {
    if (CpuSetContains(cpu_id, cpu_set)) {
      CpuSetRemove(cpu_id, cpu_set);
      return;
    }
  }
}

class CountCoresTest : public testing::Test {
 protected:
  static const int kMaxCpus;
  static const cpu_set_t kCpuSetAll;
  static const cpu_set_t kCpuSetNone;
  static const cpu_set_t kCpuSetEveryOther;
};

const int CountCoresTest::kMaxCpus = CPU_SETSIZE;
const cpu_set_t CountCoresTest::kCpuSetAll = GenerateCpuSetAll();
const cpu_set_t CountCoresTest::kCpuSetNone = CpuSetMakeEmpty();
const cpu_set_t CountCoresTest::kCpuSetEveryOther = GenerateCpuSetEveryOther();

TEST_F(CountCoresTest, HTNone) {
  std::unique_ptr<SysTopology> topology(
      SysTopology::SimpleTopology(kMaxCpus, SysTopology::HT_NONE));
  EXPECT_EQ(kMaxCpus, topology->CountCores(&kCpuSetAll));
  EXPECT_EQ(0, topology->CountCores(&kCpuSetNone));
  EXPECT_EQ(kMaxCpus / 2, topology->CountCores(&kCpuSetEveryOther));

  {
    std::unique_ptr<SysTopology> topology(SysTopology::SimpleTopology(
        /*num_nodes=*/2, /*per_node_cpus=*/36, SysTopology::HT_NONE,
        /*per_node_mem_kb=*/1024));
    EXPECT_EQ(topology->NumNodes(), 2);
    EXPECT_EQ(topology->NodeCPUs(0), HexStringToCpuSet("0x000000000fffffffff"));
    EXPECT_EQ(topology->NodeCPUs(1), HexStringToCpuSet("0xfffffffff000000000"));
  }
}

TEST_F(CountCoresTest, HTAdjacent) {
  std::unique_ptr<SysTopology> topology(
      SysTopology::SimpleTopology(kMaxCpus, SysTopology::HT_ADJACENT));
  EXPECT_EQ(kMaxCpus / 2, topology->CountCores(&kCpuSetAll));
  EXPECT_EQ(0, topology->CountCores(&kCpuSetNone));
  EXPECT_EQ(kMaxCpus / 2, topology->CountCores(&kCpuSetEveryOther));

  {
    std::unique_ptr<SysTopology> topology(SysTopology::SimpleTopology(
        /*num_nodes=*/2, /*per_node_cpus=*/36, SysTopology::HT_ADJACENT,
        /*per_node_mem_kb=*/1024));
    EXPECT_EQ(topology->NumNodes(), 2);
    EXPECT_EQ(topology->NodeCPUs(0), HexStringToCpuSet("0x000000000fffffffff"));
    EXPECT_EQ(topology->NodeCPUs(1), HexStringToCpuSet("0xfffffffff000000000"));
  }
}

TEST_F(CountCoresTest, NumHyperThreadSiblingsTest) {
  int cpus_per_core = 2;
  std::unique_ptr<SysTopology> topology(SysTopology::SimpleTopology(
      kMaxCpus, SysTopology::HT_ADJACENT, cpus_per_core));
  EXPECT_EQ(kMaxCpus / 2, topology->CountCores(&kCpuSetAll));
  EXPECT_EQ(0, topology->CountCores(&kCpuSetNone));
  EXPECT_EQ(cpus_per_core, topology->NumHyperthreadSiblings());
}

TEST_F(CountCoresTest, HTNonAdjacent) {
  std::unique_ptr<SysTopology> topology(
      SysTopology::SimpleTopology(kMaxCpus, SysTopology::HT_NONADJACENT));
  EXPECT_EQ(kMaxCpus / 2, topology->CountCores(&kCpuSetAll));
  EXPECT_EQ(0, topology->CountCores(&kCpuSetNone));
  EXPECT_EQ(kMaxCpus / 4, topology->CountCores(&kCpuSetEveryOther));

  {
    std::unique_ptr<SysTopology> topology(SysTopology::SimpleTopology(
        /*num_nodes=*/2, /*per_node_cpus=*/36, SysTopology::HT_NONADJACENT,
        /*per_node_mem_kb=*/1024));
    EXPECT_EQ(topology->NumNodes(), 2);
    EXPECT_EQ(topology->NodeCPUs(0), HexStringToCpuSet("0x00003ffff00003ffff"));
    EXPECT_EQ(topology->NodeCPUs(1), HexStringToCpuSet("0xffffc0000ffffc0000"));
  }

  {
    std::unique_ptr<SysTopology> topology(SysTopology::SimpleTopology(
        /*num_nodes=*/4, /*per_node_cpus=*/112, SysTopology::HT_NONADJACENT,
        /*per_node_mem_kb=*/1024));
    EXPECT_EQ(topology->NumNodes(), 4);
    EXPECT_EQ(topology->NodeCPUs(0),
              HexStringToCpuSet(
                  "000000000000000000000000000000000000000000ffffffffffffff"
                  "000000000000000000000000000000000000000000ffffffffffffff"));
    EXPECT_EQ(topology->NodeCPUs(1),
              HexStringToCpuSet(
                  "0000000000000000000000000000ffffffffffffff00000000000000"
                  "0000000000000000000000000000ffffffffffffff00000000000000"));
    EXPECT_EQ(topology->NodeCPUs(2),
              HexStringToCpuSet(
                  "00000000000000ffffffffffffff0000000000000000000000000000"
                  "00000000000000ffffffffffffff0000000000000000000000000000"));
    EXPECT_EQ(topology->NodeCPUs(3),
              HexStringToCpuSet(
                  "ffffffffffffff000000000000000000000000000000000000000000"
                  "ffffffffffffff000000000000000000000000000000000000000000"));
  }
}

TEST(SimpleTopologyDefaultPackageAndNode, Basic) {
  // a valid topology should have at least 1 package and 1 node level.
  std::unique_ptr<SysTopology> topology(
      SysTopology::SimpleTopology(/*num_cpus=*/2, SysTopology::HT_NONE));
  EXPECT_EQ(topology->NumNodes(), 1);
  EXPECT_EQ(topology->NumPackages(), 1);
}

TEST(CPUSetLocal, Basic) {
  cpu_set_t old_cpu_set;
  SysTopologyHelper::GetCPUSet(&old_cpu_set);
  // turn off one cpu
  cpu_set_t new_cpu_set = old_cpu_set;
  ClearOneCpuFromSet(&new_cpu_set);
  // if forge places us on a single cpu, we can't really test this
  if (CpuSetCountCpus(&new_cpu_set) == 0) {
    LOG(WARNING) << "Unable to test cpu_set with only a single cpu available";
  } else {
    SysTopologyHelper::SetCPUSet(&new_cpu_set);
    cpu_set_t tmp_cpu_set;
    SysTopologyHelper::GetCPUSet(&tmp_cpu_set);
    ASSERT_EQ(new_cpu_set, tmp_cpu_set);
  }
  SysTopologyHelper::SetCPUSet(&old_cpu_set);
}

TEST(CPUSetThreadId, Basic) {
  cpu_set_t old_cpu_set;
  SysTopologyHelper::GetCPUSet(GetTID(), &old_cpu_set);
  // turn off one cpu
  cpu_set_t new_cpu_set = old_cpu_set;
  ClearOneCpuFromSet(&new_cpu_set);
  if (CpuSetCountCpus(&new_cpu_set) == 0) {
    LOG(WARNING) << "Unable to test cpu_set with only a single cpu available";
  } else {
    EXPECT_EQ(true, SysTopologyHelper::SetCPUSet(GetTID(), &new_cpu_set));
    cpu_set_t tmp_cpu_set;
    SysTopologyHelper::GetCPUSet(GetTID(), &tmp_cpu_set);
    ASSERT_EQ(new_cpu_set, tmp_cpu_set);
  }
  // test with invalid cpu_set
  cpu_set_t invalid_cpu_set = CpuSetMakeEmpty();
  EXPECT_EQ(false, SysTopologyHelper::SetCPUSet(GetTID(), &invalid_cpu_set));
  // restore previous cpu_set
  EXPECT_EQ(true, SysTopologyHelper::SetCPUSet(GetTID(), &old_cpu_set));
  // test an invalid pid
  EXPECT_EQ(false, SysTopologyHelper::SetCPUSet(-1, &old_cpu_set));
}

// Primarily exists for code coverage reasons. Doesn't test much.
TEST(LowLatencyNodes, Basic) {
  cpu_set_t old_cpu_set;
  SysTopologyHelper::ConstrainThreadToLowLatencyNodes(&old_cpu_set);
  SysTopologyHelper::UnconstrainThread(&old_cpu_set);
}

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  testing::InitGoogleTest(&argc, argv);

  std::string path =
      ::testing::SrcDir() + "///gloop/util/sysinfo/topology/testdata";
  if (!absl::GetFlag(FLAGS_test_datadir).empty()) {
    path = absl::GetFlag(FLAGS_test_datadir);
  }

  testing::testvalue::Enable();
  testing::testvalue::Force("SysTopologyGenerator::cpu_type",
                            CpuType::kIntelGraniterapids);

  if (absl::GetFlag(FLAGS_test_system)) {
    LOG(INFO) << "NUMA topology: " << std::endl;
    SysTopology* st = SysTopology::System();

    for (int i = 0; i < st->NumCPUs(); i++) {
      LOG(INFO) << "cpu " << i << " belongs to node " << st->NodeOf(i)
                << std::endl;
    }

    for (int i = 0; i < st->NumNodes(); i++) {
      cpu_set_t cpu_set;
      st->NodeCPUs(i, &cpu_set);
      LOG(INFO) << "node " << i << " has cpu_set: " << cpu_set << std::endl;
    }

    for (int i = 0; i < st->NumPackages(); i++) {
      cpu_set_t cpu_set = st->PackageCPUs(i);
      LOG(INFO) << "package " << i << " has cpu_set: " << cpu_set << std::endl;
    }
    ManualCheck(path, &SysTopologyGenerator::System);
  }

  if (absl::GetFlag(FLAGS_test_cpuinfo)) {
    ManualCheck(path, &SysTopologyGenerator::FromSysfs);
  }

  return RUN_ALL_TESTS();
}
