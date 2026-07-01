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

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_GLOBAL_ID_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_GLOBAL_ID_H_
#include <cstdint>

// NewGlobalID generates a 64bit number that's likely to be globally
// unique across machines. It also ensures that lower bits are
// sufficiently random that they can be used by Dapper for sampling.
//
// Internally, NewGlobalID keeps three quantities for each caller
// thread: 52bit upper_bits, 12bit lower_bits, and 12bit
// increment. They are initialized to random values. For the first
// 4095 (= 2**12 - 1) calls to NewGlobalID() by the same thread is
// implemented as below:
//
//  lower_bits = (lower_bits + increment) % 4096;
//  return upper_bits | lower_bits;
//
// On every 4096th call to NewGlobalID by a thread, we reset
// upper_bits, lower_bits, and increment to new random values.
//
// The probability of collision is 1/2**52 in the worst case in which
// each thread generates just one ID and dies. The probability is
// 1/2**64 if each thread generates more than 4096 IDs each.
//
// We generate random numbers using a combination of the CPU
// cycleclock, process ID, and the current time.

namespace util {
namespace random {

uint64_t NewGlobalID();

// Reset the per-thread generator. Intended only for unittests.
void ResetPerThreadGlobalIDGenerator();

}  // namespace random.
}  // namespace util.

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_GLOBAL_ID_H_
