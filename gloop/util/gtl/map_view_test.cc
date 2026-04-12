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

#include "gloop/util/gtl/map_view.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/exception_testing.h"
#include "absl/container/flat_hash_map.h"
#include "absl/meta/internal/constexpr_testing.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/flat_map.h"
#include "gloop/util/gtl/set_view.h"
#include "google/protobuf/map.h"

namespace gtl {
namespace {

using ::absl::meta_internal::HasConstexprEvaluation;
using ::testing::Eq;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

static_assert(!absl::type_traits_internal::IsOwner<MapView<int, int>>::value &&
                  absl::type_traits_internal::IsView<MapView<int, int>>::value,
              "MapView is a view, not an owner");

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

  IteratorWithMember(const V* it)  // NOLINT(google-explicit-constructor)
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

// sizeof(BigIterator) should be large enough to prevent MapView::iterator from
// using inline storage.
template <typename V>
using BigIterator = IteratorWithMember<V, std::array<void*, 30>>;

struct NonTrivialObject {
  std::vector<std::string> s = {"just", "some", "random", "strings"};
};

// NonTrivialIterator requires MapView to correctly copy and destroy a
// non-trivial object.
template <typename V>
using NonTrivialIterator = IteratorWithMember<V, NonTrivialObject>;

// A simple, partial implementation of a map that works with the above
// iterators.
template <typename K, typename V, typename It = const std::pair<const K, V>*>
class SlowFlatMap {
 public:
  using key_type = K;
  using mapped_type = V;
  using value_type = std::pair<const K, V>;
  using const_iterator = It;
  using size_type = std::size_t;

  const_iterator begin() const { return container_.data(); }
  const_iterator end() const { return container_.data() + container_.size(); }

  const_iterator find(const key_type& k) const {
    auto it = begin();
    while (it != end() && it->first != k) ++it;
    return it;
  }

  template <typename... Args>
  const_iterator emplace(Args&&... args) {
    return &container_.emplace_back(std::forward<Args>(args)...);
  }

  size_type size() const { return container_.size(); }

