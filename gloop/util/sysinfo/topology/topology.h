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

// Interface to the CPU and memory bank topology of a system.

#ifndef THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_H_
#define THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_H_

#include <sched.h>
#include <sys/types.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gloop/util/os/core/cpu_set.h"
#include "gloop/util/sysinfo/topology/affinity.h"

ABSL_DECLARE_FLAG(std::string, sysinfo_topology_path);

class SysTopology {
 public:
  typedef int64_t Id;

  virtual int NumLevels() const { return static_cast<int>(levels_.size()); }

  // Return an indication of the richness of this topology. In
  // general, it can be assumed that a topology with a higher order is
  // "more correct" than one for the same system but with a lower order.
  virtual int64_t Order() const { return order_; }

  // Find a level corresponding to a given architectural name. Multiple names
  // may map to the same level if the topology has been collapsed to remove
  // redundancy. Currently the possible levels are: core, cpu, l1cache, l2cache,
  // l3cache, node, package, and machine.
  virtual int FindLevel(const std::string& name) const;

  // The number of entities at a given level
  virtual int LevelCount(int level) const;

  virtual const std::set<std::string>& LevelNames(int level) const {
    return levels_[level].names;
  }

  // The level corresponding to distinct cores
  virtual int core_level() const { return core_level_; }

  virtual cpu_set_t GetAllCPUs() const { return all_cpus_; }
  // Deprecated API.
  virtual void all_cpus(cpu_set_t* result_set) const {
    *result_set = all_cpus_;
  }

  virtual const std::set<Id>& all_mems() const { return all_mems_; }

#ifndef SWIG
  // The parent of a CPU at a given level
  virtual Id ParentOf(Id cpu, int level) const {
    return levels_[level].parent[cpu];
  }

  // The child CPUs of an entity at a given level.
  virtual cpu_set_t ChildrenOf(Id id, int level) const {
    if (id >= levels_[level].children.size()) {
      LOG_EVERY_N_SEC(ERROR, 3600) << "invalid id: " << id;
      return {};
    }
    return levels_[level].children[id];
  }
  // Deprecated API.
  virtual void ChildrenOf(Id id, int level, cpu_set_t* result_set) const {
    *result_set = ChildrenOf(id, level);
  }

  // The siblings of a CPU at a given level.
  virtual cpu_set_t SiblingsOf(Id cpu, int level) const {
    return levels_[level].siblings[cpu];
  }
  // Deprecated API.
  virtual void SiblingsOf(Id cpu, int level, cpu_set_t* result_set) const {
    *result_set = SiblingsOf(cpu, level);
  }
#endif  // SWIG

  // Some helpers for commonly-used combinations.
  //
  // Note, NumCores typically returns 1 on Ubiquity instances irrespective of
  // the expected virtual core count.
  virtual int NumCPUs() const { return LevelCount(0); }
  virtual int NumCores() const { return LevelCount(core_level_); }
  virtual int NumCaches() const { return LevelCount(l3_cache_level_); }
  // Returns the number of hyperthread siblings (CPUs) per core.
  virtual int NumHyperthreadSiblings() const {
    if (NumCores() == 0) {
      LOG_EVERY_N_SEC(ERROR, 60)
          << "NumCores() is 0; Returning 1 for NumHyperthreadSiblings() to "
             "avoid division by 0. This function called "
          << COUNTER << " times so far with 0 NumCores().";
      return 1;
    }
    return NumCPUs() / NumCores();
  }
  // Total number of nodes; includes both nodes with CPUs on them and nodes that
  // are memory-only.
  virtual int NumNodes() const { return LevelCount(node_level_); }
  virtual int NumPackages() const { return LevelCount(package_level_); }
  virtual Id CoreOf(Id cpu) const { return ParentOf(cpu, core_level_); }
  virtual Id CacheOf(Id cpu) const { return ParentOf(cpu, l3_cache_level_); }
  virtual Id NodeOf(Id cpu) const { return ParentOf(cpu, node_level_); }
  virtual Id PackageOf(Id cpu) const { return ParentOf(cpu, package_level_); }

  // Only meaningful on platforms with CCD. Callers of these two functions
  // should check the platform it's running on.
  // On Zen2/Rome platform, we have two caches (CCXs) per CCD. On other AMD
  // platforms, we have one CCX per CCD. Please update the functions if there
  // is a new AMD platform which has multiple CCXs per CCD. (See: b/150410597).
  virtual Id CCDOf(Id cpu) const;
  virtual int NumCCDs() const;

