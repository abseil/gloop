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

#include "gloop/util/gtl/extend/extend.h"

#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/meta/internal/constexpr_testing.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/requires.h"

namespace test_extensions {
template <typename T>
struct FieldSumExtension : gtl::Extension<FieldSumExtension, T> {
  friend int FieldSum(const T& t) {
    return std::apply([](const auto&... fields) { return (0 + ... + fields); },
                      FieldSumExtension::Unpack(t));
  }

 private:
  using gtl::Extension<FieldSumExtension, T>::Unpack;
};
}  // namespace test_extensions

namespace {
using ::testing::Pointee;
using ::testing::Property;

// DO NOT SIMPLIFY THIS CODE.
//
// The reason this code is complicated (otherwise we could just use
// std::unique_ptr) is to illustrate a compiler bug that also gets surfaced by
// std::unique_ptr on MSVC, and by some user code as well. For some reason, the
// templated constructors cause the compiler to misbehave under the /permissive
// flag, which is the default under C++17 (but not since C++20). Notice how
// simplifying the constructors (in this case, by removing `template<...>`)
// avoids the bug on MSVC. There are other similar cases (see `SwapMock` in
// `swap_test.cc`) where the absence of `const` in a copy constructor also
// triggers this bug.
//
// To avoid the bug in user code under C++17, users can specify /permissive-
// instead. However, even then, MSVC's support is fragile: a no-op cast still
// causes the compiler to misbehave: https://godbolt.org/z/xME6eMqYr
//
// We therefore do not intend to support this on MSVC. This class is here to
// illustrate a minimal reproduction of the problem and its workaround.
//
// See b/292103371#comment1 for more information.
template <class T>
class UniquePtr {
 public:
  using element_type = T;

  ~UniquePtr() noexcept { delete ptr_; }

  // DO NOT SIMPLIFY. See documentation above.
  explicit UniquePtr(T* p) noexcept : ptr_(p) {}

  // DO NOT SIMPLIFY. See documentation above.
  template <int = 0>
  UniquePtr(std::nullptr_t) noexcept {}  // NOLINT

  // DO NOT SIMPLIFY. See documentation above.
  template <int = 0>
  UniquePtr(UniquePtr&& other) noexcept  // NOLINT
      : ptr_(std::exchange(other.ptr_, nullptr)) {}

  UniquePtr& operator=(UniquePtr&&);

  T& operator*() const { return *get(); }
  T* get() const noexcept { return ptr_; }

 private:
  T* absl_nullable ptr_ = nullptr;
};

template <class T, class... U>
UniquePtr<T> MakeUnique(U&&... args) {
  return UniquePtr<T>(new T(std::forward<U>(args)...));
}

struct S1 : gtl::Extend<S1>::With<> {
  int n;
  bool b;
  double x;
};

TEST(MakeFromTuple, ExtendConstructFrom) {
  auto s = gtl::ConstructExtend<S1>(42, true, 5.1);
  EXPECT_EQ(s.n, 42);
  EXPECT_TRUE(s.b);
  EXPECT_EQ(s.x, 5.1);
}

TEST(MakeFromTuple, LValueRef) {
  std::tuple<int, bool, double> tup(3, true, 5.1);
  S1 s = gtl::MakeFromTuple<S1>(tup);
  EXPECT_EQ(s.n, 3);
  EXPECT_TRUE(s.b);
  EXPECT_EQ(s.x, 5.1);
}

TEST(MakeFromTuple, ConstLValueRef) {
  const std::tuple<int, bool, double> tup(3, true, 5.1);
  S1 s = gtl::MakeFromTuple<S1>(tup);
  EXPECT_EQ(s.n, 3);
  EXPECT_TRUE(s.b);
  EXPECT_EQ(s.x, 5.1);
}

struct S2 : gtl::Extend<S2>::With<> {
  UniquePtr<int> p;
  bool b;
  double x;
};

TEST(MakeFromTuple, RValueRef) {
  S2 s = gtl::MakeFromTuple<S2>(
      std::tuple<UniquePtr<int>, bool, double>(MakeUnique<int>(3), true, 5.1));
  EXPECT_THAT(s.p.get(), Pointee(3));
  EXPECT_TRUE(s.b);
  EXPECT_EQ(s.x, 5.1);
}

struct S3 : gtl::Extend<S3>::With<> {
  int x;
  int y;
};

TEST(MakeFromTuple, FromPair) {
  S3 s = gtl::MakeFromTuple<S3>(std::pair(2, 3));
  EXPECT_EQ(s.x, 2);
  EXPECT_EQ(s.y, 3);
}

// Works with any suitable tuple type.
TEST(MakeFromTuple, FromArray) {
  std::array<int, 2> array;
  array[0] = 2;
  array[1] = 3;
  S3 s = gtl::MakeFromTuple<S3>(array);
  EXPECT_EQ(s.x, 2);
  EXPECT_EQ(s.y, 3);
}

TEST(MakeFromTuple, CanBeConstantEvaluated) {
  using absl::meta_internal::HasConstexprEvaluation;
  EXPECT_TRUE(HasConstexprEvaluation([] {
    return gtl::MakeFromTuple<S1>(std::tuple<int, bool, double>{3, true, 5.1});
  }));
}

class NoCopyMove {
 public:
  NoCopyMove() = default;