  const mapped_type& at(const key_type& k) const { return find(k)->second; }

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

// Returns a deterministic value of type V.
template <typename V>
constexpr V MakeValue(int i) {
  return i + 20;
}

template <>
std::string MakeValue<std::string>(int i) {
  return absl::StrCat("Value: ", i);
}

// Returns a deterministic map of type M and the given size.
template <typename Map>
Map MakeMap(int size) {
  Map result;
  for (int i = 0; i < size; ++i) {
    result.emplace(MakeKey<typename Map::key_type>(i),
                   MakeValue<typename Map::mapped_type>(i));
  }
  return result;
}

template <typename C>
struct FilledContainerFactory {
  C make() { return MakeMap<C>(10); }
};

template <typename K, typename V>
struct FilledContainerFactory<std::initializer_list<std::pair<const K, V>>> {
  std::initializer_list<std::pair<const K, V>> make() {
    static constexpr std::initializer_list<std::pair<const K, V>> kInit = {
        {MakeKey<K>(0), MakeValue<V>(0)}, {MakeKey<K>(1), MakeValue<V>(1)},
        {MakeKey<K>(2), MakeValue<V>(2)}, {MakeKey<K>(3), MakeValue<V>(3)},
        {MakeKey<K>(4), MakeValue<V>(4)}, {MakeKey<K>(5), MakeValue<V>(5)},
        {MakeKey<K>(6), MakeValue<V>(6)}, {MakeKey<K>(7), MakeValue<V>(7)},
        {MakeKey<K>(8), MakeValue<V>(8)}, {MakeKey<K>(9), MakeValue<V>(9)},
    };
    return kInit;
  }
};

template <typename Map>
MapView<typename Map::key_type, typename Map::mapped_type> MakeMapView(
    const Map& m) {
  return m;
}

template <typename K, typename V>
MapView<K, V> MakeMapView(
    const std::initializer_list<std::pair<const K, V>>& m) {
  return m;
}

template <typename C>
using ViewType = decltype(MakeMapView(std::declval<C>()));

template <typename M>
class MapViewTest : public ::testing::Test {};

using MapTypes = ::testing::Types<
    std::initializer_list<std::pair<const int, int>>,                     //
    std::map<int, int>,                                                   //
    absl::flat_hash_map<int, int>,                                        //
    std::unordered_map<int, int>,                                         //
    google::protobuf::Map<int, int>,                                      //
    gtl::flat_map<int, int>,                                              //
    std::map<std::string, int>,                                           //
    absl::flat_hash_map<int, std::string>,                                //
    std::unordered_map<std::string, int>,                                 //
    google::protobuf::Map<std::string, int>,                              //
    gtl::flat_map<std::string, int>,                                      //
    SlowFlatMap<int, int>,                                                //
    SlowFlatMap<std::string, int>,                                        //
    SlowFlatMap<int, int, BigIterator<std::pair<const int, int>>>,        //
    SlowFlatMap<int, int, NonTrivialIterator<std::pair<const int, int>>>  //
    >;

TEST(MapViewTest, InitializerList) {
  [](const MapView<int, int> mv) {
    EXPECT_EQ(2, mv.size());
    EXPECT_EQ(2, mv.find(1)->second);
    EXPECT_EQ(3, mv.find(2)->second);
    EXPECT_TRUE(mv.contains(2));
    EXPECT_THAT(mv, UnorderedElementsAre(Pair(1, 2), Pair(2, 3)));
  }({{1, 2}, {2, 3}});

  // Repeated keys should fail.
  GLOOP_EXPECT_DEBUG_DEATH((MapView<int, int>({{1, 1}, {1, 1}})),
                           "HasUniqueKeys");
}

void Overloaded(const MapView<int, int>) {}
void Overloaded(const MapView<int, std::string>) {}
void Overloaded(const MapView<std::string, int>) {}
ABSL_ATTRIBUTE_UNUSED void Overloaded(const SetView<int>) {}

TYPED_TEST_SUITE(MapViewTest, MapTypes);

TYPED_TEST(MapViewTest, HandlesOverloadSets) {
  TypeParam m = {};
  using MV = ViewType<TypeParam>;
  EXPECT_TRUE((std::is_convertible<const TypeParam&, MV>::value));
  EXPECT_FALSE(
      (std::is_convertible<const TypeParam&, MapView<double, double>>::value));
  Overloaded(m);  // compiles :)
}

TYPED_TEST(MapViewTest, DefaultConstructed) {
  using MV = ViewType<TypeParam>;
  MV mv;
  EXPECT_TRUE(mv.empty());
  EXPECT_EQ(0, mv.size());
  EXPECT_TRUE(mv.begin() == mv.end());
}

TYPED_TEST(MapViewTest, Empty) {
  TypeParam m = {};
  auto mv = MakeMapView(m);
  EXPECT_TRUE(mv.empty());
  EXPECT_EQ(0, mv.size());
  EXPECT_TRUE(mv.begin() == mv.end());
}

TYPED_TEST(MapViewTest, FindSomething) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  EXPECT_EQ(m.begin()->second, mv.find(m.begin()->first)->second);
  EXPECT_TRUE(mv.contains(m.begin()->first));
}

TYPED_TEST(MapViewTest, FindNothing) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  auto key = MakeKey<typename ViewType<TypeParam>::key_type>(-1);
  EXPECT_TRUE(mv.find(key) == mv.end());
  EXPECT_FALSE(mv.contains(key));
}

TYPED_TEST(MapViewTest, ForLoop) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  int i = 0;
  for (const auto& p : mv) {
    EXPECT_EQ(&p.second, &absl::c_find_if(m, [&](const auto& x) {
                            return x.first == p.first;
                          })->second);
    ++i;
  }
  EXPECT_EQ(m.size(), i);
}

TYPED_TEST(MapViewTest, Iterator) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  for (const auto& [k, v] : mv) {
    ASSERT_TRUE(mv.contains(k));
    EXPECT_EQ(mv.at(k), v);
    auto it = mv.find(k);
    EXPECT_EQ(&it->second, &absl::c_find_if(m, [&](const auto& x) {
                              return x.first == it->first;
                            })->second);
  }
}

TYPED_TEST(MapViewTest, IteratorPreIncrement) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  for (auto it = mv.begin(); it != mv.end();) {
    auto x = ++it;
    EXPECT_EQ(x, it);
  }
}

