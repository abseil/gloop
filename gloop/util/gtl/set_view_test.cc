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

#include "gloop/util/gtl/set_view.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/meta/internal/constexpr_testing.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/flat_set.h"
#include "gloop/util/gtl/iterator_adaptors.h"

namespace gtl {
namespace {

using ::absl::meta_internal::HasConstexprEvaluation;
using ::testing::Contains;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

static_assert(!absl::type_traits_internal::IsOwner<SetView<int>>::value &&
                  absl::type_traits_internal::IsView<SetView<int>>::value,
              "SetView is a view, not an owner");

// A pointer based iterator that allows you to add a member. This is used to
// change the size of the iterator, or make it non-trivial.
template <typename V, typename Member>
class IteratorWithMember {
 public:
  using value_type = V;
  using pointer = const V*;
  using reference = const V&;
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  IteratorWithMember(const V* it)  // NOLINT(runtime/explicit)
      : it_(it) {}

  const V& operator*() const { return *it_; }

  const V* operator->() const { return it_; }

  IteratorWithMember& operator++() {
    ++it_;
    return *this;
  }

  friend bool operator==(const IteratorWithMember& a,
                         const IteratorWithMember& b) {
    return a.it_ == b.it_;
  }

  friend bool operator!=(const IteratorWithMember& a,
                         const IteratorWithMember& b) {
    return a.it_ != b.it_;
  }

 private:
  const V* it_;
  Member m_;
};

// sizeof(BigIterator) should be large enough to prevent SetView::iterator from
// using inline storage.
template <typename V>
using BigIterator = IteratorWithMember<V, std::array<void*, 30>>;

struct NonTrivialObject {
  std::vector<std::string> s = {"just", "some", "random", "strings"};
};

// NonTrivialIterator requires SetView to correctly copy and destroy a
// non-trivial object.
template <typename V>
using NonTrivialIterator = IteratorWithMember<V, NonTrivialObject>;

// A simple, partial implementation of a set that works with the above
// iterators.
template <typename K, typename It = const K*>
class SlowFlatSet {
 public:
  using key_type = K;
  using value_type = K;
  using const_iterator = It;
  using size_type = std::size_t;

  const_iterator begin() const { return container_.data(); }
  const_iterator end() const { return container_.data() + container_.size(); }

  const_iterator find(const key_type& k) const {
    auto it = begin();
    while (it != end() && *it != k) ++it;
    return it;
  }

  template <typename... Args>
  const_iterator emplace(Args&&... args) {
    return &container_.emplace_back(std::forward<Args>(args)...);
  }

  size_type size() const { return container_.size(); }

 private:
  using Container = std::vector<value_type>;
  Container container_;
};

// Returns a deterministic key of type K.
template <typename K>
constexpr K MakeKey(int i) {
  return i;
}

template <>
std::string MakeKey<std::string>(int i) {
  return absl::StrCat("Key: ", i);
}

// Returns a deterministic set of type M and the given size.
template <typename Set>
Set MakeSet(int size) {
  Set result;
  for (int i = 0; i < size; ++i) {
    result.emplace(MakeKey<typename Set::key_type>(i));
  }
  return result;
}

template <typename C>
struct FilledContainerFactory {
  C make() { return MakeSet<C>(10); }
};

template <typename K>
struct FilledContainerFactory<std::initializer_list<K>> {
  std::initializer_list<K> make() {
    static constexpr std::initializer_list<K> kInit = {
        MakeKey<K>(0), MakeKey<K>(1), MakeKey<K>(2), MakeKey<K>(3),
        MakeKey<K>(4), MakeKey<K>(5), MakeKey<K>(6), MakeKey<K>(7),
        MakeKey<K>(8), MakeKey<K>(9),
    };
    return kInit;
  }
};

template <typename Set>
SetView<typename Set::key_type> MakeSetView(const Set& m) {
  return m;
}

template <typename K>
SetView<K> MakeSetView(const std::initializer_list<K>& i) {
  return i;
}

template <typename C>
using ViewType = decltype(MakeSetView(std::declval<C>()));

template <typename M>
class SetViewTest : public ::testing::Test {};

using SetTypes = ::testing::Types<std::initializer_list<int>,                //
                                  std::set<int>,                             //
                                  absl::flat_hash_set<int>,                  //
                                  std::unordered_set<int>,                   //
                                  std::set<std::string>,                     //
                                  absl::flat_hash_set<int>,                  //
                                  std::unordered_set<std::string>,           //
                                  SlowFlatSet<int>,                          //
                                  SlowFlatSet<std::string>,                  //
                                  SlowFlatSet<int, BigIterator<int>>,        //
                                  SlowFlatSet<int, NonTrivialIterator<int>>  //
                                  >;

TEST(SetViewTest, InitializerList) {
  [](const SetView<int> sv) {
    EXPECT_EQ(2, sv.size());
    EXPECT_EQ(1, *sv.find(1));
    EXPECT_EQ(2, *sv.find(2));
    EXPECT_TRUE(sv.contains(2));
    EXPECT_THAT(sv, UnorderedElementsAre(1, 2));
  }({1, 2});
}

void Overloaded(const SetView<int>) {}
void Overloaded(const SetView<std::string>) {}

TYPED_TEST_SUITE(SetViewTest, SetTypes);

TYPED_TEST(SetViewTest, HandlesOverloadSets) {
  TypeParam m = {};
  using SV = ViewType<TypeParam>;
  EXPECT_TRUE((std::is_convertible<const TypeParam&, SV>::value));
  EXPECT_FALSE((std::is_convertible<const TypeParam&, SetView<double>>::value));
  Overloaded(m);  // compiles :)
}

TYPED_TEST(SetViewTest, DefaultConstructed) {
  using SV = ViewType<TypeParam>;
  SV sv;
  EXPECT_TRUE(sv.empty());
  EXPECT_EQ(0, sv.size());
  EXPECT_TRUE(sv.begin() == sv.end());
}

TYPED_TEST(SetViewTest, Empty) {
  TypeParam m = {};
  auto sv = MakeSetView(m);
  EXPECT_TRUE(sv.empty());
  EXPECT_EQ(0, sv.size());
  EXPECT_TRUE(sv.begin() == sv.end());
}

TYPED_TEST(SetViewTest, FindSomething) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  EXPECT_EQ(*m.begin(), *sv.find(*m.begin()));
  EXPECT_TRUE(sv.contains(*m.begin()));
}

TYPED_TEST(SetViewTest, FindNothing) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  auto key = MakeKey<typename ViewType<TypeParam>::key_type>(-1);
  EXPECT_TRUE(sv.find(key) == sv.end());
  EXPECT_FALSE(sv.contains(key));
}

