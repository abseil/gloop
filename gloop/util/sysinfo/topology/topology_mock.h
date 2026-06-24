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

#ifndef THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_MOCK_H_
#define THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_MOCK_H_

#include <sched.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "absl/base/internal/cpu_detect.h"
#include "gloop/base/sysinfo.h"
#include "gloop/util/sysinfo/topology/topology.h"
#include "gmock/gmock.h"

// Mock of the SysTopology class for use in unit-tests.
// These are code test helpers. For more information please refer to comments
// in util/sysinfo/topology/topology.h.
class SysTopologyMock : public SysTopology {
 public:
  SysTopologyMock()
      : SysTopology(TopologyInfoFromNumCpus(
            ::NumCPUs(),
            absl::base_internal::IsSMTEnabled() ? HT_ADJACENT : HT_NONE,
            absl::base_internal::NumContextsPerCPU())) {}
  MOCK_METHOD(int, NumLevels, (), (const, override));
  MOCK_METHOD(int64_t, Order, (), (const, override));
  MOCK_METHOD(int, FindLevel, (const std::string& name), (const, override));
  MOCK_METHOD(int, LevelCount, (int level), (const, override));
  MOCK_METHOD(const std::set<std::string>&, LevelNames, (int level),
              (const, override));
  MOCK_METHOD(int, core_level, (), (const, override));
  MOCK_METHOD(cpu_set_t, GetAllCPUs, (), (const, override));
  MOCK_METHOD(void, all_cpus, (cpu_set_t * result_set), (const, override));
  MOCK_METHOD(const std::set<Id>&, all_mems, (), (const, override));
  MOCK_METHOD(Id, ParentOf, (Id cpu, int level), (const, override));
  MOCK_METHOD(cpu_set_t, ChildrenOf, (Id id, int level), (const, override));
  MOCK_METHOD(void, ChildrenOf, (Id id, int level, cpu_set_t* result_set),
              (const, override));
  MOCK_METHOD(cpu_set_t, SiblingsOf, (Id cpu, int level), (const, override));
  MOCK_METHOD(void, SiblingsOf, (Id cpu, int level, cpu_set_t* result_set),
              (const, override));
  MOCK_METHOD(int, NumCPUs, (), (const, override));
  MOCK_METHOD(int, NumCores, (), (const, override));
  MOCK_METHOD(int, NumHyperthreadSiblings, (), (const, override));
  MOCK_METHOD(int, NumCaches, (), (const, override));
  MOCK_METHOD(int, NumNodes, (), (const, override));
  MOCK_METHOD(int, NumPackages, (), (const, override));
  MOCK_METHOD(Id, CoreOf, (Id cpu), (const, override));
  MOCK_METHOD(Id, CacheOf, (Id cpu), (const, override));
  MOCK_METHOD(int, NumCCDs, (), (const, override));
  MOCK_METHOD(Id, CCDOf, (Id cpu), (const, override));
  MOCK_METHOD(Id, NodeOf, (Id cpu), (const, override));
  MOCK_METHOD(Id, PackageOf, (Id cpu), (const, override));
  MOCK_METHOD(Id, PackageOfNode, (Id node_id), (const, override));
  MOCK_METHOD(cpu_set_t, CoreSiblings, (Id cpu), (const, override));
  MOCK_METHOD(void, CoreSiblings, (Id cpu, cpu_set_t* result_set),
              (const, override));
  MOCK_METHOD(cpu_set_t, CoreCPUs, (Id core), (const, override));
  MOCK_METHOD(void, CoreCPUs, (Id core, cpu_set_t* result_set),
              (const, override));
  MOCK_METHOD(cpu_set_t, CacheCPUs, (Id cache), (const, override));
  MOCK_METHOD(cpu_set_t, NodeCPUs, (Id node), (const, override));
  MOCK_METHOD(void, NodeCPUs, (Id node, cpu_set_t* result_set),
              (const, override));
  MOCK_METHOD(cpu_set_t, PackageCPUs, (Id node), (const, override));
  MOCK_METHOD(int, CountCores, (const cpu_set_t* cpu_set), (const, override));
  MOCK_METHOD(double, CPUPairCacheLocalityScore, (Id a, Id b),
              (const, override));
  MOCK_METHOD(int, NodePairMemDistance, (Id a, Id b), (const, override));
  MOCK_METHOD(Id, NodeOfMem, (Id mem), (const, override));
  MOCK_METHOD(Id, NodeOfCache, (Id cache), (const, override));
  MOCK_METHOD(int64_t, MinNodeMemSize, (), (const, override));
  MOCK_METHOD(int32_t, NumNodesWithCpus, (), (const, override));
  MOCK_METHOD(std::vector<Id>, NodeIdsWithCpus, (), (const, override));
  MOCK_METHOD(std::vector<Id>, CacheIdsWithCpus, (), (const, override));
  MOCK_METHOD(bool, IsChipletBased, (), (const, override));
  MOCK_METHOD(std::set<SysTopology::Id>, CachesOfNode, (Id node),
              (const, override));
  MOCK_METHOD(std::string, ToString, (), (const, override));
  MOCK_METHOD(cpu_set_t, low_latency_cpus, (), (const, override));
  MOCK_METHOD(void, low_latency_cpus, (cpu_set_t * result_set),
              (const, override));
  MOCK_METHOD(double, CacheCompactnessScore, (const cpu_set_t& cpu_set),
              (const, override));
};

#endif  // THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_MOCK_H_
