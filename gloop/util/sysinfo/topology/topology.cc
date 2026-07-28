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

// Interface to the CPU and memory node topology of a system.

#include "gloop/util/sysinfo/topology/topology.h"

#include <dirent.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#pragma clang diagnostic pop

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/internal/cpu_detect.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/sysinfo.h"
#include "gloop/strings/split.h"
#include "gloop/testing/production_stub/testvalue.h"
#include "gloop/util/gtl/container_logging.h"
#include "gloop/util/os/core/cpu_set.h"
#include "gloop/util/sysinfo/topology/topology.pb.h"
#include "gloop/util/sysinfo/topology/topology_converter.h"
#include "re2/re2.h"

using absl::base_internal::CpuType;
using absl::base_internal::GetCpuType;
using util_os_core::CpuSetAnd;
using util_os_core::CpuSetClear;
using util_os_core::CpuSetClearSubset;
using ::util_os_core::CpuSetCompare;
using util_os_core::CpuSetContains;
using util_os_core::CpuSetCountCpus;
using util_os_core::CpuSetInsert;
using util_os_core::CpuSetLessThan;
using util_os_core::CpuSetMakeEmpty;
using util_os_core::CpuSetOr;
using util_os_core::CpuSetTestEmpty;
using util_os_core::CpuSetTestEqual;
using util_os_core::HexStringToCpuSet;

ABSL_FLAG(std::string, sysinfo_topology_path, "",
          "Path to a file containing a binary TopologyInfo proto. If set, this "
          "overrides the detected system topology.");

namespace {
using Id = SysTopology::Id;
using ChildMap = SysTopology::ChildMap;

template <typename MapT>
std::string MapToString(const MapT& m) {
  return absl::StrJoin(
      m, " ", [](std::string* out, const typename MapT::value_type& p) {
        absl::StrAppend(out, "(", p.first, ", ", p.second, ")");
      });
}

std::string ChildMapToString(const ChildMap& m) {
  return absl::StrJoin(
      m, " ", [](std::string* out, const std::pair<Id, cpu_set_t>& p) {
        std::ostringstream ss;
        ss << p.second;
        absl::StrAppend(out, "(", p.first, ", ", ss.str(), ")");
      });
}
}  // namespace

// Helper for extracting cache information from /sys
void SysTopologyGenerator::ReadCacheInfo(SysTopology::TopologyInfo* ti) const {
  for (int index = 0;; index++) {
    std::string cpu0path = absl::Substitute(
        "$0/sys/devices/system/cpu/cpu0/cache/index$1", path_prefix_, index);
    char type[64];

    // See if this cache index exists. Assume that indices are
    // contiguous from 0
    if (access(cpu0path.c_str(), F_OK) ||
        !ReadProcField((cpu0path + "/type").c_str(), -1, 0, "%s", type))
      break;

    int level;
    if (!ReadProcField((cpu0path + "/level").c_str(), -1, 0, "%d", &level))
      continue;

    int size = -1;
    // assume unit is 'K'
    char unit;
    if (!ReadProcField((cpu0path + "/size").c_str(), -1, 0, "%d%c", &size,
                       &unit))
      continue;
    if (unit != 'K') {
      LOG(ERROR) << "Error reading cache size file " << cpu0path << "/size"
                 << "; Size unit is not in K";
      size = -1;
    }

    // collect cache info
    ti->cache.push_back(SysTopology::CacheInfo());
    SysTopology::CacheInfo& cache_info = ti->cache.back();
    cache_info.level = level;
    cache_info.size_kb = size;
    cache_info.type = type;

    // Some systems have a Data and Instruction cache at the same
    // level. For topology purposes we'll ignore Instruction caches.
    if (strcmp(type, "Unified") && strcmp(type, "Data")) continue;

    std::string levelname = absl::Substitute("l$0cache", level);

    // Read the sharing cpu_set_t for each group of CPU siblings.
    cpu_set_t cpus_to_read = CpuSetMakeEmpty();
    for (const auto& ci : ti->cpus) {
      CpuSetInsert(ci.id, &cpus_to_read);
    }

    std::vector<cpu_set_t> cpu_sets(ti->cpus.size());
    int sibling_count = 0;
    for (int i = 0; i < ti->cpus.size(); ++i) {
      const int cpu_id = ti->cpus[i].id;
      if (!CpuSetContains(cpu_id, &cpus_to_read)) {
        continue;
      }
      cpu_set_t cpu_set;
      std::string path = absl::Substitute(
          "$0/sys/devices/system/cpu/cpu$1/cache/index$2/shared_cpu_map",
          path_prefix_, cpu_id, index);
      if (!ReadCPUSet(path.c_str(), &cpu_set)) {
        break;
      }
      // shared_cpu_map is identical for all CPUs listed in it.
      // Copy this cpu_set to each sibling in the map.
      for (int j = 0; j < ti->cpus.size(); ++j) {
        if (CpuSetContains(ti->cpus[j].id, cpu_set)) {
          cpu_sets[j] = cpu_set;
          ++sibling_count;
        }
      }
      CpuSetClearSubset(&cpus_to_read, &cpu_set, &cpus_to_read);
    }
    if (sibling_count < ti->cpus.size()) {
      LOG(WARNING) << "Partial sibling information for " << levelname << ":"
                   << gtl::LogContainer(cpu_sets);
      continue;
    }

    ti->levels.push_back(SysTopology::LevelInfo());
    SysTopology::LevelInfo& li = ti->levels.back();
    li.name = levelname;
    li.mode = SysTopology::LevelInfo::SIBLINGMAP;
    for (int i = 0; i < ti->cpus.size(); i++) {
      ti->cpus[i].siblings[li.name] = cpu_sets[i];
    }
  }
}

void SysTopologyGenerator::UpdateCacheInfoForSnc(
    SysTopology::TopologyInfo* ti) const {
  CpuType cpu_type = GetCpuType();
  testing::testvalue::Adjust("SysTopologyGenerator::cpu_type", &cpu_type);
  // Check if numa cpu map has been read
  auto node_level = absl::c_find_if(
      ti->levels,
      [](const SysTopology::LevelInfo& level) { return level.name == "node"; });
  if (node_level == ti->levels.end()) {
    LOG(ERROR) << "Couldn't find node level info.";
    return;
  }
  // Check if SNC2/3 is enabled
  int num_nodes_with_cpu = absl::c_count_if(
      node_level->children,
      [&](const auto& node) { return CpuSetCountCpus(node.second) != 0; });
  // TODO: Note that this applies to SNC2/3 only. It is not a
  // compatible approach for other types of sub-numa configuration such as SNC4,
  // and not for sub-subsequent platforms with sub-numa support.
  if (num_nodes_with_cpu != 4 && num_nodes_with_cpu != 6) {
    return;
  }
  // For each cpu, find its corresponding numa node
  for (SysTopology::CPUInfo& cpu : ti->cpus) {
    auto numa = absl::c_find_if(node_level->children, [&](const auto& node) {
      return CpuSetContains(cpu.id, node.second);
    });
    if (numa == node_level->children.end()) {
      LOG(ERROR) << "Couldn't find corresponding numa node for cpu " << cpu.id;
      continue;
    }
    cpu.siblings["l3cache"] = numa->second;
  }
}

