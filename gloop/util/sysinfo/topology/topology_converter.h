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

#ifndef THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_CONVERTER_H_
#define THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_CONVERTER_H_

#include "gloop/util/sysinfo/topology/topology.h"
#include "gloop/util/sysinfo/topology/topology.pb.h"

namespace util {
namespace sysinfo {
namespace topology {
namespace TopologyInfoConverter {

// Converts a TopologyInfo proto into a SysTopology::TopologyInfo struct.
SysTopology::TopologyInfo FromProto(const TopologyInfo& ti);
// Converts a SysTopology::TopologyInfo struct into a TopologyInfo proto.
TopologyInfo FromStruct(const SysTopology::TopologyInfo& ti);

// Wrapper that calls the SysTopology instance and converts the struct
// to a proto.
TopologyInfo GetSystemTopologyAsProto();

}  // namespace TopologyInfoConverter
}  // namespace topology
}  // namespace sysinfo
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_TOPOLOGY_CONVERTER_H_
