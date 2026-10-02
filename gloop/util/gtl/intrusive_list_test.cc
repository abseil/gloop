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

#include "gloop/util/gtl/intrusive_list.h"

#include <algorithm>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Pointwise;
using ::testing::Ref;
using ::testing::SizeIs;

MATCHER(AddressEqualsPointer, "") {
  return &std::get<0>(arg) == std::get<1>(arg);
}

struct ListId2 {};

struct TestItem : public intrusive_link<TestItem>,
                  public intrusive_link<TestItem, ListId2> {
  TestItem() : n(0) {}
  explicit TestItem(int n) : n(n) {}
  int n;
};
using TestList = intrusive_list<TestItem>;
using CanonicalList = std::list<TestItem*>;

struct ABSL_CACHELINE_ALIGNED CacheAlignedTestItem
    : public intrusive_link<CacheAlignedTestItem> {
  explicit CacheAlignedTestItem(int n) : n(n) {}
  int n;
};

// In b/232466232, we discovered that splice contained invalid casts from
// intrusive_list<T>::link_type to T performed on
// intrusive_list<T>::sentinel_link_ (which as the name implies is just a link).
// Without the fix in the same CL as this test, the test would fail under ubsan
// or -fsanitize=alignment.
TEST(CacheAlignedIntrusiveListTest, DoesNotCauseUndefinedBehavior) {
  CacheAlignedTestItem items[2]{CacheAlignedTestItem(1),
                                CacheAlignedTestItem(2)};
  intrusive_list<CacheAlignedTestItem> list1;
  intrusive_list<CacheAlignedTestItem> list2;
  list1.push_back(&items[0]);
  list2.push_back(&items[1]);
  list1.splice(list1.end(), list2);
  EXPECT_THAT(list1, ElementsAre(Field(&CacheAlignedTestItem::n, 1),
                                 Field(&CacheAlignedTestItem::n, 2)));
  EXPECT_THAT(list2, IsEmpty());
}

void swap(TestItem& a, TestItem& b) {
  using std::swap;
  swap(a.n, b.n);
}

void FillLists(TestList* list_a, CanonicalList* list_b, TestItem* elems,
               int num_elems) {
  list_a->clear();
  list_b->clear();
  for (int i = 0; i < num_elems; ++i) {
    list_a->push_back(elems + i);
    list_b->push_back(elems + i);
  }
}

TEST(NewIntrusiveListTest, Basic) {
  TestList list1;

  EXPECT_EQ(sizeof(intrusive_link<TestItem>), sizeof(void*) * 2);

  constexpr int kCount = 10;
  for (int i = 0; i < kCount; ++i) {
    list1.push_front(new TestItem(i));
  }
  EXPECT_THAT(list1, SizeIs(kCount));

  // Verify we can reverse a list because we defined swap for TestItem.
  std::reverse(list1.begin(), list1.end());
  EXPECT_THAT(list1, SizeIs(kCount));

  // Check both const and non-const forward iteration.
  const TestList& clist1 = list1;
  int i = 0;
  auto iter = list1.begin();
  for (; iter != list1.end(); ++iter, ++i) {
    EXPECT_EQ(iter->n, i);
  }
  EXPECT_EQ(iter, clist1.end());
  EXPECT_NE(iter, clist1.begin());

  i = 0;
  auto citer = clist1.begin();
  for (; citer != clist1.end(); ++citer, ++i) {
    EXPECT_EQ(citer->n, i);
  }
  EXPECT_EQ(citer, clist1.end());
  EXPECT_NE(citer, clist1.begin());

  EXPECT_EQ(list1.front().n, 0);
  EXPECT_EQ(list1.back().n, 9);

  // Verify we can swap 2 lists.
  TestList list2;
  list2.swap(list1);
  EXPECT_THAT(list1, IsEmpty());
  EXPECT_THAT(list2, SizeIs(kCount));

  // Check both const and non-const reverse iteration.
  const TestList& clist2 = list2;
  auto riter = list2.rbegin();
  i = 9;
  for (; riter != list2.rend(); ++riter, --i) {
    EXPECT_EQ(riter->n, i);
  }
  EXPECT_EQ(riter, clist2.rend());
  EXPECT_NE(riter, clist2.rbegin());

  auto criter = clist2.rbegin();
  i = 9;
  for (; criter != clist2.rend(); ++criter, --i) {
    EXPECT_EQ(criter->n, i);
  }
  EXPECT_EQ(criter, clist2.rend());
  EXPECT_NE(criter, clist2.rbegin());

  while (!list2.empty()) {
    TestItem* e = &list2.front();
    list2.pop_front();
    delete e;
  }
}