bool SysTopologyGenerator::ReadCPUSet(const char* path,
                                      cpu_set_t* result_set) const {
  char buf[2048];
  if (!ReadProcField(path, -1, 0, "%s", buf)) {
    return false;
  }
  std::string cpustring = buf;
  absl::StrReplaceAll({{",", ""}}, &cpustring);
  return HexStringToCpuSet(cpustring, result_set);
}

double SysTopology::CPUPairCacheLocalityScore(Id a, Id b) const {
  CHECK_GE(FindLevel("l2cache"), 0);

  cpu_set_t cpu_set_a;
  // Pair share non-last-level-cache score 1.0
  CoreSiblings(a, &cpu_set_a);
  if (CpuSetContains(b, &cpu_set_a)) return 1.0;

  int last_level_cache_level = FindLevel("l3cache");
  if (last_level_cache_level == -1)
    last_level_cache_level = FindLevel("l2cache");
  // Pair share last-level-cache score 0.70
  SiblingsOf(a, last_level_cache_level, &cpu_set_a);
  if (CpuSetContains(b, &cpu_set_a)) return 0.70;

  int package_level = FindLevel("package");
  // Pair on the same package but don't share any cache score 0.40
  SiblingsOf(a, package_level, &cpu_set_a);
  if (CpuSetContains(b, &cpu_set_a)) return 0.40;

  // Pair on different packages score 0.1
  return 0.1;
}

double SysTopology::CacheCompactnessScore(const cpu_set_t& cpu_set) const {
  const int num_caches = NumCaches();
  if (num_caches < 2) {
    return 1.0;
  }
  const int cpu_set_num_cpus = CpuSetCountCpus(cpu_set);

  // Calculate the minimum number of caches required (best-case).
  const int cpus_per_cache = NumCPUs() / num_caches;
  const int num_caches_min =
      std::ceil(static_cast<double>(cpu_set_num_cpus) / cpus_per_cache);
  const int num_caches_max =
      std::min(cpu_set_num_cpus, num_caches);  // Worst-case.
  if (num_caches_max == num_caches_min) {
    return 1.0;
  }
  int num_caches_used = 0;  // Track how many caches are being used.
  cpu_set_t unaccounted_cpus = cpu_set;

  for (int cache_id = 0; cache_id < num_caches; ++cache_id) {
    cpu_set_t cache_cpus(CacheCPUs(cache_id));
    CpuSetAnd(&cache_cpus, &unaccounted_cpus, &cache_cpus);
    if (!CpuSetTestEmpty(&cache_cpus)) {
      num_caches_used++;
      CpuSetClearSubset(&unaccounted_cpus, &cache_cpus, &unaccounted_cpus);
      if (CpuSetTestEmpty(&unaccounted_cpus)) {
        break;
      }
    }
  }
  return 1 - (num_caches_used - num_caches_min) /
                 static_cast<double>(num_caches_max - num_caches_min);
}

int SysTopology::LastLevelCacheSizeAggregate() const {
  int ret = -1;
  if (!cache_.empty()) {
    const SysTopology::CacheInfoInt& cii = cache_.back();
    if (cii.size_kb > 0 && cii.num > 0) {
      ret = cii.size_kb * cii.num;
    }
  }
  return ret;
}

int SysTopology::LastLevelCacheDomainCount() const {
  int ret = 0;
  if (!cache_.empty()) {
    const SysTopology::CacheInfoInt& cii = cache_.back();
    if (cii.size_kb > 0 && cii.num > 0) {
      return cii.num;
    }
  }
  return ret;
}

SysTopology::Id SysTopology::PackageOfNode(const Id node_id) const {
  const cpu_set_t node_cpus(NodeCPUs(node_id));
  for (Id package_id = 0; package_id < NumPackages(); package_id++) {
    const cpu_set_t package_cpus(PackageCPUs(package_id));
    cpu_set_t result;
    CPU_AND(&result, &node_cpus, &package_cpus);
    if (CPU_EQUAL(&result, &node_cpus)) {
      return package_id;
    }
  }
  return -1;
}

std::set<SysTopology::Id> SysTopology::NodesOfPackage(
    const SysTopology::Id package_id) const {
  std::set<Id> nodes;
  cpu_set_t package_cpus(PackageCPUs(package_id));
  for (const Id node_id : NodeIdsWithCpus()) {
    cpu_set_t node_cpus(NodeCPUs(node_id));
    cpu_set_t result;
    CPU_AND(&result, &package_cpus, &node_cpus);
    CPU_ZERO(&node_cpus);
    if (!CPU_EQUAL(&result, &node_cpus)) {
      nodes.insert(node_id);
    }
  }
  return nodes;
}

std::set<SysTopology::Id> SysTopology::CachesOfNode(
    const SysTopology::Id node_id) const {
  std::set<Id> caches;
  cpu_set_t node_cpus(NodeCPUs(node_id));
  for (const Id cache_id : CacheIdsWithCpus()) {
    cpu_set_t cache_cpus(CacheCPUs(cache_id));
    cpu_set_t result;
    CPU_AND(&result, &node_cpus, &cache_cpus);
    if (CountCores(&result) > 0) {
      caches.insert(cache_id);
    }
  }
  return caches;
}

SysTopology::Id SysTopology::NodeOfCache(const Id cache_id) const {
  const cpu_set_t cache_cpus(CacheCPUs(cache_id));
  for (const Id node_id : NodeIdsWithCpus()) {
    const cpu_set_t node_cpus(NodeCPUs(node_id));
    cpu_set_t result;
    CPU_AND(&result, &cache_cpus, &node_cpus);
    if (CPU_EQUAL(&result, &cache_cpus)) {
      return node_id;
    }
  }
  return -1;
}

SysTopology::Id SysTopology::CCDOf(Id cpu) const {
  CpuType cpu_type = GetCpuType();
  testing::testvalue::Adjust("SysTopologyGenerator::cpu_type", &cpu_type);
  if (cpu_type == CpuType::kAmdRome) {
    return CacheOf(cpu) / 2;
  } else {
    return CacheOf(cpu);
  }
}

int SysTopology::NumCCDs() const {
  CpuType cpu_type = GetCpuType();
  testing::testvalue::Adjust("SysTopologyGenerator::cpu_type", &cpu_type);
  if (cpu_type == CpuType::kAmdRome) {
    return NumCaches() / 2;
  } else {
    return NumCaches();
  }
}

int SysTopology::LastLevelCacheSizePerPackage() const {
  int ret = -1;
  if (!cache_.empty()) {
    const SysTopology::CacheInfoInt& cii = cache_.back();
    if (cii.size_kb > 0) {
      ret = cii.size_kb;
    }
  }
  return ret;
}

// report node distance between two phys nodes
// reported value could be 0 as old kernel doesn't support node distance well
int SysTopology::NodePairMemDistance(Id a, Id b) const {
  // sanity check
  CHECK(a < NumNodes());
  CHECK(b < NumNodes());
  std::map<int, std::map<int, int>>::const_iterator it =
      physnode_distance.find(a);
  if (it != physnode_distance.end()) {
    // distance map has distance info of a to all nodes
    auto it2 = (*it).second.find(b);
    if (it2 != (*it).second.end()) {
      return it2->second;
    }
    return 0;
  }
  return 0;
}

