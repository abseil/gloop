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

#include "gloop/base/reference_tracker.h"

#include <string.h>

#include <vector>

#include "absl/base/attributes.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"

namespace base {

TEST(ReferenceTrackerTest, NoReferences) {
  ReferenceTracker t;
  std::vector<ReferenceTracker::StackTrace> traces;
  t.GetReferenceTraces(&traces);
  EXPECT_EQ(0, traces.size());
}

TEST(ReferenceTrackerTest, MultipleRefsSameOwner) {
  ReferenceTracker t;
  int owner;
  t.Ref(&owner);
  EXPECT_DEATH(t.Ref(&owner), "Already have a ref from");
}

TEST(ReferenceTrackerTest, UnrefNoOwner) {
  ReferenceTracker t;
  int owner;
  EXPECT_DEATH(t.Unref(&owner), "No ref from");
}

static void ABSL_ATTRIBUTE_NOINLINE
CallReferenceTrackerRef(ReferenceTracker* t) {
  int owner;
  t->Ref(&owner);
}

TEST(ReferenceTrackerTest, StackTrace) {
  ReferenceTracker t;
  std::vector<ReferenceTracker::StackTrace> traces;
  CallReferenceTrackerRef(&t);
  t.GetReferenceTraces(&traces);

  ASSERT_EQ(1, traces.size());
  ASSERT_GE(traces[0].size(), 1);
  char buf[1000];
  absl::Symbolize(const_cast<void*>(traces[0][0]), buf, sizeof(buf));

  EXPECT_TRUE(nullptr != strstr(buf, "base::CallReferenceTrackerRef()")) << buf;

  // symbolize the rest for debugging
  for (ReferenceTracker::StackTrace::const_iterator it = traces[0].begin();
       it != traces[0].end(); ++it) {
    absl::Symbolize(const_cast<void*>(*it), buf, sizeof(buf));
    VLOG(1) << buf;
  }
}

}  // namespace base
