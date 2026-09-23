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

#include "gloop/util/gtl/manual_constructor.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

static int constructor_count_ = 0;

template <int kSize>
struct TestN {
  TestN() { ++constructor_count_; }
  ~TestN() { --constructor_count_; }
  char a[kSize];
};

typedef TestN<1> Test1;
typedef TestN<2> Test2;
typedef TestN<3> Test3;
typedef TestN<4> Test4;
typedef TestN<5> Test5;
typedef TestN<9> Test9;
typedef TestN<15> Test15;

class Unmovable {
 public:
  explicit Unmovable(std::string value) : value_(std::move(value)) {}

  Unmovable(const Unmovable&) = delete;
  Unmovable& operator=(const Unmovable&) = delete;

  const std::string& value() const { return value_; }

 private:
  std::string value_;
};

class MoveOnly {
 public:
  explicit MoveOnly(std::string value) : value_(value) {}

  MoveOnly(MoveOnly&& other) = default;
  MoveOnly& operator=(MoveOnly&& other) = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;

  // Can be passed to absl::StrCat.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const MoveOnly& v) {
    sink.Append(v.value_);
  }

 private:
  std::string value_;
};

}  // namespace

namespace {

TEST(ManualConstructorTest, Sizeof) {
  CHECK_EQ(sizeof(ManualConstructor<Test1>), sizeof(Test1));
  CHECK_EQ(sizeof(ManualConstructor<Test2>), sizeof(Test2));
  CHECK_EQ(sizeof(ManualConstructor<Test3>), sizeof(Test3));
  CHECK_EQ(sizeof(ManualConstructor<Test4>), sizeof(Test4));
  CHECK_EQ(sizeof(ManualConstructor<Test5>), sizeof(Test5));
  CHECK_EQ(sizeof(ManualConstructor<Test9>), sizeof(Test9));
  CHECK_EQ(sizeof(ManualConstructor<Test15>), sizeof(Test15));

  CHECK_EQ(constructor_count_, 0);
  ManualConstructor<Test1> mt[4];
  CHECK_EQ(sizeof(mt), 4);
  CHECK_EQ(constructor_count_, 0);
  mt[0].Init();
  CHECK_EQ(constructor_count_, 1);
  mt[0].Destroy();
}

TEST(ManualConstructorTest, Alignment) {
  // We want to make sure that ManualConstructor aligns its memory properly
  // on a word barrier.  Otherwise, it might be unexpectedly slow, since
  // memory access will be unaligned.

  struct {
    char a;
    ManualConstructor<void*> b;
  } test1;
  struct {
    char a;
    void* b;
  } control1;

  // TODO: Make these tests more direct with C++11 alignment_of<T>::value.
  EXPECT_EQ(reinterpret_cast<char*>(test1.b.get()) - &test1.a,
            reinterpret_cast<char*>(&control1.b) - &control1.a);
  EXPECT_EQ(reinterpret_cast<intptr_t>(test1.b.get()) % sizeof(control1.b), 0);

  struct {
    char a;
    ManualConstructor<long double> b;
  } test2;
  struct {
    char a;
    long double b;
  } control2;

  EXPECT_EQ(reinterpret_cast<char*>(test2.b.get()) - &test2.a,
            reinterpret_cast<char*>(&control2.b) - &control2.a);
  EXPECT_EQ(reinterpret_cast<intptr_t>(test2.b.get()) % alignof(long double),
            0);
}

template <typename T>
void CheckAlignment() {
  ManualConstructor<T> t;
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(t.get()) % alignof(T));
  struct S {
    char a;
    T t;
  };
  ManualConstructor<S> s;
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(&(s->t)) % alignof(T));
}

TEST(ManualConstructorTest, AlignmentGeneral) {
  CheckAlignment<char>();
  CheckAlignment<short>();  // NOLINT(runtime/int)
  CheckAlignment<int>();
  CheckAlignment<long>();       // NOLINT(runtime/int)
  CheckAlignment<long long>();  // NOLINT(runtime/int)
  CheckAlignment<float>();
  CheckAlignment<double>();
  CheckAlignment<long double>();
  CheckAlignment<void*>();
  CheckAlignment<std::string>();
  CheckAlignment<std::vector<int>>();
}