TYPED_TEST(MapViewTest, IteratorPostIncrement) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  for (auto it = mv.begin(); it != mv.end();) {
    auto x = it++;
    EXPECT_NE(x, it);
    ++x;
    EXPECT_EQ(x, it);
  }
}

TYPED_TEST(MapViewTest, IteratorCopy) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  for (auto it = mv.begin(); it != mv.end();) {
    auto it_copy = it;
    EXPECT_TRUE(it_copy == it);
    EXPECT_TRUE(&*it_copy == &*it);
    it = it_copy;
    EXPECT_TRUE(it_copy == it);
    EXPECT_TRUE(&*it_copy == &*it);
    ++it;
    ++it_copy;
    EXPECT_TRUE(it_copy == it);
    if (it != mv.end()) {
      EXPECT_TRUE(&*it_copy == &*it);
    }
  }
}

TYPED_TEST(MapViewTest, At) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  for (const auto& p : m) {
    EXPECT_EQ(&p.second, &mv.at(p.first));
  }
}

TYPED_TEST(MapViewTest, AtThrows) {
  auto m = FilledContainerFactory<TypeParam>().make();
  auto mv = MakeMapView(m);
  auto key = MakeKey<typename ViewType<TypeParam>::key_type>(-1);
  ABSL_BASE_INTERNAL_EXPECT_FAIL(mv.at(key), std::out_of_range,
                                 "failed bounds check");
}

// Extra "BaseTest" layer is needed to support std::initializer_list (it cannot
// be stored as a member variable).
template <typename Map, typename KeyType = typename Map::key_type,
          typename MappedType = typename Map::mapped_type>
class MapViewStringLookupBaseTest : public ::testing::Test {
  using View = MapView<KeyType, MappedType>;

 protected:
  template <typename LookupKey>
  void ExpectFind(View view, const LookupKey& k) {
    EXPECT_NE(view.find(k), view.end());
  }
  template <typename LookupKey>
  void ExpectContains(View view, const LookupKey& k) {
    EXPECT_TRUE(view.contains(k));
  }
  template <typename LookupKey, typename ValueMatcher>
  void ExpectAt(View view, const LookupKey& k, ValueMatcher&& matcher) {
    ASSERT_TRUE(view.contains(k));
    EXPECT_THAT(view.at(k), std::forward<ValueMatcher>(matcher));
  }
  template <typename LookupKey>
  void ExpectAtThrows(View view, const LookupKey& k) {
    ABSL_BASE_INTERNAL_EXPECT_FAIL(view.at(k), std::out_of_range,
                                   "failed bounds check");
  }
};

template <typename Map>
class MapViewStringLookupTest : public MapViewStringLookupBaseTest<Map> {
 public:
  MapViewStringLookupTest() {
    map_.emplace("a", 1);
    map_.emplace("b", 2);
    map_.emplace("c", 3);
  }

  template <typename LookupKey>
  void TestFind(const LookupKey& k) {
    this->ExpectFind(map_, k);
  }
  template <typename LookupKey>
  void TestContains(const LookupKey& k) {
    this->ExpectContains(map_, k);
  }
  template <typename LookupKey, typename ValueMatcher>
  void TestAt(const LookupKey& k, ValueMatcher&& matcher) {
    this->ExpectAt(map_, k, std::forward<ValueMatcher>(matcher));
  }
  template <typename LookupKey>
  void TestAtThrows(const LookupKey& k) {
    this->ExpectAtThrows(map_, k);
  }

 protected:
  Map map_;
};

template <typename K, typename V>
class MapViewStringLookupTest<std::initializer_list<std::pair<const K, V>>>
    : public MapViewStringLookupBaseTest<
          std::initializer_list<std::pair<const K, V>>, K, V> {
 public:
  template <typename LookupKey>
  void TestFind(const LookupKey& k) {
    this->ExpectFind({{K("a"), 1}, {K("b"), 2}, {K("c"), 3}}, k);
  }
  template <typename LookupKey>
  void TestContains(const LookupKey& k) {
    this->ExpectContains({{K("a"), 1}, {K("b"), 2}, {K("c"), 3}}, k);
  }
  template <typename LookupKey, typename ValueMatcher>
  void TestAt(const LookupKey& k, ValueMatcher&& matcher) {
    this->ExpectAt({{K("a"), 1}, {K("b"), 2}, {K("c"), 3}}, k,
                   std::forward<ValueMatcher>(matcher));
  }
  template <typename LookupKey>
  void TestAtThrows(const LookupKey& k) {
    this->ExpectAtThrows({{K("a"), 1}, {K("b"), 2}, {K("c"), 3}}, k);
  }
};