static constexpr char kSysNodeDir[] = "/sys/devices/system/node/";

std::vector<SysTopology::Id> SysTopologyGenerator::GetMachineNodeIdsFromSysfs()
    const {
  std::vector<SysTopology::Id> node_ids;

  CpuType cpu_type = GetCpuType();
  testing::testvalue::Adjust("SysTopologyGenerator::cpu_type", &cpu_type);

  // Get all the node directories from the FS.
  DIR* sys_dir = opendir(absl::StrCat(path_prefix_, kSysNodeDir).c_str());
  if (sys_dir == nullptr) {
    return node_ids;
  }
  struct dirent* sys_ent;
  while ((sys_ent = readdir(sys_dir)) != nullptr) {
    int node_id;
    if (RE2::FullMatch(sys_ent->d_name, "node(\\d+)", &node_id)) {
      VLOG(4) << "Found NUMA node " << node_id;

      node_ids.push_back(static_cast<SysTopology::Id>(node_id));
    }
  }
  closedir(sys_dir);
  std::sort(node_ids.begin(), node_ids.end());
  return node_ids;
}

// Helper for extracting the mapping from numa nodes to CPUs from
// /sys, and adding an appropriate level in the TopologyInfo
void SysTopologyGenerator::ReadNumaCPUMap(SysTopology::TopologyInfo* ti) const {
  const std::string kNodePrefix = absl::StrCat(path_prefix_, kSysNodeDir);
  std::vector<SysTopology::Id> node_ids = GetMachineNodeIdsFromSysfs();
  if (node_ids.empty()) {
    return;
  }

  SysTopology::LevelInfo li;
  li.name = "node";
  li.mode = SysTopology::LevelInfo::CHILDMAP;

  // Read each node.
  for (SysTopology::Id node : node_ids) {
    cpu_set_t cpu_set;
    std::string dir = absl::Substitute("$0node$1/", kNodePrefix, node);
    if (access((dir + "cpumap").c_str(), F_OK) ||
        !ReadCPUSet((dir + "cpumap").c_str(), &cpu_set)) {
      LOG(DFATAL) << "Cannot read numa node " << node;
      continue;
    }
    VLOG(4) << "Found cpuset " << cpu_set << " on node " << node;

    li.children[node] = cpu_set;
    // Parse the node distances if available
    std::string distance_file = dir + "distance";
    if (access(distance_file.c_str(), F_OK) == 0) {
      std::string distances;
      if (ReadProcFileToString(distance_file.c_str(), -1, kScanfileBufsize,
                               &distances) < 0) {
        LOG(WARNING) << "Error reading: " << distance_file;
      } else {
        std::vector<int32_t> distance_vec;
        SplitLeadingDec32Values(distances.c_str(), &distance_vec);
        int num_nodes = 0;
        double sum = 0;
        std::map<int, int> node_distances;
        for (const auto& distance : distance_vec) {
          node_distances[num_nodes] = distance;
          sum += distance;
          num_nodes++;
        }

        ti->maxphysnode_distances[node] = node_distances;
        int avg_dist = static_cast<int>(sum / num_nodes + 0.1);
        distance_vec.clear();
        VLOG(3) << "avg distance for node with cpumask " << cpu_set
                << " is: " << avg_dist;
        ti->distances[avg_dist].insert(cpu_set);
      }
    }
  }
  ti->levels.push_back(li);
}

SysTopology* SysTopologyGenerator::FromSysfs() const {
  // Get all the CPU directories from the FS.
  const std::string kCpuPrefix =
      ::absl::Substitute("$0/sys/devices/system/cpu", path_prefix_);
  DIR* sys_dir = opendir(kCpuPrefix.c_str());
  std::vector<SysTopology::Id> cpu_ids;
  if (sys_dir == nullptr) {
    LOG(WARNING) << "Can't access " + kCpuPrefix;
    return nullptr;
  }
  struct dirent* sys_ent;
  static constexpr LazyRE2 kCpuRegexp = {"cpu(\\d+)"};
  while ((sys_ent = readdir(sys_dir)) != nullptr) {
    int cpu_id;
    if (RE2::FullMatch(sys_ent->d_name, *kCpuRegexp, &cpu_id)) {
      VLOG(4) << "Found CPU " << cpu_id;
      cpu_ids.push_back(static_cast<SysTopology::Id>(cpu_id));
    }
  }
  closedir(sys_dir);
  std::sort(cpu_ids.begin(), cpu_ids.end());

  SysTopology::TopologyInfo ti;
  ti.Clear();
  ti.levels.resize(3);
  ti.levels[0].name = "cpu";
  ti.levels[0].mode = SysTopology::LevelInfo::CPUID;
  ti.levels[1].name = "core";
  ti.levels[1].mode = SysTopology::LevelInfo::PARENTMAP;
  ti.levels[2].name = "package";
  ti.levels[2].mode = SysTopology::LevelInfo::PARENTMAP;

  // Read each cpu.
  for (SysTopology::Id cpu : cpu_ids) {
    int online = 1;
    std::string online_path =
        absl::Substitute("$0/cpu$1/online", kCpuPrefix, cpu);
    if (ReadProcField(online_path.c_str(), -1, 0, "%d", &online) &&
        online == 0) {
      VLOG(4) << "CPU " << cpu << " is offline";
      continue;
    }
    int core, package;
    std::string dir = absl::Substitute("$0/cpu$1/topology/", kCpuPrefix, cpu);
    if (!ReadProcField((dir + "core_id").c_str(), -1, 0, "%d", &core)) {
      LOG(WARNING) << "Cannot read CPU core " << cpu;
      continue;
    }
    if (!ReadProcField((dir + "physical_package_id").c_str(), -1, 0, "%d",
                       &package)) {
      LOG(WARNING) << "Cannot read CPU package " << package;
      continue;
    }
    VLOG(4) << "Found CPU " << cpu << ", core " << core << ", package "
            << package;

    ti.cpus.push_back(SysTopology::CPUInfo());
    SysTopology::CPUInfo& ci = ti.cpus.back();
    ci.id = cpu;

    // Fake core ids if not available.
    // Simplistic cpu info provided by xen (under Ubiquity) skips
    // topology spec details.
    // Package is set to 0 by default.
    CHECK_GE(package, 0);
    if (core == -1) {
      core = package;
    } else {
      core |= package << 16;
    }

    ci.parent["core"] = core;
    ci.parent["package"] = package;
  }

  if (ti.cpus.empty()) {
    LOG(WARNING) << "No useful info in /sys/devices/system/cpu";
    return nullptr;
  }

  ReadCacheInfo(&ti);
  ReadNumaCPUMap(&ti);
  UpdateCacheInfoForSnc(&ti);
  ReadNumaMemNodes(&ti);
  return new SysTopology(std::move(ti));
}