TEST(ManualConstructorTest, DefaultInitialize) {
  struct X {
    X() : x(123) {}
    int x;
  };
  union {
    ManualConstructor<X> x;
    ManualConstructor<int> y;
  } u;
  *u.y = -1;
  u.x.Init();  // should default-initialize u.x
  EXPECT_EQ(123, u.x->x);
  u.x.Destroy();
}

TEST(ManualConstructorTest, ZeroInitializePOD) {
  union {
    ManualConstructor<int> x;
    ManualConstructor<int> y;
  } u;
  *u.y = -1;
  u.x.Init();  // should not zero-initialize u.x
  EXPECT_EQ(-1, *u.y);
  u.x.Destroy();

  u.x.DefaultInit();  // should not zero-initialize u.x
  EXPECT_EQ(-1, *u.y);
  u.x.Destroy();

  u.x.ValueInit();  // should zero-initialize u.x
  EXPECT_EQ(0, *u.y);
  u.x.Destroy();
}

TEST(ManualConstructorTest, CopyCtorInitialize) {
  ManualConstructor<std::vector<int>> v;
  v.Init({1, 2, 3});
  EXPECT_THAT((*v), testing::ElementsAre(1, 2, 3));
  v.Destroy();
}

TEST(ManualConstructorTest, IsAlwaysTrivialType) {
  struct NonTrivial {
    explicit NonTrivial(int) {}
  };

  ASSERT_FALSE(std::is_trivial_v<NonTrivial>);
  EXPECT_TRUE(std::is_trivial_v<ManualConstructor<NonTrivial>>);
}

// ManualConstructor<T> is trivially copyable even if T is not.
// This means it can be byte-copied (e.g. via memcpy or copy construction),
// which can be dangerous if T is not trivially copyable. See warning in
// manual_constructor.h.
TEST(ManualConstructorTest, IsTriviallyCopyable) {
  static_assert(!std::is_trivially_copyable_v<Unmovable>);
  static_assert(!std::is_trivially_copyable_v<MoveOnly>);
  static_assert(!std::is_trivially_copyable_v<std::string>);
  static_assert(!std::is_trivially_copyable_v<std::vector<int>>);

  static_assert(std::is_trivially_copyable_v<ManualConstructor<int>>);
  static_assert(std::is_trivially_copyable_v<ManualConstructor<Unmovable>>);
  static_assert(std::is_trivially_copyable_v<ManualConstructor<MoveOnly>>);
  static_assert(std::is_trivially_copyable_v<ManualConstructor<std::string>>);
  static_assert(
      std::is_trivially_copyable_v<ManualConstructor<std::vector<int>>>);
}

enum class ConstructionType { kCopy, kMove };

class ManualConstructorBadConstructionTest
    : public ::testing::TestWithParam<ConstructionType> {};

TEST_P(ManualConstructorBadConstructionTest, DoubleDestructionAfterCopyOrMove) {
  static absl::NoDestructor<std::vector<int*>> deleted_pointers;
  deleted_pointers->clear();
  struct Owner {
    Owner() : p(std::make_unique<int>(42)) {}
    Owner(Owner&&) = default;
    Owner& operator=(Owner&&) = default;
    ~Owner() {
      deleted_pointers->push_back(p.get());
      // We don't delete p, to avoid double-free UB. The test itself is
      // responsible for cleanup.
      p.release();
    }
    std::unique_ptr<int> p;
  };
  static_assert(!std::is_copy_assignable_v<Owner>);
  static_assert(!std::is_copy_constructible_v<Owner>);
  static_assert(!std::is_trivially_copyable_v<Owner>);
  static_assert(std::is_move_constructible_v<Owner>);
  static_assert(std::is_move_assignable_v<Owner>);

  static_assert(std::is_trivially_copyable_v<ManualConstructor<Owner>>);
  static_assert(
      std::is_trivially_copy_constructible_v<ManualConstructor<Owner>>);
  static_assert(
      std::is_trivially_move_constructible_v<ManualConstructor<Owner>>);
  static_assert(std::is_trivially_copy_assignable_v<ManualConstructor<Owner>>);
  static_assert(std::is_trivially_move_assignable_v<ManualConstructor<Owner>>);
  ManualConstructor<Owner> mc1;
  mc1.Init();
  ASSERT_EQ(deleted_pointers->size(), 0);
  ManualConstructor<Owner> mc2 =
      GetParam() == ConstructionType::kCopy ? mc1 : std::move(mc1);

  // We intentionally call Destroy() on mc1 after it was "moved" because
  // ManualConstructor is trivially-copyable and the move is just a copy. This
  // test demonstrates that copying ManualConstructor can lead to double
  // destruction.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  mc1.Destroy();
  mc2.Destroy();

  ASSERT_EQ(deleted_pointers->size(), 2);
  // `mc2 = mc1;` or `mc2 = std::move(mc1)` created a bitwise copy of `mc1`.
  // `~Owner()` for `mc1` and `~Owner()` for `mc2` will both be called with
  // identical `p` members. If `~Owner()` called `delete p`, this would be a
  // double-free. Instead, we just record the pointers and check that they are
  // the same, proving that double-destruction of p would occur.
  ASSERT_EQ(deleted_pointers->at(0), deleted_pointers->at(1));
  // Clean up the allocation to avoid memory leaks in the test.
  delete deleted_pointers->at(0);
}

