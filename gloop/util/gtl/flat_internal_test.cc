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

#include "gloop/util/gtl/flat_internal.h"

#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/strings/cord.h"
#include "gloop/util/gtl/requires.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace internal_flat {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

TEST(FlatInternalTest, VerifyHintForEmptyArray) {
  const std::vector<int> rep;
  const std::less<int> cmp;
  // Any hint in an empty array is perfect.
  EXPECT_THAT(verify_hint(rep, rep.end(), 1, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.end()));
}

TEST(FlatInternalTest, VerifyPerfectHint) {
  const std::vector<int> rep = {2, 4, 6};
  const std::less<int> cmp;
  // Trying perfect begin/end hints.
  EXPECT_THAT(verify_hint(rep, rep.begin(), 1, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.begin()));
  EXPECT_THAT(verify_hint(rep, rep.end(), 7, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.end()));
  // Trying perfect non-begin/end hints.
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 3, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.begin() + 1));
  EXPECT_THAT(verify_hint(rep, rep.begin() + 2, 5, cmp),
              Pair(VerifyHintResult::kPerfectHint, rep.begin() + 2));
  // Not testing duplicates here, as there could be no perfect hint for them.
}

TEST(FlatInternalTest, VerifyBadHint) {
  const std::vector<int> rep = {2, 4, 6};
  const std::less<int> cmp;
  // Trying bad begin/end hints for non-duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin(), 3, cmp).first,
              VerifyHintResult::kBadHint);
  EXPECT_THAT(verify_hint(rep, rep.end(), 1, cmp).first,
              VerifyHintResult::kBadHint);
  // Trying bad begin/end hints for duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin(), 2, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.begin()));
  EXPECT_THAT(verify_hint(rep, rep.end(), 6, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.end() - 1));
  // Trying bad non-begin/end hints for non-duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 5, cmp).first,
              VerifyHintResult::kBadHint);
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 1, cmp).first,
              VerifyHintResult::kBadHint);
  // Trying bad non-begin/end hints for duplicates.
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 6, cmp).first,
              VerifyHintResult::kBadHint);
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 4, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.begin() + 1));
  EXPECT_THAT(verify_hint(rep, rep.begin() + 1, 2, cmp),
              Pair(VerifyHintResult::kKeyExists, rep.begin()));
}

TEST(FlatInternalTest, MultiInsertUniqueWithPerfectHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep;
  // Any hint in an empty array is perfect.
  multi_insert_hint(&rep, rep.end(), std::make_pair(1, 1), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1)));
  // Insert unique item with hint.
  multi_insert_hint(&rep, rep.end(), std::make_pair(2, 2), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1), Pair(2, 2)));
  // Insert unique item with hint.
  multi_insert_hint(&rep, rep.begin(), std::make_pair(0, 0), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 1), Pair(2, 2)));
}

TEST(FlatInternalTest, MultiInsertDuplicatesWithPerfectHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep = {{1, 1}};
  // Insert a duplicate before.
  multi_insert_hint(&rep, rep.begin(), std::make_pair(1, 2), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 2), Pair(1, 1)));
  // Insert a duplicate after.
  multi_insert_hint(&rep, rep.end(), std::make_pair(1, 3), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 2), Pair(1, 1), Pair(1, 3)));
  // Insert a duplicate in the middle.
  multi_insert_hint(&rep, rep.begin() + 1, std::make_pair(1, 4), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 2), Pair(1, 4), Pair(1, 1), Pair(1, 3)));
}

TEST(FlatInternalTest, MultiInsertUniqueWithBadHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep = {{1, 1}, {2, 2}};
  multi_insert_hint(&rep, rep.begin(), std::make_pair(3, 3), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3)));
  multi_insert_hint(&rep, rep.end(), std::make_pair(0, 0), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 1), Pair(2, 2), Pair(3, 3)));
}

TEST(FlatInternalTest, MultiInsertDuplicatesWithBadHint) {
  const value_compare<std::less<int>> cmp;
  std::vector<std::pair<int, int>> rep = {{0, 0}, {1, 1}, {2, 2}};
  // Insert a duplicate with a hint before a correct one. Insert at lower bound.
  multi_insert_hint(&rep, rep.begin(), std::make_pair(1, 2), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 2), Pair(1, 1), Pair(2, 2)));
  // Insert a duplicate with a hint after a correct one. Insert at upper bound.
  multi_insert_hint(&rep, rep.end(), std::make_pair(1, 0), cmp);
  EXPECT_THAT(rep, ElementsAre(Pair(0, 0), Pair(1, 2), Pair(1, 1), Pair(1, 0),
                               Pair(2, 2)));
}

struct EmptyRep {
  using const_pointer = const char*;
  using pointer = char*;
};

struct RepWithData {
  using const_pointer = const char*;
  using pointer = char*;
  const_pointer data() const noexcept { return nullptr; }
  pointer data() noexcept { return nullptr; }
};

template <typename Rep>
class SampleFlatContainer
    : public FlatContainersMaybeExportData<Rep, SampleFlatContainer<Rep>> {
  friend const Rep& internal_flat::GetInternalRepresentation<>(
      const SampleFlatContainer& container);

  Rep& rep() { return rep_; }
  const Rep& rep() const { return rep_; }

  Rep rep_;
};

template <typename T>
inline constexpr bool kHasData =
    gtl::Requires<T>([](auto&& t) -> decltype(t.data()) {});

TEST(FlatInternalTest, FlatContainersMaybeExportDataWithoutData) {
  static_assert(!kHasData<SampleFlatContainer<EmptyRep>>,
                "FlatContainer shouldn't have data() if the rep doesn't");
  // Make sure instantiating such flat container doesn't lead to an error.
  (void)SampleFlatContainer<EmptyRep>{};
}

TEST(FlatInternalTest, FlatContainersMaybeExportDataWithData) {
  SampleFlatContainer<RepWithData> cont{};
  EXPECT_TRUE(cont.data() == nullptr);
  static_assert(!std::is_assignable<decltype(*cont.data()), char>());
  const SampleFlatContainer<RepWithData> const_cont{};
  EXPECT_TRUE(const_cont.data() == nullptr);
  static_assert(!std::is_assignable<decltype(*const_cont.data()), char>());
}

TEST(FlatInternalTest, ConstexprAssigmentWorks) {
  std::tuple<std::pair<int, int>, int> array[] = {
      std::make_tuple(std::make_pair(0, 1), 2),
      std::make_tuple(std::make_pair(3, 4), 5)};
  EXPECT_NE(array[0], array[1]);
  ConstexprHelper<std::tuple<std::pair<int, int>, int>>::Assign(&array[0],
                                                                &array[1]);
  EXPECT_EQ(array[0], array[1]);
}

TEST(FlatInternalTest, DefaultStringLess) {
  auto less = DefaultLess<std::string>{};

  EXPECT_TRUE(less(std::string("abc"), absl::Cord("def")));
  EXPECT_TRUE(less(std::string("abc"), absl::Cord("abcdef")));
  EXPECT_FALSE(less(std::string("abc"), absl::Cord("abc")));
  EXPECT_FALSE(less(std::string("abcdef"), absl::Cord("abc")));
  EXPECT_FALSE(less(std::string("def"), absl::Cord("abc")));
}

}  // namespace
}  // namespace internal_flat
}  // namespace gtl