using StringMaps = testing::Types<
    // Supports heterogeneous lookup.
    absl::flat_hash_map<std::string, int>,        //
    absl::flat_hash_map<absl::string_view, int>,  //
    absl::flat_hash_map<absl::Cord, int>,         //
    // Does not support heterogeneous lookup.
    std::map<std::string, int>,        //
    std::map<absl::string_view, int>,  //
    std::map<absl::Cord, int>,         //
    // Supports heterogeneous lookup (string_view but not Cord).
    // initializer lists are special
    std::initializer_list<std::pair<const std::string, int>>,        //
    std::initializer_list<std::pair<const absl::string_view, int>>,  //
    std::initializer_list<std::pair<const absl::Cord, int>>          //
    >;

TYPED_TEST_SUITE(MapViewStringLookupTest, StringMaps);

TYPED_TEST(MapViewStringLookupTest, LiteralString) {
  this->TestFind("b");
  this->TestContains("b");
  this->TestAt("b", Eq(2));
  this->TestAtThrows("w");
}

TYPED_TEST(MapViewStringLookupTest, CString) {
  char buff[256] = "b";
  const char* raw = buff;
  this->TestFind(raw);
  this->TestContains(raw);
  this->TestAt(raw, Eq(2));
  buff[0] = 'w';
  this->TestAtThrows(raw);
}

TYPED_TEST(MapViewStringLookupTest, String) {
  this->TestFind(std::string("b"));
  this->TestContains(std::string("b"));
  this->TestAt(std::string("b"), Eq(2));
  this->TestAtThrows(std::string("w"));
}

TYPED_TEST(MapViewStringLookupTest, StringView) {
  this->TestFind(absl::string_view("b"));
  this->TestContains(absl::string_view("b"));
  this->TestAt(absl::string_view("b"), Eq(2));
  this->TestAtThrows(absl::string_view("w"));
}

TEST(ConstexprMapView, Constructors) {
  using View = gtl::MapView<int, absl::string_view>;

  static constexpr std::initializer_list<
      std::pair<const int, absl::string_view>>
      kInitList = {{0, "zero"}, {1, "one"}, {2, "two"}};
  static constexpr auto kMap = gtl::fixed_flat_map_of<int, absl::string_view>(
      {{0, "zero"}, {1, "one"}, {2, "two"}});

  EXPECT_TRUE(HasConstexprEvaluation([] { return View(); }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return View(kInitList); }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return View(kMap); }));
}

TEST(MapView, InitListWithDuplicatesRejected) {
  using View = gtl::MapView<int, absl::string_view>;

  static constexpr std::initializer_list<
      std::pair<const int, absl::string_view>>
      kEmptyList = {};
  static constexpr std::initializer_list<
      std::pair<const int, absl::string_view>>
      kUniqueList = {{0, "zero"}, {1, "one"}, {2, "two"}};
  static constexpr std::initializer_list<
      std::pair<const int, absl::string_view>>
      kDuplicatesList = {{0, "zero"}, {0, "zero"}, {0, "zero"}};

  EXPECT_TRUE(HasConstexprEvaluation([] { return View(kEmptyList); }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return View(kUniqueList); }));

  // MapView cannot be created from an initializer list with duplicate keys
  // which is enforced with an `assert` statement.
  // Any violation will result in
  //   (a) inability to be used in a constexpr
  //   (b) crash in the debug mode

#ifndef NDEBUG
  // `assert()` is disabled in some builds such as "-c opt".
  EXPECT_FALSE(HasConstexprEvaluation([] { return View(kDuplicatesList); }));
#endif  // NDEBUG

  GLOOP_EXPECT_DEBUG_DEATH(static_cast<void>(View(kDuplicatesList)),
                           "HasUniqueKeys");
}