  virtual std::set<Id> NodesOfPackage(Id package_id) const;
  virtual Id PackageOfNode(Id node_id) const;

  // Find L3 caches that belong to a node and vice versa.
  virtual std::set<Id> CachesOfNode(Id node_id) const;
  // Returns -1 if we can't find a node associated with |cache_id|.
  virtual Id NodeOfCache(Id cache_id) const;

  virtual cpu_set_t CoreSiblings(Id cpu) const {
    return SiblingsOf(cpu, core_level_);
  }
  // Deprecated API.
  virtual void CoreSiblings(Id cpu, cpu_set_t* result_set) const {
    *result_set = CoreSiblings(cpu);
  }
  virtual cpu_set_t CoreCPUs(Id core) const {
    return ChildrenOf(core, core_level_);
  }
  // Deprecated API.
  virtual void CoreCPUs(Id core, cpu_set_t* result_set) const {
    *result_set = CoreCPUs(core);
  }
  virtual cpu_set_t CacheCPUs(Id cache) const {
    return ChildrenOf(cache, l3_cache_level_);
  }
  virtual cpu_set_t NodeCPUs(Id node) const {
    return ChildrenOf(node, node_level_);
  }
  // Deprecated API.
  virtual void NodeCPUs(Id node, cpu_set_t* result_set) const {
    *result_set = NodeCPUs(node);
  }
  virtual cpu_set_t PackageCPUs(Id package) const {
    return ChildrenOf(package, package_level_);
  }

  // Count how many distinct cores are encompassed by a cpu set.
  virtual int CountCores(const cpu_set_t* cpu_set) const;

  // This function returns a score in range [0, 1] which is:
  // 1.0 if they share non-last-level-cache;
  // 0.7 if they share last-level-cache;
  // 0.4 if they are on the same package but do not share any cache;
  // 0.1 if they are on diff packages.
  // It is not meant to be proportional to latency or other specific
  // characteristics of the underlying architecture.
  virtual double CPUPairCacheLocalityScore(Id a, Id b) const;

  // Calculates the L3 Cache Compactness Score (<link>)
  // for a given CPU mask, which measures the spread of CPUs across L3 caches
  // (1 = ideal, 0 = worst-case).
  // Formula:
  //   min_caches_needed = ceil(num_cpus_used / num_cpus_per_cache)
  //   max_caches_possible = min(num_cpus_used, system_cache_count)
  //   CCS = 1 - (num_caches_used - min_caches_needed)
  //             / (max_caches_possible - min_caches_needed)
  //   CCS is 1 when max_caches_possible == min_caches_needed.
  virtual double CacheCompactnessScore(const cpu_set_t& cpu_set) const;

  // return system wide last level cache
  // if the LLC size is 10 KB and it has two package, it will return 20 KB
  // return -1, if not available
  virtual int LastLevelCacheSizeAggregate() const;
  // return last level cache per package
  virtual int LastLevelCacheSizePerPackage() const;
  // return the number of last level cache domains
  virtual int LastLevelCacheDomainCount() const;

  // return node distance (as reported by kernel) of two phys nodes
  virtual int NodePairMemDistance(Id a, Id b) const;
  // The physical node for a given memory bank
  virtual Id NodeOfMem(Id mem) const;
  // Total memory size (in kb) on this node.
  virtual int64_t NodeMemSize(Id node) const;

  // Minimum memory size (in kb) per node among all the nodes.
  virtual int64_t MinNodeMemSize() const;

  // Number of nodes with CPUs on them; there are topologies that have
  // a number of memory-only nodes, without any CPUs, together with nodes that
  // have both memory and CPUs on them.
  virtual int32_t NumNodesWithCpus() const;

  // Ids of nodes with CPUs on them.
  virtual std::vector<Id> NodeIdsWithCpus() const;

  // Ids of caches with CPUs on them.
  virtual std::vector<Id> CacheIdsWithCpus() const;

  // Returns true if the platform is chiplet based.
  virtual bool IsChipletBased() const;

#ifndef SWIG
  // Structures used to set up a SysTopology

  // You should pass in:
  // - a vector of levels, each with a name and a mapping mode.
  // - a vector of CPUs, each with an id

  // The parent Id for a CPU at each level
  typedef std::map<std::string, Id> CPUParentMap;
  // The siblings for a CPU at each level
  typedef std::map<std::string, cpu_set_t> CPUSiblingMap;
  // The CPUs on each entity at a level
  typedef std::map<Id, cpu_set_t> ChildMap;

