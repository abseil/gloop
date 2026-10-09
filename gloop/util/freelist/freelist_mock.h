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

// MockFreelist is a gMock-based version of the AbstractFreeList template class
// useful for testing.
//
// MockFreelist makes it particularly easy to inject other mock objects into
// your tests.

#ifndef THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_MOCK_H_
#define THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_MOCK_H_

#include <string>

#include "gloop/util/freelist/freelist.h"
#include "gmock/gmock.h"

template <class T>
class MockFreelist : public AbstractFreeList<T> {
 public:
  ~MockFreelist() override {}
  MOCK_METHOD(T*, New, (), (override));
  MOCK_METHOD(void, NewMany, (T * *ptr, int n), (override));
  MOCK_METHOD(void, Delete, (T * x), (override));
  MOCK_METHOD(void, DeleteMany, (T * *ptr, int n), (override));
  MOCK_METHOD(int, size, (), (const, override));
  MOCK_METHOD(bool, CheckRep, (), (const, override));
  MOCK_METHOD(bool, CanDelete, (T * x), (const, override));
  MOCK_METHOD(void, Clear, (), (override));
  MOCK_METHOD(std::string, DebugString, (), (const, override));
};

#endif  // THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_MOCK_H_