TYPED_TEST(SetViewTest, ForLoop) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  int i = 0;
  for (const auto& p : sv) {
    EXPECT_EQ(&p, &*absl::c_find(m, p));
    ++i;
  }
  EXPECT_EQ(m.size(), i);
}

TYPED_TEST(SetViewTest, Iterator) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  for (const auto& k : sv) {
    ASSERT_TRUE(sv.contains(k));
    auto it = sv.find(k);
    EXPECT_EQ(&*it, &*absl::c_find(m, *it));
  }
}

TYPED_TEST(SetViewTest, IteratorPreIncrement) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  for (auto it = sv.begin(); it != sv.end();) {
    auto x = ++it;
    EXPECT_EQ(x, it);
  }
}

TYPED_TEST(SetViewTest, IteratorPostIncrement) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  for (auto it = sv.begin(); it != sv.end();) {
    auto x = it++;
    EXPECT_NE(x, it);
    ++x;
    EXPECT_EQ(x, it);
  }
}

TYPED_TEST(SetViewTest, IteratorCopy) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto sv = MakeSetView(m);
  for (auto it = sv.begin(); it != sv.end();) {
    auto it_copy = it;
    EXPECT_TRUE(it_copy == it);
    EXPECT_TRUE(&*it_copy == &*it);
    it = it_copy;
    EXPECT_TRUE(it_copy == it);
    EXPECT_TRUE(&*it_copy == &*it);
    ++it;
    ++it_copy;
    EXPECT_TRUE(it_copy == it);
    if (it != sv.end()) {
      EXPECT_TRUE(&*it_copy == &*it);
    }
  }
}

TEST(SetViewTest, KeyViewConstructor) {
  using Map = std::map<std::string, std::string>;
  EXPECT_TRUE(
      (std::is_convertible_v<gtl::key_view_t<Map>, SetView<std::string>>));
  EXPECT_TRUE((
      std::is_convertible_v<gtl::key_view_t<const Map>, SetView<std::string>>));
}

TEST(SetViewTest, KeyViewIterator) {
  std::map<std::string, std::string> m = {{"a", "1"}, {"b", "2"}, {"c", "3"}};
  auto kv = gtl::key_view(m);
  gtl::SetView<std::string> v(kv);
  EXPECT_THAT(v, UnorderedElementsAre("a", "b", "c"));
}

TEST(SetViewTest, KeyViewLookup) {
  std::map<std::string, std::string> m = {{"a", "1"}, {"b", "2"}, {"c", "3"}};
  auto kv = gtl::key_view(m);
  gtl::SetView<std::string> v(kv);
  // linear search
  EXPECT_THAT(v, Contains("a"));
  EXPECT_THAT(v, Not(Contains("d")));
  // std::map lookup
  EXPECT_TRUE(v.contains("a"));
  EXPECT_EQ(v.find("d"), v.end());
}

