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

#ifndef THIRD_PARTY_GLOOP_BASE_PROFILE_H_
#define THIRD_PARTY_GLOOP_BASE_PROFILE_H_

#include <cstddef>
#include <cstdint>

#include "absl/base/attributes.h"

namespace base {

// A Profile object holds a mapping from <stacktrace,tag> to
// <num,count> values where num and count are integers associated
// with samples or their sums.
//
// Profile is an interface; implementations are provided by
// sources of profile data.
class ABSL_DEPRECATED(
    "Profiler-specific types should be used for new implementation rather than "
    "base::Profile") Profile {
 public:
  Profile() {}
  virtual ~Profile() {}

  // The profile type controls the interpretation of any tags associated with
  // an entry.
  struct Entry {
    int64_t sum;            // Total added with this <stack,tag>
    int64_t count;          // Total of all added counts with this <stack,tag>
    int depth;              // Length of the stack array
    void** stack;           // Stack trace
    size_t ntags;           // Number of tags.
    const uintptr_t* tags;  // tags[0,ntags-1] hold tags for this entry.

    // If a CPU profiler is tracking latency, latency_micros holds the sum of
    // latency (in microseconds) attributable to this <stack,tag>.
    int64_t latency_micros;
  };

  // Call (*func)(arg, entry) for every entry stored in the profile.
  // "entry" and anything it points to is only guaranteed to be live
  // for the duration of the single call to Handler.
  typedef void (*Handler)(void* arg, const Entry& entry);
  virtual void Iterate(void* arg, Handler func) const = 0;

  // Return the sampling period used to collect the data.  The unit of
  // the period depends on the type of profile, and must be agreed
  // upon by the producer and consumer of the profile.
  virtual int64_t period() const { return 0; }

  // Does the profile store latency information per entry?
  virtual bool has_latency() const { return false; }

 private:
  Profile(const Profile&) = delete;
  Profile& operator=(const Profile&) = delete;

  virtual void UnusedKeyMethod();  // <link>
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_PROFILE_H_
