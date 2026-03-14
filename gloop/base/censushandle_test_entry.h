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

#ifndef THIRD_PARTY_GLOOP_BASE_CENSUSHANDLE_TEST_ENTRY_H_
#define THIRD_PARTY_GLOOP_BASE_CENSUSHANDLE_TEST_ENTRY_H_

#include <cstdint>
#include <memory>

#include "absl/base/casts.h"
#include "gloop/base/censushandle.h"

// A friend of CensusHandle.
class TestEntry : public CensusHandle::EntryBase {
 public:
  uint64_t rc() const { return rc_; }
  void RefForTest() { Ref(); }
  void UnrefForTest() { Unref(); }
  bool UnrefNoDeleteForTest() { return UnrefNoDelete(); }

  static CensusHandle Wrap(TestEntry* e) {
    return CensusHandle(std::unique_ptr<CensusHandle::EntryBase>(e));
  }

  static TestEntry* Get(const CensusHandle& h) {
    return absl::down_cast<TestEntry*>(h.get_entry());
  }

  static void ResetRep(CensusHandle& h) { h.set_entry(nullptr); }
};
#endif  // THIRD_PARTY_GLOOP_BASE_CENSUSHANDLE_TEST_ENTRY_H_