void SysTopologyGenerator::ReadNumaMemNodes(
    SysTopology::TopologyInfo* ti) const {
  for (SysTopology::Id node_id : GetMachineNodeIdsFromSysfs()) {
    SysTopology::MemInfo info;

    // MemInfo contains the information of memory node to physical node map. It
    // was designed to support fake NUMA, which is no longer supported. So the
    // memory node id is the same as its physical node id.
    //
    // TODO: Remove any code related to fake NUMA.
    info.id = info.node = node_id;

    // Read the memory node capacity. Expect following format:
    //     "Node 1 MemTotal:       131048880 kB"
    const std::string meminfo_filename =
        absl::StrCat(path_prefix_, kSysNodeDir, "node", node_id, "/meminfo");
    std::string field_prefix = absl::StrCat("Node ", node_id, " MemTotal:");
    long long size_kb = 0;  // NOLINT(google-runtime-int)
    if (ReadProcKeyword(meminfo_filename.c_str(), 0, field_prefix.c_str(),
                        "%lld", &size_kb)) {
      info.size_kb = size_kb;
    } else {
      LOG(WARNING) << "Failed to parse node 'MemTotal' from: "
                   << meminfo_filename;
    }

    ti->mems.push_back(info);
  }
}

absl::StatusOr<SysTopology::TopologyInfo>
SysTopologyGenerator::TopologyInfoFromProcCPUInfo() const {
  FILE* f;

  SysTopology::TopologyInfo ti;

  const std::string cpuinfo_path =
      absl::Substitute("$0/proc/cpuinfo", path_prefix_);
  f = OpenProcFile(cpuinfo_path.c_str(), -1);
  if (!f) {
    return absl::InternalError(
        absl::Substitute("Couldn't open $0.", cpuinfo_path));
  }

  ti.Clear();
  ti.levels.resize(3);
  ti.levels[0].name = "cpu";
  ti.levels[0].mode = SysTopology::LevelInfo::CPUID;
  ti.levels[1].name = "core";
  ti.levels[1].mode = SysTopology::LevelInfo::PARENTMAP;
  ti.levels[2].name = "package";
  ti.levels[2].mode = SysTopology::LevelInfo::PARENTMAP;

  // Scan /proc/cpuinfo looking for lines for cpu id, core id and
  // package id
  int cpu = -1, next_cpu = -1, core = -1, package = -1;

  char buf[kScanfileBufsize];

  while (true) {
    char* read_line;
    read_line = fgets(buf, sizeof(buf), f);

    if (read_line) {
      VLOG(3) << "read line " << buf;
    } else {
      VLOG(3) << "EOF";
    }

    if (!read_line || (sscanf(buf, "processor\t: %d", &next_cpu) == 1)) {
      // We've found the start of a new processor section. If we have
      // existing cpu/core/package info, add a CPU to the list.
      if (cpu >= 0) {
        VLOG(4) << "Found CPU " << cpu << ", core " << core << ", package "
                << package;

        ti.cpus.push_back(SysTopology::CPUInfo());
        SysTopology::CPUInfo& ci = ti.cpus.back();
        ci.id = cpu;

        // Fake core ids if not available.
        // Simplistic cpu info provided by xen (under Ubiquity) skips
        // topology spec details.
        // Package is set to 0 by default.
        CHECK_GE(package, 0);
        if (core == -1) {
          core = package;
        } else {
          core |= package << 16;
        }

        ci.parent["core"] = core;
        ci.parent["package"] = package;
      }

      cpu = next_cpu;
      VLOG(3) << "Read cpu id " << cpu;
      next_cpu = -1;
      core = -1;
      package = 0;
    }

    if (!read_line) break;

    // See if this line matches a core or package id
    if (sscanf(buf, "core id : %d", &core) == 1) {
      VLOG(3) << "Read core id " << core;
    }
    if (sscanf(buf, "physical id : %d", &package) == 1) {
      VLOG(3) << "Read package id " << package;
    }
  }
  fclose(f);

  if (ti.cpus.empty()) {
    return absl::InternalError("No useful info in /proc/cpuinfo");
  }

  ReadCacheInfo(&ti);
  ReadNumaCPUMap(&ti);
  UpdateCacheInfoForSnc(&ti);
  ReadNumaMemNodes(&ti);
  return ti;
}

SysTopology* SysTopologyGenerator::FromProcCPUInfo() const {
  auto statusor_ti = TopologyInfoFromProcCPUInfo();
  if (!statusor_ti.ok()) {
    LOG(WARNING) << statusor_ti.status();
    return nullptr;
  }
  return new SysTopology(std::move(*statusor_ti));
}

SysTopology::TopologyInfo SysTopology::TopologyInfoFromNumCpus(
    int num_cpus, HTMode ht, int ht_cpus_per_core) {
  TopologyInfo ti;
  ti.levels.resize(2);
  ti.levels[0].name = "cpu";
  ti.levels[0].mode = LevelInfo::CPUID;
  ti.levels[1].name = "core";
  ti.levels[1].mode = LevelInfo::PARENTMAP;

  num_cpus = std::max(1, num_cpus);
  ti.cpus.resize(num_cpus);

  for (int i = 0; i < num_cpus; i++) {
    ti.cpus[i].id = i;
    Id core = i;
    switch (ht) {
      case HT_NONE:
        break;
      case HT_ADJACENT:
        core /= ht_cpus_per_core;
        break;
      case HT_NONADJACENT:
        if (num_cpus >= ht_cpus_per_core) {
          core %= (num_cpus / ht_cpus_per_core);
        }
        break;
    }
    ti.cpus[i].parent["core"] = core;
  }
  return ti;
}

SysTopology* SysTopology::SimpleTopology(int num_cpus, HTMode ht) {
  return new SysTopology(TopologyInfoFromNumCpus(num_cpus, ht, 2));
}

SysTopology* SysTopology::SimpleTopology(int num_cpus, HTMode ht,
                                         int cpus_per_core) {
  return new SysTopology(TopologyInfoFromNumCpus(num_cpus, ht, cpus_per_core));
}

SysTopology* SysTopology::SimpleTopology(int num_nodes, int per_node_cpus,
                                         HTMode ht, int per_node_mem_kb) {
  return SimpleTopology(num_nodes, num_nodes, per_node_cpus, ht,
                        per_node_mem_kb);
}

