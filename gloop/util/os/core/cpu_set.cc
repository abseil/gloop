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

// Implementation of the convenience functions for manipulating cpu_set_t
// provided by glibc.

#include "gloop/util/os/core/cpu_set.h"

#include <ctype.h>
#include <string.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/repeated_field.h"

namespace util_os_core {

static cpu_set_t compute_cpu_set_all() {
  cpu_set_t cpu_set;
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id)
    CPU_SET(cpu_id, &cpu_set);
  return cpu_set;
}

static const cpu_set_t cpu_set_all = compute_cpu_set_all();

void UInt64ToCpuSet(uint64_t cpu_mask, cpu_set_t* cpu_set) {
  int max_cpus = sizeof(cpu_mask) * 8;

  CPU_ZERO(cpu_set);
  for (int cpu_id = 0; cpu_mask != 0 && cpu_id < max_cpus; ++cpu_id) {
    if (cpu_mask & 1) {
      CPU_SET(cpu_id, cpu_set);
    }
    cpu_mask = cpu_mask >> 1;
  }
}

cpu_set_t UInt64ToCpuSet(uint64_t cpu_mask) {
  cpu_set_t cpu_set;
  UInt64ToCpuSet(cpu_mask, &cpu_set);
  return cpu_set;
}

std::string CpuSetToHexString(const cpu_set_t* cpu_set, bool add_prefix) {
  // Accumulate a vector of bytes holding the CPU bitmask.
  std::vector<uint8_t> bytes(1, 0);

  // How many CPUs do we need to find?
  int num_cpus_remaining = CPU_COUNT(cpu_set);

  // For each byte...
  for (int i = 0; num_cpus_remaining != 0 && i < CPU_SETSIZE; i += 8) {
    // For each bit...
    for (int j = 0; j < 8; ++j) {
      if (CPU_ISSET(i + j, cpu_set)) {
        bytes.back() |= (1 << j);
        num_cpus_remaining--;
      }
    }
    // If we have more CPUs left, go again.
    if (num_cpus_remaining != 0) {
      bytes.push_back(0);
    }
  }

  // Turn the result into a hex string.
  std::string result;
  for (int b = bytes.size() - 1; b >= 0; --b) {
    result += absl::StrCat(absl::Hex(bytes[b], absl::kZeroPad2));
  }
  // Strip leading '0' if it exists, to make the result more consistent with
  // standard hex formatting.
  if (result[0] == '0') {
    result = result.substr(1, result.length() - 1);
  }
  return add_prefix ? "0x" + result : result;
}

bool HexStringToCpuSet(absl::string_view in_str, cpu_set_t* cpu_set) {
  cpu_set_t tmp;
  CPU_ZERO(&tmp);

  // Chop off the leading "0x" if present.
  if (in_str.size() >= 2 && in_str[0] == '0' && tolower(in_str[1]) == 'x') {
    in_str.remove_prefix(2);
  }

  if (in_str.empty()) {
    return false;
  }

  // For each hex digit...
  int i = 0;
  for (auto it = in_str.rbegin(); it != in_str.rend(); ++it, ++i) {
    if (!isxdigit(*it)) {
      return false;
    }

    uint8_t hexit = tolower(*it);

    uint8_t val;
    if (hexit >= '0' && hexit <= '9') {
      val = hexit - '0';
    } else if (hexit >= 'a' && hexit <= 'f') {
      val = 10 + hexit - 'a';
    } else {
      LOG(FATAL) << "isxdigit() but not [0-9a-fA-F]: this should never happen";
    }

    for (int j = 0; val != 0 && j < 4; ++j) {
      uint8_t m = 1 << j;
      if (val & m) {
        CPU_SET((i * 4) + j, &tmp);
        val &= ~m;
      }
    }
  }
  memcpy(cpu_set, &tmp, sizeof(*cpu_set));
  return true;
}

cpu_set_t HexStringToCpuSet(const std::string& in_str) {
  cpu_set_t cpu_set;
  if (!HexStringToCpuSet(in_str, &cpu_set)) {
    LOG(FATAL) << "Cannot parse hex string: " << in_str;
  }
  return cpu_set;
}

void CpuSetClearSubset(const cpu_set_t* in, const cpu_set_t* to_clear,
                       cpu_set_t* result, cpu_set_t* cleared) {
  if (cleared) CpuSetAnd(cleared, in, to_clear);
  cpu_set_t cpu_set_tmp;
  CpuSetXor(&cpu_set_tmp, &cpu_set_all, to_clear);
  CpuSetAnd(result, in, &cpu_set_tmp);
}

bool CpuSetTestEmpty(const cpu_set_t* cpu_set) {
  // Profiling has found that it's faster to use CPU_COUNT() rather than using a
  // loop that tries to be efficient by returning as soon as any CPU is found.
  return CPU_COUNT(cpu_set) == 0 ? true : false;
}

int CpuSetCompare(const cpu_set_t* lhs, const cpu_set_t* rhs) {
  if (CpuSetTestEqual(lhs, rhs)) {
    return 0;
  }
  for (int cpu_id = CPU_SETSIZE - 1; cpu_id >= 0; --cpu_id) {
    bool lhs_bit = CPU_ISSET(cpu_id, lhs) ? 1 : 0;
    bool rhs_bit = CPU_ISSET(cpu_id, rhs) ? 1 : 0;
    if (!lhs_bit && rhs_bit) {
      return -1;
    }
    if (lhs_bit && !rhs_bit) {
      return 1;
    }
  }
  LOG(FATAL) << "lhs == rhs but specialised test missed";
}

cpu_set_t CpuSetMakeEmpty() {
  cpu_set_t cpu_set;
  CpuSetClear(&cpu_set);
  return cpu_set;
}

cpu_set_t ProtobufToCpuSet(
    const google::protobuf::RepeatedField<uint64_t>& pb) {
  cpu_set_t cpu_set;
  CpuSetClear(&cpu_set);
  const int pb_size = pb.size();
  for (int index = 0; index < pb_size; index++) {
    uint64_t sub_mask = pb.Get(index);
    // Convert protobuf index to cpu_set index. In CpuSetToProtobuf(), we start
    // filling the protobuf from MSB to LSB i,e pb[0] contains most significant
    // bits of the cpu_set. We need to do the same thing when we convert back
    // from proto to cpu_set.
    const int cpu_set_idx = pb_size - 1 - index;
    for (int sub_id = 0; sub_mask; ++sub_id) {
      if (sub_mask & 1) {
        CpuSetInsert(sub_id + 64ul * cpu_set_idx, &cpu_set);
      }
      sub_mask >>= 1;
    }
  }
  return cpu_set;
}

void CpuSetToProtobuf(const cpu_set_t& cpu_set,
                      google::protobuf::RepeatedField<uint64_t>* pb) {
  pb->Clear();
  uint64_t sub_mask = 0;
  int bit_count = 0;
  bool found_non_zero_sub_mask = false;
  for (int cpu_id = CPU_SETSIZE - 1; cpu_id >= 0; --cpu_id) {
    if (CpuSetContains(cpu_id, cpu_set)) {
      sub_mask |= 1;
    }
    if (++bit_count == 64) {
      bit_count = 0;
      if (sub_mask || found_non_zero_sub_mask) {
        pb->Add(sub_mask);
        found_non_zero_sub_mask = true;
      }
      sub_mask = 0;
    }
    sub_mask <<= 1;
  }
}

}  // namespace util_os_core

std::ostream& operator<<(std::ostream& o, const cpu_set_t& cpu_set) {
  o << util_os_core::CpuSetToHexString(&cpu_set);
  return o;
}