// Extra "BaseTest" layer is needed to support std::initializer_list (it cannot
// be stored as a member variable).
template <typename Set, typename KeyType = typename Set::key_type>
class SetViewStringLookupBaseTest : public ::testing::Test {
  using View = SetView<KeyType>;

 protected:
  template <typename LookupKey>
  void ExpectFind(View view, const LookupKey& k) {
    EXPECT_NE(view.find(k), view.end());
  }
  template <typename LookupKey>
  void ExpectContains(View view, const LookupKey& k) {
    EXPECT_TRUE(view.contains(k));
  }
};

template <typename Set>
class SetViewStringLookupTest : public SetViewStringLookupBaseTest<Set> {
 public:
  SetViewStringLookupTest() {
    set_.emplace("a");
    set_.emplace("b");
    set_.emplace("c");
  }

  template <typename LookupKey>
  void TestFind(const LookupKey& k) {
    this->ExpectFind(set_, k);
  }
  template <typename LookupKey>
  void TestContains(const LookupKey& k) {
    this->ExpectContains(set_, k);
  }

 protected:
  Set set_;
};

template <typename K>
class SetViewStringLookupTest<std::initializer_list<K>>
    : public SetViewStringLookupBaseTest<std::initializer_list<K>, K> {
 public:
  template <typename LookupKey>
  void TestFind(const LookupKey& k) {
    this->ExpectFind({K("a"), K("b"), K("c")}, k);
  }
  template <typename LookupKey>
  void TestContains(const LookupKey& k) {
    this->ExpectContains({K("a"), K("b"), K("c")}, k);
  }
};

using StringSets = testing::Types<
    // Supports heterogeneous lookup.
    absl::flat_hash_set<std::string>,        //
    absl::flat_hash_set<absl::string_view>,  //
    absl::flat_hash_set<absl::Cord>,         //
    // Does not support heterogeneous lookup.
    std::set<std::string>,        //
    std::set<absl::string_view>,  //
    std::set<absl::Cord>,         //
    // Supports heterogeneous lookup (string_view but not Cord).
    // initializer lists are special
    std::initializer_list<std::string>,        //
    std::initializer_list<absl::string_view>,  //
    std::initializer_list<absl::Cord>          //
    >;

TYPED_TEST_SUITE(SetViewStringLookupTest, StringSets);

TYPED_TEST(SetViewStringLookupTest, LiteralString) {
  this->TestFind("b");
  this->TestContains("b");
}

TYPED_TEST(SetViewStringLookupTest, CString) {
  char buff[256] = "b";
  const char* raw = buff;
  this->TestFind(raw);
  this->TestContains(raw);
}

TYPED_TEST(SetViewStringLookupTest, String) {
  this->TestFind(std::string("b"));
  this->TestContains(std::string("b"));
}

TYPED_TEST(SetViewStringLookupTest, StringView) {
  this->TestFind(absl::string_view("b"));
  this->TestContains(absl::string_view("b"));
}

TEST(ConstexprSetView, Constructors) {
  static constexpr std::initializer_list<int> kIList = {0, 1, 2};
  static constexpr auto kSet = gtl::fixed_flat_set_of<int>({0, 1, 2});
  EXPECT_TRUE(HasConstexprEvaluation([] { return gtl::SetView<int>(); }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return gtl::SetView<int>(kIList); }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return gtl::SetView<int>(kSet); }));
}

template <typename M, typename K>
ABSL_ATTRIBUTE_NOINLINE bool Contains(const M& m, const K& k) {
  return m.find(k) != m.end();
}

// Makes keys to use for lookup benchmarks of the given size. The result will be
// larger than the specified size in order to trigger a few misses.
template <typename K>
std::vector<K> MakeKeysForBenchmark(int64_t size) {
  int64_t num_misses = std::max<int64_t>(1, size * 0.1);
  std::vector<K> keys;
  keys.reserve(size + num_misses);
  for (int64_t i = 0; i < size + num_misses; ++i) {
    keys.push_back(MakeKey<K>(i));
  }
  absl::BitGen rng;
  std::shuffle(keys.begin(), keys.end(), rng);
  return keys;
}

template <typename Set>
std::tuple<Set, std::vector<typename Set::key_type>> InitSetForBenchmark(
    ::benchmark::State& state) {
  int64_t size = state.range(0);
  Set set = MakeSet<Set>(size);
  auto keys = MakeKeysForBenchmark<typename Set::key_type>(size);
  return {set, keys};
}