SysTopology* SysTopology::SimpleTopology(int num_nodes, int per_package_nodes,
                                         int per_node_cpus, HTMode ht,
                                         int per_node_mem_kb) {
  int ht_cpus_per_core = 2;
  int num_packages = 1;
  TopologyInfo ti;
  ti.levels.resize(3);
  ti.levels[0].name = "cpu";
  ti.levels[0].mode = LevelInfo::CPUID;
  ti.levels[1].name = "core";
  ti.levels[1].mode = LevelInfo::PARENTMAP;
  ti.levels[2].name = "node";
  ti.levels[2].mode = LevelInfo::CHILDMAP;
  if (per_package_nodes != num_nodes) {
    CHECK(per_package_nodes > 0 && per_package_nodes < num_nodes &&
          num_nodes % per_package_nodes == 0);
    num_packages = num_nodes / per_package_nodes;
    ti.levels.push_back({.mode = LevelInfo::CHILDMAP, .name = "package"});
  }

  int num_cpus = std::max(1, num_nodes * per_node_cpus);
  ti.cpus.resize(num_cpus);

  for (int i = 0; i < num_cpus; i++) {
    ti.cpus[i].id = i;
    Id core = i;
    switch (ht) {
      case HT_NONE:
        break;
      case HT_ADJACENT:
        core /= ht_cpus_per_core;
        break;
      case HT_NONADJACENT:
        core %= (num_cpus / ht_cpus_per_core);
        break;
    }
    ti.cpus[i].parent["core"] = core;
  }

  LOG_IF(WARNING, num_nodes * per_node_cpus > CPU_SETSIZE)
      << "Total number of CPUs " << num_nodes * per_node_cpus << " exceeds "
      << CPU_SETSIZE;
  // Total number of CPUs per hyperthread section in the CPU set.
  const int cpus_per_hyperthread = num_cpus / ht_cpus_per_core;
  // Number of just a node's CPUs per hyperthread section in the CPU set.
  const int node_cpus_per_hyperthread = per_node_cpus / ht_cpus_per_core;
  ti.mems.resize(num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    MemInfo mi(i, i);
    mi.size_kb = per_node_mem_kb;
    ti.mems[i] = mi;

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    for (int j = 0; j < per_node_cpus; ++j) {
      int cpu;
      if (ht == HT_NONADJACENT) {
        // CPU-offset to the first hyperthread section for this node's CPUs.
        const int hyperthread_ofs = i * node_cpus_per_hyperthread;
        // Hyperthread section number for this CPU.
        const int hyperthread_num = j / node_cpus_per_hyperthread;
        // This CPU's offset within the hyperthread section.
        const int node_cpu_within_hyperthread = j % node_cpus_per_hyperthread;
        // Pick the CPU within the appropriate hyperthread section for this
        // node.
        cpu = hyperthread_ofs + hyperthread_num * cpus_per_hyperthread +
              node_cpu_within_hyperthread;
      } else {
        cpu = i * per_node_cpus + j;
      }
      CPU_SET(cpu, &cpu_set);
    }
    ti.levels[2].children[i] = cpu_set;
  }
  if (num_packages > 1) {
    for (int package = 0; package < num_packages; package++) {
      for (int i = 0; i < per_package_nodes; i++) {
        int node = package * per_package_nodes + i;
        CpuSetOr(&ti.levels[3].children[package],
                 &ti.levels[3].children[package], &ti.levels[2].children[node]);
      }
    }
  }

  return new SysTopology(ti);
}

SysTopology* SysTopologyGenerator::FromNumCPUs() const {
  return SysTopology::SimpleTopology(::NumCPUs(),
                                     absl::base_internal::IsSMTEnabled()
                                         ? SysTopology::HT_ADJACENT
                                         : SysTopology::HT_NONE);
}

static SysTopologyGenerator::Generator generators[] = {
    &SysTopologyGenerator::FromSysfs,        // the better ...
    &SysTopologyGenerator::FromProcCPUInfo,  // the good ...
    &SysTopologyGenerator::FromNumCPUs,      // and the feeble
};

SysTopology* SysTopology::System() {
  static SysTopology* instance = SysTopology::UncachedSystem();
  return instance;
}

SysTopology* SysTopology::UncachedSystem() {
  SysTopologyGenerator generator;
  return generator.System();
}

namespace {
std::optional<::util::sysinfo::topology::TopologyInfo> LoadTopologyInfoProto(
    const std::string& path) {
  ::util::sysinfo::topology::TopologyInfo proto;
  std::ifstream stream(path, std::ios::in | std::ios::binary);
  if (!stream.good()) {
    LOG(ERROR) << "Failed to open topology info file: " << path;
    return {};
  }
  if (!proto.ParseFromIstream(&stream)) {
    LOG(ERROR) << "Failed to parse topology proto from " << path;
    return {};
  }
  return proto;
}
}  // namespace

SysTopology* SysTopologyGenerator::System() const {
  // Optionally bypass the detection if a TopologyInfo proto is available.
  if (auto path = absl::GetFlag(FLAGS_sysinfo_topology_path); !path.empty()) {
    if (auto proto = LoadTopologyInfoProto(path); proto.has_value()) {
      // The proto doesn't match the current system which will lead to weird
      // broken behavior.
      CHECK_EQ(proto->cpu_size(), ::NumCPUs())
          << "Specified topology " << path
          << " doesn't match this system's CPU count";
      return new SysTopology(
          ::util::sysinfo::topology::TopologyInfoConverter::FromProto(*proto));
    }
  }

  SysTopology* best = nullptr;

  // Try each generator in turn - use the one that gives us the most
  // detailed CPU info.
  for (int i = 0; i < std::size(generators); i++) {
    SysTopologyGenerator::Generator g = generators[i];
    // We really don't want this one kicking in unless there's nothing better
    if (g == &SysTopologyGenerator::FromNumCPUs && best) continue;

    SysTopology* t = (this->*g)();

    if (!t) continue;
    VLOG(4) << "Topology " << i << " has order " << t->Order();

    if (!best || t->Order() > best->Order()) {
      delete best;
      best = t;
    } else {
      delete t;
    }
  }

  return best;
}

class SysTopology::TopologySorter {
 public:
  explicit TopologySorter(const TopologyInfo& ti) : ti_(ti) {}
  bool operator()(const LevelInfo& l1, const LevelInfo& l2) const {
    VLOG(4) << "Comparing levels " << l1.name << " and " << l2.name;
    bool lt = false, gt = false;
    bool offline_cpu = false;
    int last_cpu_id = -1;
    for (const auto& ci : ti_.cpus) {
      // (b/280062439): Check all cpu id are consecutive as topology
      // sorter fails when a CPU is offline.
      // Note that l3cache is missing when one cpu is offline.
      if (last_cpu_id != -1) {  // We need at least one sample.
        if ((ci.id - 1) != last_cpu_id) {
          VLOG(4) << "CPU id: " << last_cpu_id << " is offline.";
          offline_cpu = true;
        }
      }
      last_cpu_id = ci.id;

      const CPUSiblingMap& siblings = ci.siblings;
      CPUSiblingMap::const_iterator it1 = siblings.find(l1.name);
      CHECK(it1 != siblings.end());
      CPUSiblingMap::const_iterator it2 = siblings.find(l2.name);
      CHECK(it2 != siblings.end());
      VLOG(4) << "Comparing cpu " << ci.id << ": " << it1->second << " vs "
              << it2->second;
      cpu_set_t cpu_set_tmp;
      CpuSetClearSubset(&it1->second, &it2->second, &cpu_set_tmp);
      VLOG(4) << "Got First tmp = " << cpu_set_tmp;
      if (!CpuSetTestEmpty(cpu_set_tmp))  // (it1->second&  ~it2->second)
        gt = true;
      CpuSetClearSubset(&it2->second, &it1->second, &cpu_set_tmp);
      VLOG(4) << "Got Second tmp = " << cpu_set_tmp;
      if (!CpuSetTestEmpty(cpu_set_tmp))  // (it2->second&  ~it1->second)
        lt = true;
      VLOG(4) << "Got gt: " << gt << " and lt: " << lt;
    }
    // Is the last cpu offline.
    if (last_cpu_id < ti_.cpus.size()) {
      offline_cpu = true;
    }
    // Topology ordering does not match when it has cpus offline.
    if (!offline_cpu) {
      CHECK(!(lt && gt)) << "Impossible topology ordering: level " << l1.name
                         << " vs " << l2.name;
    }
    return lt;
  }