TEST(MapView, InitListWithWrongType) {
  EXPECT_TRUE(
      (std::is_constructible_v<
          gtl::MapView<int, absl::string_view>,
          std::initializer_list<std::pair<const int, absl::string_view>>>));

  // Missing const on key type.
  EXPECT_FALSE((std::is_constructible_v<
                gtl::MapView<int, absl::string_view>,
                std::initializer_list<std::pair<int, absl::string_view>>>));

  // Wrong key type.
  EXPECT_FALSE(
      (std::is_constructible_v<
          gtl::MapView<int, absl::string_view>,
          std::initializer_list<std::pair<const float, absl::string_view>>>));

  // Wrong mapped type.
  EXPECT_FALSE((std::is_constructible_v<
                gtl::MapView<int, absl::string_view>,
                std::initializer_list<std::pair<const int, std::string>>>));
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

// Baseline. Finds all elements in a map, with some misses.
template <typename Map>
void BM_MapFind(::benchmark::State& state) {
  int64_t size = state.range(0);
  Map m = MakeMap<Map>(size);
  auto keys = MakeKeysForBenchmark<typename Map::key_type>(size);

  while (state.KeepRunningBatch(keys.size())) {
    for (const auto& key : keys) {
      benchmark::DoNotOptimize(&m);
      benchmark::DoNotOptimize(Contains(m, key));
    }
  }
}

// Same as BM_MapFind, but uses a MapView for find().
template <typename Map>
void BM_MapViewFind(::benchmark::State& state) {
  int64_t size = state.range(0);
  Map m = MakeMap<Map>(size);
  auto keys = MakeKeysForBenchmark<typename Map::key_type>(size);

  auto mv = MakeMapView(m);
  while (state.KeepRunningBatch(keys.size())) {
    for (const auto& key : keys) {
      benchmark::DoNotOptimize(&mv);
      benchmark::DoNotOptimize(Contains(mv, key));
    }
  }
}

// Baseline. Iterates over a map.
template <typename Map>
void BM_MapIter(::benchmark::State& state) {
  int64_t size = state.range(0);
  Map m = MakeMap<Map>(size);

  while (state.KeepRunningBatch(m.size())) {
    benchmark::DoNotOptimize(&m);
    for (const auto& v : m) {
      benchmark::DoNotOptimize(&v);
    }
  }
}

// Same as BM_MapFind, but uses a MapView to iterate.
template <typename Map>
void BM_MapViewIter(::benchmark::State& state) {
  int64_t size = state.range(0);
  Map m = MakeMap<Map>(size);

  auto mv = MakeMapView(m);
  while (state.KeepRunningBatch(mv.size())) {
    benchmark::DoNotOptimize(&mv);
    for (const auto& v : mv) {
      benchmark::DoNotOptimize(&v);
    }
  }
}

static constexpr int kRangeStart = 1 << 7;
static constexpr int kRangeEnd = 1 << 13;

#define MAP_VIEW_BENCHMARKS_FOR_TYPE(...)                                     \
  BENCHMARK_TEMPLATE(BM_MapFind, __VA_ARGS__)->Range(kRangeStart, kRangeEnd); \
  BENCHMARK_TEMPLATE(BM_MapViewFind, __VA_ARGS__)                             \
      ->Range(kRangeStart, kRangeEnd);                                        \
  BENCHMARK_TEMPLATE(BM_MapIter, __VA_ARGS__)->Range(kRangeStart, kRangeEnd); \
  BENCHMARK_TEMPLATE(BM_MapViewIter, __VA_ARGS__)                             \
      ->Range(kRangeStart, kRangeEnd);

MAP_VIEW_BENCHMARKS_FOR_TYPE(std::map<std::string, std::string>);
MAP_VIEW_BENCHMARKS_FOR_TYPE(std::unordered_map<std::string, std::string>);
MAP_VIEW_BENCHMARKS_FOR_TYPE(absl::flat_hash_map<std::string, std::string>);
MAP_VIEW_BENCHMARKS_FOR_TYPE(std::map<uint64_t, uint64_t>);
MAP_VIEW_BENCHMARKS_FOR_TYPE(std::unordered_map<uint64_t, uint64_t>);
MAP_VIEW_BENCHMARKS_FOR_TYPE(absl::flat_hash_map<uint64_t, uint64_t>);

#undef MAP_VIEW_BENCHMARKS_FOR_TYPE

}  // namespace
}  // namespace gtl