  NoCopyMove(const NoCopyMove&) = delete;
  NoCopyMove(NoCopyMove&&) = delete;
  NoCopyMove& operator=(const NoCopyMove&) = delete;
  NoCopyMove& operator=(NoCopyMove&&) = delete;
};

struct RefMembers : gtl::Extend<RefMembers>::With<> {
  NoCopyMove& ref;
  int val;
};

TEST(MakeFromTuple, ConstructWithRef) {
  NoCopyMove no_copy_move;
  auto ref_members = gtl::ConstructExtend<RefMembers>(no_copy_move, 4);
  EXPECT_EQ(&ref_members.ref, &no_copy_move);
}

TEST(MakeFromTuple, MakeWithRef) {
  NoCopyMove no_copy_move;
  std::tuple<NoCopyMove&, int> tup = {no_copy_move, 4};
  auto ref_members = gtl::MakeFromTuple<RefMembers>(tup);
  EXPECT_EQ(&ref_members.ref, &no_copy_move);
}

class C1 : public gtl::Extend<C1, 3>::With<> {
 public:
  C1(UniquePtr<int> p, bool b, double x) : p_(std::move(p)), b_(b), x_(x) {}

  const UniquePtr<int>& p() const { return p_; }
  bool b() const { return b_; }
  double x() const { return x_; }

 private:
  friend gtl::EnableExtensions;
  UniquePtr<int> p_;
  bool b_;
  double x_;
};

TEST(MakeFromTuple, ClassType) {
  EXPECT_THAT(gtl::MakeFromTuple<C1>(std::tuple(MakeUnique<int>(3), true, 5.1)),
              AllOf(Property(&C1::p, Pointee(3)), Property(&C1::b, true),
                    Property(&C1::x, 5.1)));
}

class Class
    : public gtl::Extend<Class, 3>::With<test_extensions::FieldSumExtension> {
 public:
  explicit Class(int m, int n) : a_(m * m), b_(m * n), c_(n * n) {}

 private:
  friend gtl::EnableExtensions;
  int a_;
  int b_;
  int c_;
};

TEST(Class, Extension) {
  EXPECT_EQ(FieldSum(Class(1, 2)), 1 * 1 + 1 * 2 + 2 * 2);
  EXPECT_EQ(FieldSum(Class(2, 3)), 2 * 2 + 2 * 3 + 3 * 3);
}

template <typename T>
inline constexpr bool HasFieldCount =
    gtl::Requires<T>([](auto v) -> decltype(gtl::FieldCount<decltype(v)>()) {});

TEST(FieldCount, Extension) {
  EXPECT_TRUE(HasFieldCount<S1>);
  EXPECT_EQ(gtl::FieldCount<S1>(), 3);
  EXPECT_EQ(gtl::FieldCount<S2>(), 3);
  EXPECT_FALSE(HasFieldCount<Class>);
}

TEST(FieldCount, CanBeConstantEvaluated) {
  using absl::meta_internal::HasConstexprEvaluation;
  EXPECT_TRUE(HasConstexprEvaluation([] { return gtl::FieldCount<S1>(); }));
}

namespace {
// Note: This anonymous namespace is crucial to the test below testing what we
// expect. Even though the code is already in an anonymous namespace, we add
// another one so that the anonymous namespace's declaration is near the usage
// site.
struct Anon : gtl::Extend<Anon>::With<> {
  std::optional<int> n;
};

TEST(FieldCount, ConstexprConstructorInAnonymousNamespace) {
  Anon a;
  static_cast<void>(a);
}

// Verify that no padding is added in case the first member uses Extend.
struct Member : gtl::Extend<Member>::With<> {
  int x;
};
struct Parent : gtl::Extend<Parent>::With<> {
  Member m;
};
static_assert(std::is_standard_layout_v<Member>);
static_assert(std::is_standard_layout_v<Parent>);
static_assert(sizeof(Member) == sizeof(int));
static_assert(sizeof(Parent) == sizeof(int));

}  // namespace
}  // namespace