TestList ReturnPassedList(TestList list) { return list; }

TEST(NewIntrusiveListTest, FromInitializerList) {
  TestItem e1, e2, e3;
  EXPECT_THAT(ReturnPassedList({&e1, &e2, &e3}),
              ElementsAre(Ref(e1), Ref(e2), Ref(e3)));
}

TEST(NewIntrusiveListTest, FromSpan) {
  TestItem e1, e2, e3;
  std::vector<TestItem*> v = {&e1, &e2, &e3};
  EXPECT_THAT(ReturnPassedList(TestList(v)),
              ElementsAre(Ref(e1), Ref(e2), Ref(e3)));
}

TEST(NewIntrusiveListTest, Erase) {
  TestList l;
  constexpr int kCount = 10;
  TestItem* e[kCount];

  // Create a list with 10 items.
  for (int i = 0; i < kCount; ++i) {
    e[i] = new TestItem(i);
    l.push_front(e[i]);
  }

  // Test that erase works.
  for (int i = 0; i < kCount; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(l.size(), kCount - i);

    auto iter = l.erase(e[i]);
    EXPECT_NE(iter, TestList::iterator(e[i]));

    EXPECT_EQ(l.size(), kCount - i - 1);
    delete e[i];
  }
}

TEST(NewIntrusiveListTest, Insert) {
  TestList l;
  auto iter = l.end();
  constexpr int kCount = 10;
  TestItem* e[kCount];

  // Create a list with 10 items.
  for (int i = kCount - 1; i >= 0; --i) {
    e[i] = new TestItem(i);
    iter = l.insert(iter, e[i]);
    EXPECT_EQ(&*iter, e[i]);
  }

  EXPECT_THAT(l, SizeIs(kCount));

  // Verify insertion order.
  iter = l.begin();
  for (TestItem* item : e) {
    EXPECT_EQ(&*iter, item);
    iter = l.erase(item);
    delete item;
  }
}

TEST(NewIntrusiveListTest, Move) {
  // Move constructible.

  {  // Move-construct from an empty list.
    TestList src;
    TestList dest(std::move(src));
    EXPECT_THAT(dest, IsEmpty());
  }

  {  // Move-construct from a single item list.
    TestItem e;
    TestList src;
    src.push_front(&e);

    TestList dest(std::move(src));
    EXPECT_THAT(src, IsEmpty());  // NOLINT bugprone-use-after-move
    EXPECT_THAT(dest, ElementsAre(Ref(e)));
  }

  {  // Move-construct from a list with multiple items.
    constexpr int kCount = 10;
    TestItem items[kCount];
    TestList src;
    for (TestItem& e : items) src.push_back(&e);

    TestList dest(std::move(src));
    EXPECT_THAT(src, IsEmpty());  // NOLINT bugprone-use-after-move
    // Verify the items on the destination list.
    ASSERT_THAT(dest, SizeIs(kCount));
    int i = 0;
    for (TestItem& e : dest) {
      EXPECT_EQ(&e, &items[i++]) << " for index " << i;
    }
  }

  {  // Move-construct, then call clear(). Subsequent use should not trigger
     // use-after-move lint errors.
    TestItem e;
    TestList src;
    src.push_front(&e);

    TestList dest(std::move(src));
    src.clear();
    EXPECT_THAT(src, IsEmpty());
  }
}

TEST(NewIntrusiveListTest, StaticInsertErase) {
  TestList l;
  TestItem e[2];
  auto i = l.begin();
  TestList::insert(i, &e[0]);
  TestList::insert(&e[0], &e[1]);
  TestList::erase(&e[0]);
  TestList::erase(TestList::iterator(&e[1]));
  EXPECT_THAT(l, IsEmpty());
}