  // All the per-CPU information that we know
  struct CPUInfo {
    Id id;
    CPUParentMap parent;
    CPUSiblingMap siblings;
  };

  // All the per-level information that we know
  struct LevelInfo {
    // The various ways of identifying the structure of each level
    enum Mode {
      // No mapping necessary - represents individual CPUs
      CPUID,
      // LevelInfo should contain mapping from parent to its child cpu_set_t
      CHILDMAP,
      // CPUInfo.parent[levelname] should contain id of parent
      PARENTMAP,
      // CPUInfo.siblings[levelname] should contain cpu_set_t of sibling CPUs
      SIBLINGMAP
    } mode;
    std::string name;
    ChildMap children;
  };

  struct CacheInfo {
    std::string type;
    int level;
    int size_kb;
  };

  // Information we know about a memory bank - currently just which
  // physical node it's on, based on cpumap
  struct MemInfo {
    Id id;
    Id node;
    int64_t size_kb;

    MemInfo() : id(0), node(0), size_kb(0) {}
    MemInfo(Id i, Id n) : id(i), node(n), size_kb(0) {}
  };

  // All the relevant facts that we know about the system topology
  struct TopologyInfo {
    std::vector<LevelInfo> levels;
    std::vector<CPUInfo> cpus;
    std::vector<MemInfo> mems;
    std::vector<CacheInfo> cache;
    std::map<int, std::set<cpu_set_t, util_os_core::CpuSetLessThan> > distances;
    std::map<int, std::map<int, int> > maxphysnode_distances;
    void Clear() {
      levels.clear();
      cpus.clear();
      mems.clear();
      distances.clear();
      maxphysnode_distances.clear();
    }
  };

  // Use the passed TopologyInfo as a set of constraints to construct
  // a multi-level topology map.
  explicit SysTopology(TopologyInfo ti);

  // This type is neither copyable nor movable.
  SysTopology(const SysTopology&) = delete;
  SysTopology& operator=(const SysTopology&) = delete;

  virtual ~SysTopology() {}

  TopologyInfo topology_info() const { return topology_info_; }
#endif  // SWIG

  // Determine the topology for the system by any appropriate means
  static SysTopology* System();

#ifndef SWIG
  // UncachedSystem() is like System() but doesn't cache the result.
  static SysTopology* UncachedSystem();
#endif  // SWIG

  // Create a simple topology with the given number of CPUs and HT
  // mode. Useful for mocking
  enum HTMode {
    HT_NONE,         // No hyperthreading
    HT_ADJACENT,     // adjacent logical CPU ids are on the same core
    HT_NONADJACENT,  // Modern order: 0<=>2, 1<=>3 for four CPUs.
  };

  // TEST ONLY: following methods are convenience methods for tests
  static SysTopology* SimpleTopology(int num_cpus, HTMode ht);
  static SysTopology* SimpleTopology(int num_cpus, HTMode ht,
                                     int cpus_per_core);
  static SysTopology* SimpleTopology(int num_nodes, int per_node_cpus,
                                     HTMode ht, int per_node_mem_kb);
  static SysTopology* SimpleTopology(int num_nodes, int per_package_nodes,
                                     int per_node_cpus, HTMode ht,
                                     int per_node_mem_kb);
  virtual std::string ToString() const;

  virtual cpu_set_t low_latency_cpus() const { return low_latency_cpus_; }
  // Deprecated API.
  virtual void low_latency_cpus(cpu_set_t* result_set) const {
    *result_set = low_latency_cpus();
  }

 protected:
  // Creates a TopologyInfo objects based on the number of CPUs, and CPUs
  // per core.
  static TopologyInfo TopologyInfoFromNumCpus(int num_cpus, HTMode ht,
                                              int ht_cpus_per_core);

 private:
  // Internal vectors and cpu_set_ts representing the state exported via
  // the public interface.

  struct LevelInfoInt {
    std::set<std::string> names;
    int count;
    std::vector<Id> parent;
    std::vector<cpu_set_t> children;
    std::vector<cpu_set_t> siblings;
    std::vector<std::set<Id> > mems;
  };

  struct CacheInfoInt {
    std::string type;
    int level;
    // size of the cache
    int size_kb;
    // number of such cache in the system
    int num;
  };

  std::vector<LevelInfoInt> levels_;
  std::vector<CacheInfoInt> cache_;
  cpu_set_t all_cpus_;
  std::set<Id> all_mems_;
  std::map<Id, int64_t> node_mems_;