// Baseline. Finds all elements in a set, with some misses.
template <typename Set>
void BM_SetFind(::benchmark::State& state) {
  auto [m, keys] = InitSetForBenchmark<Set>(state);

  while (state.KeepRunningBatch(keys.size())) {
    for (const auto& key : keys) {
      benchmark::DoNotOptimize(&m);
      benchmark::DoNotOptimize(Contains(m, key));
    }
  }
}

// Same as BM_SetFind, but uses a SetView for find().
template <typename Set>
void BM_SetViewFind(::benchmark::State& state) {
  auto [m, keys] = InitSetForBenchmark<Set>(state);

  auto sv = MakeSetView(m);
  while (state.KeepRunningBatch(keys.size())) {
    for (const auto& key : keys) {
      benchmark::DoNotOptimize(&sv);
      benchmark::DoNotOptimize(Contains(sv, key));
    }
  }
}

template <typename Set, typename Keys>
void SetContainsBenchmark(const Set& set, const Keys& keys,
                          ::benchmark::State& state) {
  while (state.KeepRunningBatch(keys.size())) {
    for (const auto& key : keys) {
      benchmark::DoNotOptimize(&set);
      benchmark::DoNotOptimize(set.contains(key));
    }
  }
}

// Baseline. Finds all elements in a set, with some misses.
template <typename Set>
void BM_SetContains(::benchmark::State& state) {
  auto [set, keys] = InitSetForBenchmark<Set>(state);
  SetContainsBenchmark(set, keys, state);
}

// Same as BM_SetContains, but uses a SetView for contains().
template <typename Set>
void BM_SetViewContains(::benchmark::State& state) {
  auto [set, keys] = InitSetForBenchmark<Set>(state);
  SetContainsBenchmark(MakeSetView(set), keys, state);
}

// Baseline. Iterates over a set.
template <typename Set>
void BM_SetIter(::benchmark::State& state) {
  int64_t size = state.range(0);
  Set m = MakeSet<Set>(size);

  while (state.KeepRunningBatch(m.size())) {
    benchmark::DoNotOptimize(&m);
    for (const auto& v : m) {
      benchmark::DoNotOptimize(&v);
    }
  }
}

// Same as BM_SetFind, but uses a SetView to iterate.
template <typename Set>
void BM_SetViewIter(::benchmark::State& state) {
  int64_t size = state.range(0);
  Set m = MakeSet<Set>(size);

  auto sv = MakeSetView(m);
  while (state.KeepRunningBatch(sv.size())) {
    benchmark::DoNotOptimize(&sv);
    for (const auto& v : sv) {
      benchmark::DoNotOptimize(&v);
    }
  }
}

static constexpr int kRangeStart = 1 << 7;
static constexpr int kRangeEnd = 1 << 13;

#define SET_VIEW_BENCHMARKS_FOR_TYPE(...)                                     \
  BENCHMARK_TEMPLATE(BM_SetFind, __VA_ARGS__)->Range(kRangeStart, kRangeEnd); \
  BENCHMARK_TEMPLATE(BM_SetViewFind, __VA_ARGS__)                             \
      ->Range(kRangeStart, kRangeEnd);                                        \
  BENCHMARK_TEMPLATE(BM_SetIter, __VA_ARGS__)->Range(kRangeStart, kRangeEnd); \
  BENCHMARK_TEMPLATE(BM_SetViewIter, __VA_ARGS__)                             \
      ->Range(kRangeStart, kRangeEnd);                                        \
  BENCHMARK_TEMPLATE(BM_SetViewContains, __VA_ARGS__)                         \
      ->Range(kRangeStart, kRangeEnd);

// Note: std containers do not provide contains method until C++20.
BENCHMARK_TEMPLATE(BM_SetContains, absl::flat_hash_set<std::string>)
    ->Range(kRangeStart, kRangeEnd);
BENCHMARK_TEMPLATE(BM_SetContains, absl::flat_hash_set<std::uint64_t>)
    ->Range(kRangeStart, kRangeEnd);

SET_VIEW_BENCHMARKS_FOR_TYPE(std::set<std::string>);
SET_VIEW_BENCHMARKS_FOR_TYPE(std::unordered_set<std::string>);
SET_VIEW_BENCHMARKS_FOR_TYPE(absl::flat_hash_set<std::string>);
SET_VIEW_BENCHMARKS_FOR_TYPE(std::set<uint64_t>);
SET_VIEW_BENCHMARKS_FOR_TYPE(std::unordered_set<uint64_t>);
SET_VIEW_BENCHMARKS_FOR_TYPE(absl::flat_hash_set<uint64_t>);

#undef SET_VIEW_BENCHMARKS_FOR_TYPE

}  // namespace
}  // namespace gtl