TEST(NewIntrusiveListTest, PushPop) {
  auto item = [](int n) { return Field(&TestItem::n, n); };
  TestList l;

  TestItem item1(1);
  TestItem item2(2);
  TestItem item3(3);
  TestItem item4(4);

  // push front, pop front.
  l.push_front(&item1);
  EXPECT_THAT(l, ElementsAre(item(1)));
  l.pop_front();
  EXPECT_THAT(l, IsEmpty());

  // push front, pop back.
  l.push_front(&item1);
  EXPECT_THAT(l, ElementsAre(item(1)));
  l.pop_back();
  EXPECT_THAT(l, IsEmpty());

  // push back, pop front.
  l.push_back(&item1);
  EXPECT_THAT(l, ElementsAre(item(1)));
  l.pop_front();
  EXPECT_THAT(l, IsEmpty());

  // push back, pop back.
  l.push_back(&item1);
  EXPECT_THAT(l, ElementsAre(item(1)));
  l.pop_back();
  EXPECT_THAT(l, IsEmpty());

  // push and pop several times.
  l.push_back(&item3);
  l.push_front(&item2);
  l.push_back(&item4);
  l.push_front(&item1);
  EXPECT_THAT(l, ElementsAre(item(1), item(2), item(3), item(4)));
  l.pop_back();
  EXPECT_THAT(l, ElementsAre(item(1), item(2), item(3)));
  l.pop_back();
  EXPECT_THAT(l, ElementsAre(item(1), item(2)));
  l.pop_front();
  EXPECT_THAT(l, ElementsAre(item(2)));
  l.pop_front();
  EXPECT_THAT(l, IsEmpty());
}

TEST(IntrusiveListTest, Splice) {
  constexpr int kNumElements = 10;
  TestItem e[kNumElements];
  TestList l1, l2;
  CanonicalList ll1, ll2;

  // We verify that the contents of this secondary list aren't affected by any
  // of the splices.
  intrusive_list<TestItem, ListId2> secondary_list;
  for (int i = 0; i < 3; ++i) {
    secondary_list.push_back(&e[i]);
  }

  // Test the basic cases:
  // - The lists range from 0 to 2 elements.
  // - The insertion point ranges from begin() to end()
  // - The transferred range has multiple sizes and locations in the source.
  for (int l1_count = 0; l1_count < 3; ++l1_count) {
    for (int l2_count = 0; l2_count < 3; ++l2_count) {
      for (int pos = 0; pos <= l1_count; ++pos) {
        for (int first = 0; first <= l2_count; ++first) {
          for (int last = first; last <= l2_count; ++last) {
            SCOPED_TRACE(::testing::Message()
                         << "l1_count=" << l1_count << ", l2_count=" << l2_count
                         << ", pos=" << pos << ", first=" << first
                         << ", last=" << last);
            FillLists(&l1, &ll1, e, l1_count);
            EXPECT_THAT(l1, Pointwise(AddressEqualsPointer(), ll1));
            FillLists(&l2, &ll2, e + l1_count, l2_count);
            EXPECT_THAT(l2, Pointwise(AddressEqualsPointer(), ll2));

            l1.splice(std::next(l1.begin(), pos), l2,
                      std::next(l2.begin(), first),
                      std::next(l2.begin(), last));
            ll1.splice(std::next(ll1.begin(), pos), ll2,
                       std::next(ll2.begin(), first),
                       std::next(ll2.begin(), last));

            EXPECT_THAT(l1, Pointwise(AddressEqualsPointer(), ll1));
            EXPECT_THAT(l2, Pointwise(AddressEqualsPointer(), ll2));

            EXPECT_THAT(secondary_list,
                        ElementsAre(Ref(e[0]), Ref(e[1]), Ref(e[2])));
          }
        }
      }
    }
  }
}

TEST(IntrusiveListTest, SpliceRvalue) {
  TestItem e[3];
  intrusive_list<TestItem> primary_list;
  primary_list.push_back(&e[0]);
  intrusive_list<TestItem> secondary_list;
  secondary_list.push_back(&e[1]);
  secondary_list.push_back(&e[2]);

  primary_list.splice(primary_list.end(), std::move(secondary_list));
  EXPECT_THAT(primary_list, ElementsAre(Ref(e[0]), Ref(e[1]), Ref(e[2])));
}

