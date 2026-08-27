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

#include "gloop/util/sysinfo/topology/topology_converter.h"

#include <sched.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "gloop/util/os/core/cpu_set.h"
#include "gloop/util/sysinfo/topology/topology.h"
#include "gloop/util/sysinfo/topology/topology.pb.h"

namespace util {
namespace sysinfo {
namespace topology {
namespace TopologyInfoConverter {

namespace {

// Code copied from CpuMask to convert a proto field of repeated uint64 into
// cpu_set_t.
cpu_set_t WriteProtoToCpuSet(
    const google::protobuf::RepeatedField<uint64_t>& cpu_mask) {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  int pb_max = cpu_mask.size() - 1;
  for (int pb_index = pb_max; pb_index >= 0; --pb_index) {
    uint64_t sub_mask = cpu_mask.Get(pb_index);
    for (int sub_id = 0; sub_mask; ++sub_id) {
      if (sub_mask & 1) {
        CPU_SET(sub_id + 64 * (pb_max - pb_index), &cpu_set);
      }
      sub_mask >>= 1;
    }
  }
  return cpu_set;
}

}  // namespace

SysTopology::TopologyInfo FromProto(const TopologyInfo& ti) {
  SysTopology::TopologyInfo result;
  for (const auto& level : ti.level()) {
    SysTopology::LevelInfo new_level;
    new_level.mode = static_cast<SysTopology::LevelInfo::Mode>(level.mode());
    new_level.name = level.name();
    for (const auto& child : level.child()) {
      new_level.children[child.id()] = WriteProtoToCpuSet(child.cpu_mask());
    }
    result.levels.push_back(new_level);
  }
  for (const auto& cpu : ti.cpu()) {
    SysTopology::CPUInfo new_cpu;
    new_cpu.id = cpu.id();
    for (const auto& parent : cpu.parent()) {
      new_cpu.parent[parent.level()] = parent.id();
    }
    for (const auto& sibling : cpu.sibling()) {
      new_cpu.siblings[sibling.level()] =
          WriteProtoToCpuSet(sibling.cpu_mask());
    }
    result.cpus.push_back(new_cpu);
  }
  for (const auto& mem : ti.mem()) {
    SysTopology::MemInfo new_mem;
    new_mem.id = mem.id();
    new_mem.node = mem.node();
    new_mem.size_kb = mem.size_kb();
    result.mems.push_back(new_mem);
  }
  for (const auto& cache : ti.cache()) {
    SysTopology::CacheInfo new_cache;
    new_cache.type = cache.type();
    new_cache.level = cache.level();
    new_cache.size_kb = cache.size_kb();
    result.cache.push_back(new_cache);
  }
  for (const auto& distance : ti.distance()) {
    std::set<cpu_set_t, util_os_core::CpuSetLessThan> new_cpu_sets;
    for (const auto& cpu_sets : distance.distance()) {
      new_cpu_sets.insert(WriteProtoToCpuSet(cpu_sets.cpu_mask()));
    }
    result.distances[distance.id()] = new_cpu_sets;
  }
  for (const auto& distance : ti.maxphysnode_distance()) {
    std::map<int, int> distance_map;
    for (const auto& distance_entry : distance.distance()) {
      distance_map[distance_entry.id()] = distance_entry.distance();
    }
    result.maxphysnode_distances[distance.id()] = distance_map;
  }
  return result;
}

namespace {

// Code copied from CpuMask to convert a cpu_set_t into a repeated field of
// uint64.
void WriteCpuSetToProto(const cpu_set_t& cpu_set,
                        google::protobuf::RepeatedField<uint64_t>* cpu_mask) {
  cpu_mask->Clear();
  int64_t cpus_left = CPU_COUNT(&cpu_set);
  for (uint64_t mask_start = 0; cpus_left > 0; mask_start += 64) {
    uint64_t current_mask = 0;

    for (uint64_t mask_offset = 0; mask_offset < 64; ++mask_offset) {
      uint64_t cpu_id = mask_start | mask_offset;
      DCHECK(cpu_id < CPU_SETSIZE);
      if (CPU_ISSET(cpu_id, &cpu_set)) {
        current_mask |= uint64_t{1} << mask_offset;
        --cpus_left;
      }
    }
    cpu_mask->Add(current_mask);
  }
  std::reverse(cpu_mask->begin(), cpu_mask->end());
}

}  // namespace

TopologyInfo FromStruct(const SysTopology::TopologyInfo& ti) {
  TopologyInfo result;
  for (const auto& level : ti.levels) {
    auto* new_level = result.add_level();
    new_level->set_mode(static_cast<LevelInfo::Mode>(level.mode));
    new_level->set_name(level.name);
    for (const auto& child : level.children) {
      auto* new_child = new_level->add_child();
      new_child->set_id(child.first);
      WriteCpuSetToProto(child.second, new_child->mutable_cpu_mask());
    }
  }
  for (const auto& cpu : ti.cpus) {
    auto* new_cpu = result.add_cpu();
    new_cpu->set_id(cpu.id);
    for (const auto& parent : cpu.parent) {
      auto* new_parent = new_cpu->add_parent();
      new_parent->set_level(parent.first);
      new_parent->set_id(parent.second);
    }
    for (const auto& sibling : cpu.siblings) {
      auto* new_sibling = new_cpu->add_sibling();
      new_sibling->set_level(sibling.first);
      WriteCpuSetToProto(sibling.second, new_sibling->mutable_cpu_mask());
    }
  }
  for (const auto& mem : ti.mems) {
    auto* new_mem = result.add_mem();
    new_mem->set_id(mem.id);
    new_mem->set_node(mem.node);
    new_mem->set_size_kb(mem.size_kb);
  }
  for (const auto& cache : ti.cache) {
    auto* new_cache = result.add_cache();
    new_cache->set_type(cache.type);
    new_cache->set_level(cache.level);
    new_cache->set_size_kb(cache.size_kb);
  }
  for (const auto& distance : ti.distances) {
    auto* new_distance = result.add_distance();
    new_distance->set_id(distance.first);
    for (const auto& cpu_set : distance.second) {
      WriteCpuSetToProto(cpu_set,
                         new_distance->add_distance()->mutable_cpu_mask());
    }
  }
  for (const auto& distance : ti.maxphysnode_distances) {
    auto* new_distance = result.add_maxphysnode_distance();
    new_distance->set_id(distance.first);
    for (const auto& distance_entry : distance.second) {
      auto* new_distance_entry = new_distance->add_distance();
      new_distance_entry->set_id(distance_entry.first);
      new_distance_entry->set_distance(distance_entry.second);
    }
  }
  return result;
}

TopologyInfo GetSystemTopologyAsProto() {
  SysTopology* topology = SysTopology::System();
  return FromStruct(topology->topology_info());
}

}  // namespace TopologyInfoConverter
}  // namespace topology
}  // namespace sysinfo
}  // namespace util