INSTANTIATE_TEST_SUITE_P(CopyAndMove, ManualConstructorBadConstructionTest,
                         ::testing::Values(ConstructionType::kCopy,
                                           ConstructionType::kMove),
                         [](const auto& info) {
                           return info.param == ConstructionType::kCopy
                                      ? "Copy"
                                      : "Move";
                         });

TEST(ManualConstructorTest, InitViaCallable) {
  ManualConstructor<Unmovable> v;
  v.InitViaCallable([]() { return Unmovable("hello"); });
  EXPECT_EQ(v->value(), "hello");
  v.Destroy();
}

struct StrCat {
  template <typename... Args>
  std::string operator()(Args&&... args) const {
    return absl::StrCat(std::forward<Args>(args)...);
  }
};

TEST(ManualConstructorTest, InitViaCallableMultiArgs) {
  ManualConstructor<Unmovable> v;
  v.InitViaCallable(StrCat(), "hello ", MoveOnly("world"));
  EXPECT_EQ(v->value(), "hello world");
  v.Destroy();
}

TEST(ManualConstructorTest, LValueReference) {
  static_assert(sizeof(ManualConstructor<char&>) >= sizeof(void*),
                "size of object appears buggy");
  ManualConstructor<Unmovable&> v;
  Unmovable x("hello");
  v.InitViaCallable([&]() -> Unmovable& { return x; });

  static_assert(std::is_same_v<decltype(*v), Unmovable&>);
  EXPECT_EQ(&*v, &x);
  EXPECT_EQ(v.get(), &x);
  EXPECT_EQ(v->value(), "hello");

  static_assert(std::is_same_v<decltype(*std::as_const(v)), Unmovable&>);
  EXPECT_EQ(&*std::as_const(v), &x);
  EXPECT_EQ(std::as_const(v).get(), &x);
  EXPECT_EQ(std::as_const(v)->value(), "hello");

  v.Destroy();
}

TEST(ManualConstructorTest, RValueReference) {
  static_assert(sizeof(ManualConstructor<char&&>) >= sizeof(void*),
                "size of object appears buggy");
  ManualConstructor<Unmovable&&> u;
  Unmovable y("hello");
  u.InitViaCallable([&]() -> Unmovable&& { return std::move(y); });

  static_assert(std::is_same_v<decltype(*u), Unmovable&&>);
  Unmovable&& ref = *u;
  EXPECT_EQ(&ref, &y);
  EXPECT_EQ(u.get(), &y);
  EXPECT_EQ(u->value(), "hello");
  EXPECT_EQ(u.operator->(), &y);

  static_assert(std::is_same_v<decltype(*std::as_const(u)), Unmovable&&>);
  static_assert(
      std::is_same_v<decltype(std::as_const(u).operator->()), Unmovable*>);
  Unmovable&& cref = *std::as_const(u);
  EXPECT_EQ(&cref, &y);
  EXPECT_EQ(std::as_const(u).get(), &y);
  EXPECT_EQ(std::as_const(u)->value(), "hello");
  EXPECT_EQ(std::as_const(u).operator->(), &y);

  u.Destroy();
}

}  // namespace