TEST(NewIntrusiveListTest, Prefetch) {
  // Test that prefetching subsequent and previous elements executes cleanly
  // without faults.
  TestItem e[3];
  intrusive_list<TestItem> primary_list;
  intrusive_list<TestItem, ListId2> secondary_list;
  for (int i = 0; i < 3; ++i) {
    primary_list.push_front(&e[i]);
    secondary_list.push_back(&e[i]);
  }
  for (auto pos = primary_list.begin(); pos != primary_list.end(); ++pos) {
    GtlIntrusiveListPrefetchNext(pos);
    GtlIntrusiveListPrefetchPrev(pos);
  }
  for (auto pos = secondary_list.begin(); pos != secondary_list.end(); ++pos) {
    GtlIntrusiveListPrefetchNext(pos);
    GtlIntrusiveListPrefetchPrev(pos);
  }
}

// Build up a set of classes which form "challenging" type hierarchies to use
// with an intrusive_list.
struct BaseLinkId {};
struct DerivedLinkId {};

struct AbstractBase : public intrusive_link<AbstractBase, BaseLinkId> {
  virtual ~AbstractBase() = 0;
  virtual std::string name() { return "AbstractBase"; }
};
AbstractBase::~AbstractBase() = default;
struct DerivedClass : public intrusive_link<DerivedClass, DerivedLinkId>,
                      public AbstractBase {
  ~DerivedClass() override = default;
  std::string name() override { return "DerivedClass"; }
};
struct VirtuallyDerivedBaseClass : public virtual AbstractBase {
  ~VirtuallyDerivedBaseClass() override = 0;
  std::string name() override { return "VirtuallyDerivedBaseClass"; }
};
VirtuallyDerivedBaseClass::~VirtuallyDerivedBaseClass() = default;
struct VirtuallyDerivedClassA
    : public intrusive_link<VirtuallyDerivedClassA, DerivedLinkId>,
      public virtual VirtuallyDerivedBaseClass {
  ~VirtuallyDerivedClassA() override = default;
  std::string name() override { return "VirtuallyDerivedClassA"; }
};
struct NonceClass {
  virtual ~NonceClass() = default;
};
struct VirtuallyDerivedClassB
    : public intrusive_link<VirtuallyDerivedClassB, DerivedLinkId>,
      public virtual NonceClass,
      public virtual VirtuallyDerivedBaseClass {
  ~VirtuallyDerivedClassB() override = default;
  std::string name() override { return "VirtuallyDerivedClassB"; }
};
struct VirtuallyDerivedClassC
    : public intrusive_link<VirtuallyDerivedClassC, DerivedLinkId>,
      public virtual AbstractBase,
      public virtual NonceClass,
      public virtual VirtuallyDerivedBaseClass {
  ~VirtuallyDerivedClassC() override = default;
  std::string name() override { return "VirtuallyDerivedClassC"; }
};

// Test for multiple layers between the element type and the link.
namespace templated_base_link {
template <typename T>
struct AbstractBase : public intrusive_link<T> {
  virtual ~AbstractBase() = 0;
};
template <typename T>
AbstractBase<T>::~AbstractBase() = default;
struct DerivedClass : public AbstractBase<DerivedClass> {};
}  // namespace templated_base_link