 private:
  const TopologyInfo& ti_;
};

// Compare two vectors of cpu_set_t. Returns true if equal, else false.
static bool CompareCpuSetVectors(absl::Span<const cpu_set_t> lhs,
                                 absl::Span<const cpu_set_t> rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (int index = 0; index < lhs.size(); ++index) {
    if (!CpuSetTestEqual(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

// Compare two sets of cpu_set_t. Returns true if equal, else false.
static bool CompareCpuSetSets(const std::set<cpu_set_t, CpuSetLessThan>& lhs,
                              const std::set<cpu_set_t, CpuSetLessThan>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  auto lhs_it = lhs.begin();
  auto rhs_it = rhs.begin();
  for (; lhs_it != lhs.end(); ++lhs_it, ++rhs_it) {
    if (!CpuSetTestEqual(*lhs_it, *rhs_it)) {
      return false;
    }
  }
  return true;
}

SysTopology::SysTopology(TopologyInfo ti) : topology_info_(ti) {
  // Take the various bits of information passed by the caller, and
  // convert it into a set of vectors.

  typedef __gnu_cxx::hash_map<Id, Id> RemapMap;
  std::map<std::string, Id> max_id;

  // Initialize the bitmap of all our CPUs
  CpuSetClear(&all_cpus_);
  for (const auto& ci : ti.cpus) {
    CpuSetInsert(ci.id, &all_cpus_);
  }

  bool saw_node_level = false;
  bool saw_package_level = false;
  for (const auto& li : ti.levels) {
    if (li.name == "node") {
      saw_node_level = true;
    }
    if (li.name == "package") {
      saw_package_level = true;
    }
  }

  // Synthesize a node level if necessary
  if (!saw_node_level) {
    VLOG(4) << "Adding generic node level: node 0 with cpus " << all_cpus_;
    LevelInfo li;
    li.name = "node";
    li.mode = LevelInfo::CHILDMAP;
    li.children[0] = all_cpus_;
    ti.levels.push_back(li);
  }

  // Synthesize a package level if necessary
  if (!saw_package_level) {
    VLOG(4) << "Adding generic package level: node 0 with cpus " << all_cpus_;
    LevelInfo li;
    li.name = "package";
    li.mode = LevelInfo::CHILDMAP;
    li.children[0] = all_cpus_;
    ti.levels.push_back(li);
  }

  // Create a machine level
  {
    LevelInfo li;
    li.name = "machine";
    li.mode = LevelInfo::CHILDMAP;
    li.children[0] = all_cpus_;
    ti.levels.push_back(li);
  }

  // Propagate information from the level info child maps
  // to the cpuinfo parent maps
  for (const auto& li : ti.levels) {
    // Prep the max_id map
    max_id[li.name] = -1;

    if (li.mode != LevelInfo::CHILDMAP) {
      CHECK(li.children.empty());
      continue;
    }
    CHECK(!li.children.empty());

    VLOG(4) << "Processing childmap information for level " << li.name;
    VLOG(4) << ChildMapToString(li.children);

    for (auto& ci : ti.cpus) {
      CHECK(!ci.parent.count(li.name));
      CHECK(!ci.siblings.count(li.name));
      for (const auto& child : li.children) {
        if (CpuSetContains(ci.id, &child.second)) {
          ci.parent[li.name] = child.first;
          VLOG(4) << "CPU " << ci.id << " parent = " << child.first;
          break;
        }
      }
    }
  }

  // Next scan the cpuinfo list to see if we can convert sibling maps
  // into parent maps or vice-versa
  for (auto& li : ti.levels) {
    if (li.mode == LevelInfo::PARENTMAP || li.mode == LevelInfo::CHILDMAP) {
      if (li.mode == LevelInfo::PARENTMAP) {
        // Set bits in the childmap for each level based on the parent
        // that each CPU declares at that level
        for (auto& ci : ti.cpus) {
          CHECK(!ci.siblings.count(li.name));
          CHECK(ci.parent.count(li.name));
          CpuSetInsert(ci.id, &li.children[ci.parent[li.name]]);
        }
      }

      // Propagate the child information back to the sibling maps for
      // each CPU at that level
      for (const auto& child : li.children) {
        for (auto& ci : ti.cpus) {
          if (ci.parent[li.name] == child.first) {
            ci.siblings[li.name] = child.second;
          }
        }
      }
    } else if (li.mode == LevelInfo::SIBLINGMAP) {
      // Track whether this set of siblingmaps is a distinct partition
      // of CPUs. May not be true for e.g. distance-based levels
      bool distinct_map = true;

      // Sanity checking
      for (auto& ci : ti.cpus) {
        CHECK(!ci.parent.count(li.name));
        CHECK(ci.siblings.count(li.name));
        CpuSetInsert(ci.id, &ci.siblings[li.name]);  // CPU is its own sibling
      }

      // Try to associate an ID with each distinct set of CPU
      // siblings.
      for (int c = 0; c < ti.cpus.size() && distinct_map; c++) {
        CPUInfo& ci = ti.cpus[c];
        cpu_set_t siblings = ci.siblings[li.name];
        // See if this sibling set clashes with any other
        ChildMap::iterator it = li.children.begin();
        while (it != li.children.end()) {
          cpu_set_t cpu_set_tmp;
          CpuSetAnd(&cpu_set_tmp, &it->second, &siblings);
          if (CpuSetTestEqual(it->second, siblings)) {
            // The sibling set matches
            ci.parent[li.name] = it->first;
            break;
          } else if (!CpuSetTestEmpty(cpu_set_tmp)) {  // it->second & siblings
            // The sibling set clashes
            distinct_map = false;
            break;
          }
          ++it;
        }
        if (it == li.children.end()) {
          // Create a new sibling set
          Id new_id = ++max_id[li.name];
          ci.parent[li.name] = new_id;
          li.children[new_id] = siblings;
        }
      }
      if (!distinct_map) {
        VLOG(4) << "Clearing parent/child info due to "
                << "overlapping siblingmaps in level " << li.name;
        // We didn't find a distinct mapping from parents to children
        // - clear any partial information we may have built up
        li.children.clear();
        max_id[li.name] = -1;
        for (auto& ci : ti.cpus) {
          ci.parent.erase(li.name);
        }
      }
    }
  }

  // Scan the cpuinfo list to find max ids for each level.
  // Compact ids since they're sparse on some kernels
  std::map<std::string, RemapMap> level_remaps;
  for (auto& li : ti.levels) {
    switch (li.mode) {
      case LevelInfo::CHILDMAP:
      case LevelInfo::PARENTMAP: {
        // Compress sparse ids
        RemapMap& rm = level_remaps[li.name];
        VLOG(4) << "Compressing ids for level " << li.name;
        for (auto& ci : ti.cpus) {
          Id& parent_id = ci.parent[li.name];
          RemapMap::iterator it = rm.find(parent_id);
          CHECK(ci.parent.count(li.name));
          VLOG(4) << "Found parent id " << parent_id;
          if (it == rm.end()) {
            // New id
            parent_id = rm[parent_id] = ++max_id[li.name];
            VLOG(4) << "New id " << parent_id;
          } else {
            parent_id = it->second;
            VLOG(4) << "Reusing id " << parent_id;
          }
        }
        break;
      }
      case LevelInfo::SIBLINGMAP:
        // Already done in the sibling map code
        break;
      case LevelInfo::CPUID: {
        // The "cpu" level is a special identity mapping
        Id& mi = max_id[li.name];
        for (auto& ci : ti.cpus) {
          mi = std::max(ci.id, mi);
          ci.parent[li.name] = ci.id;
          CpuSetClear(&ci.siblings[li.name]);
          CpuSetInsert(ci.id, &ci.siblings[li.name]);
          li.children[ci.id] = ci.siblings[li.name];
        }
        break;
      }
    }
  }

  // Propagate the remapped parent maps back to the level childmaps
  // (not really necessary for generating the final vectors, but it
  // makes the debugging output more useful)
  for (auto& li : ti.levels) {
    ChildMap newmap;
    RemapMap& rm = level_remaps[li.name];
    if (rm.empty()) continue;

    VLOG(4) << "Remapping childmaps for level " << li.name;
    VLOG(4) << "Map = " << MapToString(rm);
    VLOG(4) << "Children = " << ChildMapToString(li.children);

    for (const auto& child : li.children) {
      if (rm.count(child.first) == 0) {
        LOG(WARNING) << ": no entry for id " << child.first << " in level "
                     << li.name << ".  Fake NUMA "
                     << "detected."
                     << "";
        newmap[child.first] = child.second;
        ++max_id[li.name];
      } else {
        newmap[rm[child.first]] = child.second;
      }
    }
    li.children = newmap;

    if (li.name == "node") {
      VLOG(4) << "Remapping mems";
      for (auto& mi : ti.mems) {
        if (rm.count(mi.node) == 0) {
          LOG(WARNING) << ": no entry for node id " << mi.node;
          continue;
        }
        mi.node = rm[mi.node];
      }
    }
  }

  // Reorder levels so that each level is equal to or a subset of the
  // level above
  TopologySorter ts(ti);
  std::stable_sort(ti.levels.begin(), ti.levels.end(), ts);

  // Now compress this into a more efficiently queryable format -
  // vectors of vectors, rather than vectors of string maps.

  VLOG(4) << "max_id[] = " << MapToString(max_id);
  Id max_cpu = max_id["cpu"];

  for (const LevelInfo& li : ti.levels) {
    VLOG(4) << "Setting up level " << li.name;
    LevelInfoInt lii;
    lii.count = li.children.size();

    lii.names.insert(li.name);

    // Add child/parent maps
    if (!li.children.empty()) {
      VLOG(4) << "Adding child map " << ChildMapToString(li.children);
      cpu_set_t cpu_set_all{};
      for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        CpuSetInsert(cpu, &cpu_set_all);
      }
      lii.children.resize(max_id[li.name] + 1, cpu_set_all);
      for (const auto& child : li.children) {
        CHECK_LT(child.first, lii.children.size());
        CHECK(CpuSetTestEqual(lii.children[child.first], cpu_set_all))
            << ": parent " << child.first << " already entered";
        lii.children[child.first] = child.second;
      }
      lii.parent.resize(max_cpu + 1, -1);
      for (auto& ci : ti.cpus) {
        CHECK_EQ(lii.parent[ci.id], -1);
        lii.parent[ci.id] = ci.parent[li.name];
      }
    }

    // Add sibling maps
    cpu_set_t cpu_set_zero = CpuSetMakeEmpty();
    lii.siblings.resize(max_cpu + 1, cpu_set_zero);
    for (auto& ci : ti.cpus) {
      lii.siblings[ci.id] = ci.siblings[li.name];
    }

    // If this level is identical to the previous level then drop it
    // and point the name at the previous level
    if (!levels_.empty()) {
      LevelInfoInt& prev_level = levels_.back();
      if (lii.parent == prev_level.parent &&
          CompareCpuSetVectors(lii.children, prev_level.children) &&
          CompareCpuSetVectors(lii.siblings, prev_level.siblings)) {
        VLOG(4) << "Collapsing level " << li.name;
        levelnames_[li.name] = levels_.size() - 1;
        prev_level.names.insert(li.name);
        continue;
      }
    }

    // Add the new level to the vector of levels
    levelnames_[li.name] = levels_.size();
    levels_.push_back(lii);
  }

  core_level_ = FindLevel("core");
  if (core_level_ == -1) core_level_ = 0;

  l3_cache_level_ = FindLevel("l3cache");
  if (l3_cache_level_ == -1) l3_cache_level_ = 0;

  node_level_ = FindLevel("node");
  CHECK_NE(node_level_, -1);

  package_level_ = FindLevel("package");
  CHECK_NE(package_level_, -1);

  // Store the memory banks for each node
  LevelInfoInt& nodes = levels_[node_level_];
  nodes.mems.resize(nodes.count);
  for (const auto& mi : ti.mems) {
    if (mi.node < nodes.mems.size()) {
      nodes.mems[mi.node].insert(mi.id);
    }
    all_mems_.insert(mi.id);
    node_mems_[mi.node] = mi.size_kb;
  }

  order_ = 1;
  for (const auto& li : levels_) {
    if (li.count) {
      order_ *= li.count;
    } else {
      order_++;
    }
  }

  // Check physical node distances
  physnode_distance.clear();
  for (int i = 0; i < NumNodes(); i++) {
    std::map<int, int> distances;
    for (int j = 0; j < NumNodes(); j++) {
      distances[j] = (ti.maxphysnode_distances[i])[j];
    }
    CHECK_EQ(distances.size(), NumNodes());
    physnode_distance[i] = distances;
  }
  CHECK_EQ(physnode_distance.size(), NumNodes());

  // construct cache info
  for (const auto& cache_info : ti.cache) {
    CacheInfoInt cii;
    cii.level = cache_info.level;
    cii.size_kb = cache_info.size_kb;
    cii.type = cache_info.type;
    int topology_level = FindLevel(absl::Substitute("l$0cache", cii.level));
    if (topology_level != -1) {
      cii.num = LevelCount(topology_level);
    } else {
      continue;
    }
    cache_.push_back(cii);
  }

  // Check for asymmetric NUMA
  //
  // Look for nodes which are more well connected than others, as measured
  // by the average distance to other nodes. This information is exported
  // by the kernel via sysfs.
  memcpy(&low_latency_cpus_, &all_cpus_, sizeof(low_latency_cpus_));
  VLOG(3) << ToString();
  // If the NUMA distance data is not available, we're done.
  if (!ti.distances.empty()) {
    // Check if all nodes have the same average distance
    std::set<cpu_set_t, CpuSetLessThan>& min_set = ti.distances.begin()->second;
    std::set<cpu_set_t, CpuSetLessThan>& max_set =
        ti.distances.rbegin()->second;
    if (!CompareCpuSetSets(min_set, max_set)) {
      CpuSetClear(&low_latency_cpus_);
      for (const auto& cpu_set : min_set) {
        CpuSetOr(&low_latency_cpus_, &low_latency_cpus_, &cpu_set);
      }
      LOG(INFO) << "Asymmetric NUMA system detected. low_latency_set_cpus = "
                << low_latency_cpus_;
    }
  }

  const int num_nodes = NumNodes();
  for (Id node_id = 0; node_id < num_nodes; node_id++) {
    cpu_set_t core_cpu_set = NodeCPUs(node_id);
    if (CountCores(&core_cpu_set) != 0) {
      nodes_with_cpus_.push_back(node_id);
    }
  }

  const int num_caches = NumCaches();
  for (Id cache_id = 0; cache_id < num_caches; ++cache_id) {
    cpu_set_t core_cpu_set = CacheCPUs(cache_id);
    if (CountCores(&core_cpu_set) != 0) {
      caches_with_cpus_.push_back(cache_id);
    }
  }
}

int SysTopology::FindLevel(const std::string& name) const {
  absl::flat_hash_map<std::string, int>::const_iterator it =
      levelnames_.find(name);
  if (it == levelnames_.end()) return -1;
  return it->second;
}

int SysTopology::LevelCount(int level) const {
  if (level < 0 || level >= levels_.size()) {
    return 0;
  }
  return levels_[level].count;
}

int SysTopology::CountCores(const cpu_set_t* cpu_set) const {
  cpu_set_t core_cpu_set = CpuSetMakeEmpty();
  if (NumCPUs() == NumCores()) {
    // TODO: This leaves open the possibility that we'll return more
    // cores than we actually have.  Either mask off extra bits, or just always
    // use the loop below.
    core_cpu_set = *cpu_set;
  } else {
    for (int i = 0; i < NumCPUs(); i++) {
      if (CpuSetContains(i, cpu_set)) {
        // CoreOf(i) returns negative when core is offline, so
        // avoid CpuSetInsert.
        if (CoreOf(i) != -1) {
          CpuSetInsert(CoreOf(i), &core_cpu_set);
        }
      }
    }
    VLOG(4) << "CountCores: cpu_set = " << *cpu_set
            << ", core_set = " << core_cpu_set;
  }
  return CpuSetCountCpus(&core_cpu_set);
}

SysTopology::Id SysTopology::NodeOfMem(Id mem) const {
  const LevelInfoInt& li = levels_[node_level_];
  for (int i = 0; i < li.mems.size(); i++) {
    if (li.mems[i].count(mem)) return i;
  }
  return -1;
}

int64_t SysTopology::NodeMemSize(Id node) const {
  std::map<Id, int64_t>::const_iterator it = node_mems_.find(node);
  if (it == node_mems_.end()) return 0;
  return it->second;
}

int64_t SysTopology::MinNodeMemSize() const {
  auto it = absl::c_min_element(node_mems_, [](const auto& l, const auto& r) {
    return l.second < r.second;
  });
  return it->second;
}

int32_t SysTopology::NumNodesWithCpus() const {
  return nodes_with_cpus_.size();
}

std::vector<SysTopology::Id> SysTopology::NodeIdsWithCpus() const {
  return nodes_with_cpus_;
}

std::vector<SysTopology::Id> SysTopology::CacheIdsWithCpus() const {
  return caches_with_cpus_;
}

bool SysTopology::IsChipletBased() const {
  return NumCaches() > NumNodesWithCpus();
}

std::string SysTopology::ToString() const {
  std::ostringstream buf;
  buf << "all_cpus = " << all_cpus_ << std::endl;
  buf << "levels = "
      << MapToString(std::map(levelnames_.begin(), levelnames_.end()))
      << std::endl;
  for (const auto& l : levels_) {
    buf << std::endl
        << "level = " << gtl::LogContainer(l.names, gtl::LogLegacyUpTo100())
        << std::endl;
    buf << "count = " << std::dec << l.count << std::endl;
    buf << "children[] = " << std::hex
        << gtl::LogContainer(l.children, gtl::LogLegacyUpTo100()) << std::endl;
    buf << "parents[] = "
        << gtl::LogContainer(l.parent, gtl::LogLegacyUpTo100()) << std::endl;
    buf << "siblings[] = "
        << gtl::LogContainer(l.siblings, gtl::LogLegacyUpTo100()) << std::endl;
    if (!l.mems.empty()) {
      std::ostringstream tmp_buf;
      tmp_buf << "mems[[]] = ";
      for (const auto& mi : l.mems) {
        tmp_buf << "[" << gtl::LogContainer(mi, gtl::LogLegacyUpTo100())
                << "] ";
      }
      std::string tmp_str = tmp_buf.str();
      absl::StripTrailingAsciiWhitespace(&tmp_str);
      buf << std::dec << tmp_str << std::endl;
    }
  }
  buf << "low_latency_cpus = " << low_latency_cpus_ << std::endl;

  // Dump CPUPairCacheLocalityScore info iff:
  //   1) cache info is available; and
  //   2) NumCPUs() <= 64 (log spam).
  if (FindLevel("l2cache") != -1 && NumCPUs() <= 64) {
    buf << std::endl;
    buf << "CPUPairCacheLocalityScore:" << std::endl;
    for (int i = 0; i < NumCPUs(); i++) {
      for (int j = 0; j < NumCPUs(); j++) {
        buf << "" << CPUPairCacheLocalityScore(i, j);
        if (j != NumCPUs() - 1) buf << " ";
      }
      buf << std::endl;
    }
  }

  // dump NodePairMemDistance info on multi-node machines
  if (NumNodes() > 1) {
    buf << std::endl;
    buf << "NodePairMemDistance:" << std::endl;
    for (int i = 0; i < NumNodes(); i++) {
      for (int j = 0; j < NumNodes(); j++) {
        buf << "" << std::dec << NodePairMemDistance(i, j);
        if (j != NumNodes() - 1) buf << " ";
      }
      buf << std::endl;
    }
  }

  // dump cache info
  if (!cache_.empty()) {
    buf << std::endl;
    buf << "CacheInfo:" << std::endl;
    for (const auto& cii : cache_) {
      buf << "Level " << cii.level << " " << cii.type << " cache, size "
          << cii.size_kb << " KB,"
          << " number of caches: " << cii.num << std::endl;
    }
  }

  // Remove "0x" prefixes.
  std::string str = buf.str();
  size_t pos;
  while ((pos = str.find("0x")) != std::string::npos) {
    str.erase(pos, 2);
  }

  return str;
}

void SysTopologyHelper::ConstrainThreadToLowLatencyNodes(
    cpu_set_t* old_cpuset) {
  cpu_set_t low_latency_cpus, all_cpus;
  GetCPUSet(old_cpuset);
  SysTopology* st = SysTopology::System();
  st->low_latency_cpus(&low_latency_cpus);
  st->all_cpus(&all_cpus);
  if (CpuSetTestEqual(low_latency_cpus, all_cpus)) {
    return;
  }
  // Ensure that we don't try to use CPUs we're not allowed to run on
  CpuSetAnd(&low_latency_cpus, &low_latency_cpus, old_cpuset);
  if (CpuSetCountCpus(&low_latency_cpus) == 0) {
    LOG(WARNING) << "No low latency cpus available. Current cpuset: "
                 << *old_cpuset;
    return;
  }
  LOG(INFO) << "Constraining thread to fast nodes: " << low_latency_cpus;
  SetCPUSet(&low_latency_cpus);
}