  absl::flat_hash_map<std::string, int> levelnames_;

  // The level at which cores are distinct entities
  int core_level_;

  // The level at which l3 caches are distinct entities
  int l3_cache_level_;

  // The level at which nodes are distinct entities
  int node_level_;

  // The level at which packages are distinct entities
  int package_level_;

  int order_;

  std::vector<SysTopology::Id> nodes_with_cpus_;
  std::vector<SysTopology::Id> caches_with_cpus_;

  class TopologySorter;

  // CPU set for nodes which have a lower average memory latency than others.
  cpu_set_t low_latency_cpus_;

  // essentially a 2D array, entry [i][j] is the distance form
  // phys_node(i) to phys_node(j).
  std::map<int, std::map<int, int> > physnode_distance;

  // The topology info struct that was used to initialize this class.
  const TopologyInfo topology_info_;
};

// Namespace for some static helper functions
class SysTopologyHelper {
 public:
  // Limit current thread to a set of CPUs that are well connected and therefore
  // experience lower memory latency. This is useful to get deterministic
  // behavior out of applications that write to frequently accessed data
  // structures which don't fit in the cache from an initialization thread.
  //
  static void ConstrainThreadToLowLatencyNodes(cpu_set_t* old_cpuset);
  static void UnconstrainThread(const cpu_set_t* old_cpu_set) {
    SetCPUSet(old_cpu_set);
  }

  // Restrict the current thread to the set of CPUs given by cpu_set
  static bool SetCPUSet(const cpu_set_t* cpu_set) {
    return util::sysinfo::topology::SetCPUSet(cpu_set);
  }
  // Get the set of CPUs the current thread is allowed to run on
  static bool GetCPUSet(cpu_set_t* result_set) {
    return util::sysinfo::topology::GetCPUSet(result_set);
  }

  // Restrict a thread or process to the set of CPUs given by cpu_set;
  // (a pid of zero affects the current process).
  // returns a true if successful.
  static bool SetCPUSet(pid_t pid, const cpu_set_t* cpu_set) {
    return util::sysinfo::topology::SetCPUSet(pid, cpu_set);
  }
  // Get the set of CPUs a thread or process is allowed to run on.
  // (a pid of zero returns the current process).
  static bool GetCPUSet(pid_t pid, cpu_set_t* result_set) {
    return util::sysinfo::topology::GetCPUSet(pid, result_set);
  }
};

#ifndef SWIG
class SysTopologyGenerator {
 public:
  typedef SysTopology* (SysTopologyGenerator::*Generator)() const;

  // Uses standard system files.
  SysTopologyGenerator() : path_prefix_() {}

  // For testing: prepends path_prefix to system files.
  explicit SysTopologyGenerator(absl::string_view path_prefix)
      : path_prefix_(path_prefix) {}

  // This type is neither copyable nor movable.
  SysTopologyGenerator(const SysTopologyGenerator&) = delete;
  SysTopologyGenerator& operator=(const SysTopologyGenerator&) = delete;

  // Runs all the other generators and returns the best one.
  SysTopology* System() const;

  // Use information in /sys to determine topology
  SysTopology* FromSysfs() const;

  // Use information in /proc/cpuinfo in more recent kernels to
  // determine topology
  SysTopology* FromProcCPUInfo() const;
  absl::StatusOr<SysTopology::TopologyInfo> TopologyInfoFromProcCPUInfo() const;

  // Create a plausible topology based on the number of CPUs and
  // whether HT is enabled
  SysTopology* FromNumCPUs() const;

 private:
  std::vector<SysTopology::Id> GetMachineNodeIdsFromSysfs() const;

  // Read cache sharing info
  void ReadCacheInfo(SysTopology::TopologyInfo* ti) const;

  // Read MemTotal for each node from /sys/devices/system/node into ti.
  void ReadNumaMemNodes(SysTopology::TopologyInfo* ti) const;

  // Read any available NUMA CPU map from /sys into ti
  void ReadNumaCPUMap(SysTopology::TopologyInfo* ti) const;

  bool ReadCPUSet(const char* path, cpu_set_t* result_set) const;

  // Make SysTopology aware of SNC cache hierarchy by setting l3cache cpu_set
  // identical to its numa node cpu_set for each cpu.
  void UpdateCacheInfoForSnc(SysTopology::TopologyInfo* ti) const;

  const std::string path_prefix_;
};
#endif  // SWIG

#endif  // THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_H_
