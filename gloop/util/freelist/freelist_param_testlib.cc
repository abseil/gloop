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

#include "gloop/util/freelist/freelist_param_testlib.h"

#include <stdio.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "gloop/util/freelist/freelist.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

namespace freelist {
namespace {

TEST_P(FreeListConformanceTest, InitialState) {
  std::unique_ptr<AbstractFreeList<EnsureDeleted>> fl =
      GetParam().create_freelist();

  std::string debug_str = fl->DebugString();
  printf("DebugString (before) %s", debug_str.c_str());
  RE2 r1("(size)|(hot):");
  RE2 num("\\d+");
  CHECK(RE2::PartialMatch(debug_str, r1));
  CHECK(RE2::PartialMatch(debug_str, num));
  // Free list fl is initially empty
  CHECK(fl->CheckRep());
  CHECK_EQ(fl->size(), 0);

  // "Delete(NULL)" should be a noop, just like "delete NULL"
  fl->Delete(nullptr);
  CHECK_EQ(fl->size(), 0);
}

TEST_P(FreeListConformanceTest, SingleItem) {
  std::unique_ptr<AbstractFreeList<EnsureDeleted>> fl =
      GetParam().create_freelist();
  const int size = GetParam().size;

  // Simple case: creates and deletes a single freelist item.
  EnsureDeleted* const ed = fl->New();
  CHECK(ed != nullptr);  // not the NULL we just "deleted"
  CHECK_EQ(fl->size(), 0);
  CHECK(fl->CheckRep());
  fl->Delete(ed);
  CHECK_EQ(fl->size(), std::min(size, 1));
  CHECK_EQ(EnsureDeleted::live(), std::min(size, 1));
  CHECK(fl->CheckRep());
}

TEST_P(FreeListConformanceTest, CanDelete) {
  std::unique_ptr<AbstractFreeList<EnsureDeleted>> fl =
      GetParam().create_freelist();
  const int size = GetParam().size;

  // CanDelete() test.
  EnsureDeleted* const ed2 = fl->New();
  CHECK(ed2 != nullptr);
  CHECK(fl->CheckRep());
  CHECK(fl->CanDelete(ed2));
  fl->Delete(ed2);
  CHECK(fl->CheckRep());
  CHECK(size == 0 || !fl->CanDelete(ed2));
}

TEST_P(FreeListConformanceTest, MultipleItems) {
  const auto& [test_name, create_freelist, size, can_be_lazy] = GetParam();
  std::unique_ptr<AbstractFreeList<EnsureDeleted>> fl = create_freelist();

  // Adds multiple elements to the list.  Deletes all elements individually
  // after testing for an overload condition.
  std::vector<EnsureDeleted*> elems;
  elems.reserve(size);
  for (int n = 0; n < size; ++n) {
    elems.push_back(fl->New());
    CHECK_EQ(EnsureDeleted::live(), fl->size() + elems.size());
    if (can_be_lazy) {
      CHECK_GE(fl->size(), 0);
      CHECK_GE(EnsureDeleted::live(), elems.size());
    } else {
      CHECK_EQ(fl->size(), 0);
      CHECK_EQ(EnsureDeleted::live(), elems.size());
    }
    CHECK(fl->CheckRep());
  }

  // Tests for overload.
  EnsureDeleted* const over = fl->New();
  CHECK_EQ(EnsureDeleted::live(), fl->size() + elems.size() + 1);
  if (!can_be_lazy) {
    CHECK_EQ(fl->size(), 0);
    CHECK_EQ(EnsureDeleted::live(), size + 1);
  }
  CHECK(fl->CheckRep());

  fl->Delete(over);
  CHECK_EQ(EnsureDeleted::live(), fl->size() + elems.size());
  if (size == 0) {
    CHECK_EQ(fl->size(), 0);
  } else if (can_be_lazy) {
    CHECK_GE(fl->size(), 1);
  } else {
    CHECK_EQ(fl->size(), 1);
  }
  CHECK(fl->CheckRep());

  for (int n = 0; n < size; ++n) {
    fl->Delete(elems[n]);
    if (can_be_lazy) {
      CHECK_EQ(EnsureDeleted::live(), fl->size() + size - n - 1);
    } else {
      CHECK_EQ(EnsureDeleted::live(), n == size - 1 ? size : size + 1);
      CHECK_EQ(fl->size(), std::min(size, 2 + n));
    }
    CHECK(fl->CheckRep());
  }
  for (int n = 0; n < size; ++n) {
    EnsureDeleted* elem = fl->New();
    CHECK(fl->CheckRep());
    CHECK_EQ(EnsureDeleted::live(), fl->size() + 1);
    if (!can_be_lazy) {
      CHECK_EQ(fl->size(), size - 1);
    }
    elem->set_val(15);
    CHECK_EQ(elem->val(), 15);
    CHECK(fl->CheckRep());
    fl->Delete(elem);
  }
  CHECK_EQ(EnsureDeleted::live(), fl->size());
  if (!can_be_lazy) {
    CHECK_EQ(fl->size(), size);
    CHECK_EQ(size, EnsureDeleted::live());
  }
}

TEST_P(FreeListConformanceTest, DeleteMany) {
  const auto& [test_name, create_freelist, size, can_be_lazy] = GetParam();
  std::unique_ptr<AbstractFreeList<EnsureDeleted>> fl = create_freelist();

  // Check if DeleteMany deletes all objects and simultaneously checks if
  // DeleteMany skips over NULL ptrs.
  std::vector<EnsureDeleted*> elems;
  elems.reserve(size);
  for (int n = 0; n < size; ++n) {
    elems.push_back((n % 2) == 0 ? nullptr : fl->New());
    if (can_be_lazy) {
      CHECK_GE(fl->size(), 0);
      CHECK_GE(EnsureDeleted::live(), elems.size() / 2);
    } else {
      CHECK_EQ(fl->size(), 0);
      CHECK_EQ(EnsureDeleted::live(), elems.size() / 2);
    }
    CHECK(fl->CheckRep());
  }
  if (!elems.empty()) {
    fl->DeleteMany(&elems[0], elems.size());
  }
  CHECK_EQ(EnsureDeleted::live(), fl->size());
  if (!can_be_lazy) {
    CHECK_EQ(fl->size(), size / 2);
    CHECK_EQ(size / 2, EnsureDeleted::live());
  }
}

TEST_P(FreeListConformanceTest, NewMany) {
  const auto& [test_name, create_freelist, size, can_be_lazy] = GetParam();
  std::unique_ptr<AbstractFreeList<EnsureDeleted>> fl = create_freelist();

  // Test calls to NewMany().  Before calling NewMany(), we initialize the
  // FreeList to contain various numbers of existing elements.  This is so
  // we can test the nontrivial code in AffinitizedFreeList.
  for (int initial_num = 0; initial_num <= size; ++initial_num) {
    for (int num_to_NewMany = 0; num_to_NewMany <= size * 2; ++num_to_NewMany) {
      fl->Clear();
      // Pre-allocate a bunch of elements, then delete them so they're back on
      // the FreeList.
      std::vector<EnsureDeleted*> elems;
      for (int i = 0; i < initial_num; ++i) {
        elems.push_back(fl->New());
      }
      for (int i = 0; i < initial_num; ++i) {
        fl->Delete(elems[i]);
      }
      // Now call NewMany.  This may return all pre-allocated items, or all-new
      // items, or some combination of both.
      elems.resize(num_to_NewMany);
      for (int i = 0; i < num_to_NewMany; ++i) {
        elems[i] = nullptr;
      }
      if (!elems.empty()) {
        fl->NewMany(&(elems[0]), num_to_NewMany);
      }
      for (int i = 0; i < num_to_NewMany; ++i) {
        CHECK(elems[i] != nullptr);
      }
      CHECK_EQ(EnsureDeleted::live(), fl->size() + elems.size());
      if (can_be_lazy) {
        CHECK_GE(fl->size(), std::max(initial_num - num_to_NewMany, 0));
        CHECK_GE(EnsureDeleted::live(), std::max(initial_num, num_to_NewMany));
      } else {
        CHECK_EQ(fl->size(), std::max(initial_num - num_to_NewMany, 0));
        CHECK_EQ(EnsureDeleted::live(), std::max(initial_num, num_to_NewMany));
      }
      CHECK(fl->CheckRep());
      if (!elems.empty()) {
        fl->DeleteMany(&elems[0], elems.size());
      }
      CHECK_EQ(EnsureDeleted::live(), fl->size());
    }
  }
}

}  // namespace

std::string GetTestName(
    const testing::TestParamInfo<FreeListTestParams>& info) {
  return info.param.test_name.empty() ? testing::PrintToString(info.index)
                                      : info.param.test_name;
}

}  // namespace freelist