TEST(NewIntrusiveListTest, HandleInheritanceHierarchies) {
  {
    intrusive_list<DerivedClass, DerivedLinkId> list;
    DerivedClass elements[2];
    EXPECT_THAT(list, IsEmpty());
    list.push_back(&elements[0]);
    EXPECT_EQ(list.size(), 1);
    list.push_back(&elements[1]);
    EXPECT_EQ(list.size(), 2);
    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    list.pop_back();
    EXPECT_THAT(list, IsEmpty());
  }
  {
    intrusive_list<VirtuallyDerivedClassA, DerivedLinkId> list;
    VirtuallyDerivedClassA elements[2];
    EXPECT_THAT(list, IsEmpty());
    list.push_back(&elements[0]);
    EXPECT_EQ(list.size(), 1);
    list.push_back(&elements[1]);
    EXPECT_EQ(list.size(), 2);
    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    list.pop_back();
    EXPECT_THAT(list, IsEmpty());
  }
  {
    intrusive_list<VirtuallyDerivedClassC, DerivedLinkId> list;
    VirtuallyDerivedClassC elements[2];
    EXPECT_THAT(list, IsEmpty());
    list.push_back(&elements[0]);
    EXPECT_EQ(list.size(), 1);
    list.push_back(&elements[1]);
    EXPECT_EQ(list.size(), 2);
    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    list.pop_back();
    EXPECT_THAT(list, IsEmpty());
  }
  {
    intrusive_list<AbstractBase, BaseLinkId> list;
    DerivedClass d1;
    VirtuallyDerivedClassA d2;
    VirtuallyDerivedClassB d3;
    VirtuallyDerivedClassC d4;
    EXPECT_THAT(list, IsEmpty());
    list.push_back(&d1);
    EXPECT_EQ(list.size(), 1);
    list.push_back(&d2);
    EXPECT_EQ(list.size(), 2);
    list.push_back(&d3);
    EXPECT_EQ(list.size(), 3);
    list.push_back(&d4);
    EXPECT_EQ(list.size(), 4);
    auto it = list.begin();
    EXPECT_EQ((it++)->name(), "DerivedClass");
    EXPECT_EQ((it++)->name(), "VirtuallyDerivedClassA");
    EXPECT_EQ((it++)->name(), "VirtuallyDerivedClassB");
    EXPECT_EQ((it++)->name(), "VirtuallyDerivedClassC");
  }
  {
    intrusive_list<templated_base_link::DerivedClass> list;
    templated_base_link::DerivedClass elements[2];
    EXPECT_THAT(list, IsEmpty());
    list.push_back(&elements[0]);
    EXPECT_EQ(list.size(), 1);
    list.push_back(&elements[1]);
    EXPECT_EQ(list.size(), 2);
    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    list.pop_back();
    EXPECT_THAT(list, IsEmpty());
  }
}

class IntrusiveListTagTypeTest : public ::testing::Test {
 protected:
  struct Tag {};
  class Element : public intrusive_link<Element, Tag> {};
};

TEST_F(IntrusiveListTagTypeTest, TagTypeListID) {
  intrusive_list<Element, Tag> list;
  Element e;
  EXPECT_THAT(list, IsEmpty());
  list.push_back(&e);
  EXPECT_THAT(list, ElementsAre(Ref(e)));
  list.pop_back();
  EXPECT_THAT(list, IsEmpty());
}

void BM_Erase_Insert(benchmark::State& state) {
  TestItem items[10];
  TestList list;
  for (TestItem& item : items) {
    list.push_back(&item);
  }
  auto* const middle = &items[5];
  for (const auto s : state) {
    auto it = TestList::erase(middle);
    benchmark::DoNotOptimize(it);
    benchmark::DoNotOptimize(list);
    TestList::insert(it, middle);
    benchmark::DoNotOptimize(list);
  }
}
BENCHMARK(BM_Erase_Insert);

void BM_PushBackPopFront(benchmark::State& state) {
  const int n = state.range(0);
  std::vector<TestItem> items(n);
  TestList list;
  for (TestItem& item : items) {
    list.push_back(&item);
  }
  for (const auto s : state) {
    // Before the loop the items are in order.
    for (TestItem& item : items) {
      list.pop_front();  // pops `item`.
      benchmark::DoNotOptimize(list);
      list.push_back(&item);
      benchmark::DoNotOptimize(list);
    }
    // After the loop the items are in order.
  }
}
BENCHMARK(BM_PushBackPopFront)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_PopBackPushFront(benchmark::State& state) {
  const int n = state.range(0);
  std::vector<TestItem> items(n);
  TestList list;
  for (TestItem& item : items) {
    list.push_back(&item);
  }
  for (const auto s : state) {
    // Before the loop the items are in order.
    for (TestItem& item : reversed_view(items)) {
      list.pop_back();  // pops `item`.
      benchmark::DoNotOptimize(list);
      list.push_front(&item);
      benchmark::DoNotOptimize(list);
    }
    // After the loop the items are in order.
  }
}
BENCHMARK(BM_PopBackPushFront)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

}  // namespace
}  // namespace gtl
