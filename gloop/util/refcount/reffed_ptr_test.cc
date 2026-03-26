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

#include "gloop/util/refcount/reffed_ptr.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/node_hash_map.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

// This class implements a ref-counting interface, but doesn't actually delete
// itself automatically.  This makes it easy to check when the ref count has
// reached 0 for testing.
class RCTester {
 public:
  RCTester() : ref_count_(1) {}
  ~RCTester() = default;

  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  void Unref() { ref_count_.fetch_sub(1, std::memory_order_acq_rel); }

  int count() const { return ref_count_.load(std::memory_order_relaxed); }

 private:
  std::atomic<int32_t> ref_count_;
};

}  // namespace

namespace refcount {
namespace {

// Verifies that we can annotate the `reffed_ptr` type with Nullability
// annotations.
TEST(ReffedPtrTest, PassesThroughAnnotations) {
  using T = reffed_ptr<RCTester>;
  EXPECT_TRUE((std::is_same_v<absl_nonnull T, T>));
  EXPECT_TRUE((std::is_same_v<absl_nullable T, T>));
  EXPECT_TRUE((std::is_same_v<absl_nullability_unknown T, T>));
}

TEST(ReffedPtrTest, NullConstructor) {
  reffed_ptr<RCTester> a;
  ASSERT_TRUE(a.get() == nullptr);
  ASSERT_TRUE(a.release() == nullptr);

  reffed_ptr<RCTester> b(nullptr);
  ASSERT_TRUE(b.get() == nullptr);
  ASSERT_TRUE(b.release() == nullptr);

  reffed_ptr<RCTester> c(nullptr, RefTakingMode::kAdopt);
  ASSERT_TRUE(c.get() == nullptr);
  ASSERT_TRUE(c.release() == nullptr);

  reffed_ptr<RCTester> d(nullptr, RefTakingMode::kCreate);
  ASSERT_TRUE(d.get() == nullptr);
  ASSERT_TRUE(d.release() == nullptr);
}

// Test that reffed_ptr can be constexpr-initialized.
TEST(ReffedPtrTest, ConstexprConstructor) {
  ABSL_CONST_INIT static reffed_ptr<RCTester> kConstexprPtr;  // NOLINT
  ASSERT_TRUE(kConstexprPtr.get() == nullptr);

  ABSL_CONST_INIT static reffed_ptr<RCTester> kNull = nullptr;  // NOLINT
  ASSERT_TRUE(kNull.get() == nullptr);
}

static void TakesPtrRef(const reffed_ptr<RCTester>& p = {}) {}
TEST(ReffedPtrTest, ImplicitDefaultConstructor) {
  // check implicit default ctor.
  const reffed_ptr<RCTester>& p ABSL_ATTRIBUTE_UNUSED = {};
  TakesPtrRef({});
  TakesPtrRef();
}

RCTester* PtrOf(const reffed_ptr<RCTester>& p) { return p.get(); }
TEST(ReffedPtrTest, ImplicitNullptrConstructor) {
  reffed_ptr<RCTester> a = nullptr;
  ASSERT_TRUE(a.get() == nullptr);
  ASSERT_TRUE(a.release() == nullptr);

  // Check implicit conversion from nullptr.
  EXPECT_TRUE(PtrOf(nullptr) == nullptr);
}

TEST(ReffedPtrTest, PtrConstructor) {
  RCTester rct;
  {
    reffed_ptr<RCTester> a(&rct);
    ASSERT_EQ((*a).count(), 1);
    ASSERT_EQ(a->count(), 1);
    ASSERT_EQ(a->count(), 1);
    ASSERT_EQ(a.get(), &rct);
  }
  ASSERT_EQ(rct.count(), 0);

  {
    reffed_ptr<RCTester> a(&rct, RefTakingMode::kAdopt);
    ASSERT_EQ(a->count(), 0);
    ASSERT_EQ(a.release(), &rct);
    ASSERT_EQ(rct.count(), 0);
  }
  ASSERT_EQ(rct.count(), 0);

  {
    reffed_ptr<RCTester> a(&rct, RefTakingMode::kCreate);
    ASSERT_EQ(a->count(), 1);
  }
  ASSERT_EQ(rct.count(), 0);
}

TEST(ReffedPtrTest, CopyConstructor) {
  RCTester rct;
  {
    reffed_ptr<RCTester> a(&rct);
    ASSERT_EQ(rct.count(), 1);
    reffed_ptr<RCTester> b(a);
    ASSERT_EQ(rct.count(), 2);
  }
  ASSERT_EQ(rct.count(), 0);
}

TEST(ReffedPtrTest, ExplicitBool) {
  RCTester obj;
  reffed_ptr<RCTester> p(&obj);
  EXPECT_TRUE(p);
  EXPECT_TRUE(static_cast<bool>(p));
  p.reset();
  EXPECT_FALSE(p);
  EXPECT_FALSE(static_cast<bool>(p));
}

TEST(ReffedPtrTest, CopyConstructorFromOtherType) {
  class Derived : public RCTester {};
  Derived rct;

  {
    reffed_ptr<RCTester> a(&rct);
    ASSERT_EQ(rct.count(), 1);
    reffed_ptr<RCTester> b(a);
    ASSERT_EQ(rct.count(), 2);

    reffed_ptr<Derived> c(&rct, RefTakingMode::kCreate);
    ASSERT_EQ(rct.count(), 3);
    reffed_ptr<RCTester> d(c);
    ASSERT_EQ(rct.count(), 4);
    reffed_ptr<RCTester> e = c;
    ASSERT_EQ(rct.count(), 5);
  }

  ASSERT_EQ(rct.count(), 0);
}

// TEST(ReffedPtrTest, PtrAssignment) {
//   RCTester rct_a;
//   reffed_ptr<RCTester> a;

//   // From null to ptr (adopts the ref):
//   ASSERT_EQ(rct_a.count(), 1);
//   ASSERT_TRUE(a.get() == nullptr);
//   a = &rct_a;
//   ASSERT_EQ(rct_a.count(), 1);
//   ASSERT_EQ(a.get(), &rct_a);

//   // From ptr to null:
//   a = nullptr;
//   ASSERT_EQ(rct_a.count(), 0);
//   ASSERT_TRUE(a.get() == nullptr);

//   // From one ptr to another:
//   RCTester rct_b;
//   rct_a.Ref();
//   a = &rct_a;
//   ASSERT_EQ(rct_a.count(), 1);
//   ASSERT_EQ(rct_b.count(), 1);

//   a = &rct_b;
//   ASSERT_EQ(rct_a.count(), 0);
//   ASSERT_EQ(rct_b.count(), 1);

//   // From one ptr to same:
//   a = &rct_b;
//   ASSERT_EQ(rct_b.count(), 1);
// }

TEST(ReffedPtrTest, ReffedPtrAssignment) {
  RCTester rct_a;
  reffed_ptr<RCTester> a(&rct_a);
  RCTester rct_b;
  reffed_ptr<RCTester> b;

  // From null to ptr (adopts the ref):
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);
  ASSERT_EQ(a.get(), &rct_a);
  ASSERT_TRUE(b.get() == nullptr);
  b = a;
  ASSERT_EQ(rct_a.count(), 2);
  ASSERT_EQ(rct_b.count(), 1);
  ASSERT_EQ(a.get(), &rct_a);
  ASSERT_EQ(b.get(), &rct_a);

  // From ptr to null:
  b.reset(nullptr);
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(a.get(), &rct_a);
  ASSERT_TRUE(b.get() == nullptr);

  a = b;
  ASSERT_EQ(rct_a.count(), 0);
  ASSERT_TRUE(a.get() == nullptr);
  ASSERT_TRUE(b.get() == nullptr);

  // From one ptr to another:
  a.reset(&rct_a, RefTakingMode::kCreate);
  b.reset(&rct_b, RefTakingMode::kAdopt);
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);

  b = a;
  ASSERT_EQ(rct_a.count(), 2);
  ASSERT_EQ(rct_b.count(), 0);
  ASSERT_EQ(b.get(), &rct_a);

  // From one ptr to same:
  b = a;
  ASSERT_EQ(rct_a.count(), 2);
  ASSERT_EQ(b.get(), &rct_a);

  // From one ptr to self:
  b = *&b;
  ASSERT_EQ(rct_a.count(), 2);
  ASSERT_EQ(b.get(), &rct_a);
}

TEST(ReffedPtrTest, NullptrAssignment) {
  RCTester rct_a;
  RCTester rct_b;
  reffed_ptr<RCTester> a(&rct_a, RefTakingMode::kAdopt);
  reffed_ptr<RCTester> b(&rct_b, RefTakingMode::kAdopt);
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);

  a = nullptr;
  ASSERT_EQ(rct_a.count(), 0);
  ASSERT_TRUE(a.get() == nullptr);

  b.reset(nullptr);
  ASSERT_EQ(rct_b.count(), 0);
  ASSERT_TRUE(b.get() == nullptr);
}

TEST(ReffedPtrTest, ReffedPtrAssignmentFromOtherType) {
  class Derived : public RCTester {};
  Derived rct, rct2;

  {
    reffed_ptr<RCTester> a(&rct);
    ASSERT_EQ(rct.count(), 1);

    reffed_ptr<Derived> b(&rct, RefTakingMode::kCreate);
    ASSERT_EQ(rct.count(), 2);
    a = b;
    ASSERT_EQ(rct.count(), 2);

    reffed_ptr<Derived> c(&rct2);
    ASSERT_EQ(rct2.count(), 1);
    a = c;

    ASSERT_EQ(rct.count(), 1);
    ASSERT_EQ(rct2.count(), 2);
  }

  ASSERT_EQ(rct.count(), 0);
  ASSERT_EQ(rct2.count(), 0);
}

struct History {
  void rec(std::string s) { v.push_back(s); }

  friend std::ostream& operator<<(std::ostream& os, const History& v) {
    os << "(";
    const char* sep = "";
    for (size_t i = 0; i < v.v.size(); ++i) {
      os << sep << v.v[i];
      sep = ",";
    }
    return os << ")";
  }

  std::vector<std::string> v;
};

TEST(ReffedPtrTest, HeterogeneousCopy) {
  struct B {
    virtual ~B() = default;
    explicit B(History* h) : ref_(1), h_(h) { h_->rec("B()"); }
    void Ref() const {
      h_->rec("B+");
      ++ref_;
    }
    void Unref() const {
      h_->rec("B-");
      if (!--ref_) {
        h_->rec("~B");
        delete this;
      }
    }
    mutable int ref_;
    History* h_;
  };
  struct D : B {
    explicit D(History* h) : B(h), ref_(1), h_(h) { h_->rec("D()"); }
    void Ref() const {
      B::Ref();
      h_->rec("D+");
      ++ref_;
    }
    void Unref() const {
      h_->rec("D-");
      if (!--ref_) {
        h_->rec("~D");
      }
      B::Unref();
    }
    mutable int ref_;
    History* h_;
  };
  History history;

  reffed_ptr<D> dp(new D(&history));
  reffed_ptr<B> bp = dp;
  dp.reset();
  bp.reset();

  std::ostringstream os;
  os << history;
  EXPECT_EQ("(B(),D(),B+,D-,~D,B-,B-,~B)", os.str());
}

TEST(ReffedPtrTest, Equality) {
  class D : public RCTester {};

  D rct;

  reffed_ptr<D> dp(&rct);
  EXPECT_FALSE(dp == nullptr);
  EXPECT_FALSE(nullptr == dp);
  EXPECT_TRUE(dp != nullptr);
  EXPECT_TRUE(nullptr != dp);

  reffed_ptr<RCTester> bp(dp);
  EXPECT_TRUE(bp == dp);
  EXPECT_TRUE(dp == bp);

  EXPECT_FALSE(bp != dp);
  EXPECT_FALSE(dp != bp);

  dp.reset();
  EXPECT_TRUE(dp == nullptr);
  EXPECT_TRUE(nullptr == dp);
  EXPECT_FALSE(dp != nullptr);
  EXPECT_FALSE(nullptr != dp);

  EXPECT_FALSE(bp == dp);
  EXPECT_FALSE(dp == bp);

  EXPECT_TRUE(bp != dp);
  EXPECT_TRUE(dp != bp);

  EXPECT_EQ(1, rct.count());
}

TEST(ReffedPtrTest, EqualityWithConvertible) {
  struct RefWrap {
    explicit RefWrap(reffed_ptr<RCTester>* p) : p_(p) {}
    operator reffed_ptr<RCTester>&() const { return *p_; }
    reffed_ptr<RCTester>* const p_;
  };
  RCTester rct;
  reffed_ptr<RCTester> p(&rct);
  RefWrap wrap(&p);
  EXPECT_TRUE(p == wrap);
  EXPECT_TRUE(wrap == p);
  EXPECT_FALSE(p != wrap);
  EXPECT_FALSE(wrap != p);
}

TEST(ReffedPtrTest, Hash) {
  RCTester rc0;
  RCTester rc1;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      reffed_ptr<RCTester>(&rc0), reffed_ptr<RCTester>(&rc1), nullptr)));
}

TEST(ReffedPtrTest, CompareNullNull) {
  reffed_ptr<RCTester> null_ptr;
  EXPECT_FALSE(null_ptr < null_ptr);
  EXPECT_TRUE(null_ptr <= null_ptr);
  EXPECT_FALSE(null_ptr > null_ptr);
  EXPECT_TRUE(null_ptr >= null_ptr);
}

TEST(ReffedPtrTest, CompareNullNotNull) {
  reffed_ptr<RCTester> null_ptr;
  RCTester rct0;
  reffed_ptr<RCTester> not_null_ptr(&rct0);
  EXPECT_TRUE(null_ptr < not_null_ptr);
  EXPECT_FALSE(not_null_ptr < null_ptr);
  EXPECT_TRUE(null_ptr <= not_null_ptr);
  EXPECT_FALSE(not_null_ptr <= null_ptr);
  EXPECT_FALSE(null_ptr > not_null_ptr);
  EXPECT_TRUE(not_null_ptr > null_ptr);
  EXPECT_FALSE(null_ptr >= not_null_ptr);
  EXPECT_TRUE(not_null_ptr >= null_ptr);
}

TEST(ReffedPtrTest, CompareNotNullNotNull) {
  // Use an array so we know that &rct[0] < &rct[1].
  RCTester rct[2];
  reffed_ptr<RCTester> not_null_ptr0(&rct[0]);
  reffed_ptr<RCTester> not_null_ptr1(&rct[1]);
  EXPECT_TRUE(not_null_ptr0 < not_null_ptr1);
  EXPECT_FALSE(not_null_ptr0 < not_null_ptr0);
  EXPECT_FALSE(not_null_ptr1 < not_null_ptr0);

  EXPECT_TRUE(not_null_ptr0 <= not_null_ptr1);
  EXPECT_TRUE(not_null_ptr0 <= not_null_ptr0);
  EXPECT_FALSE(not_null_ptr1 <= not_null_ptr0);

  EXPECT_FALSE(not_null_ptr0 > not_null_ptr1);
  EXPECT_FALSE(not_null_ptr0 > not_null_ptr0);
  EXPECT_TRUE(not_null_ptr1 > not_null_ptr0);

  EXPECT_FALSE(not_null_ptr0 >= not_null_ptr1);
  EXPECT_TRUE(not_null_ptr0 >= not_null_ptr0);
  EXPECT_TRUE(not_null_ptr1 >= not_null_ptr0);
}

TEST(ReffedPtrTest, ComparisonConvertible) {
  class RCTesterChild : public RCTester {};
  RCTester rct0;
  reffed_ptr<RCTester> rct0_ptr(&rct0);
  RCTesterChild rct1;
  reffed_ptr<RCTesterChild> rct1_ptr(&rct1);

  // reffed_ptr comparison is the same as the comparison of the underlying
  // pointer.
  EXPECT_EQ(rct0_ptr < rct1_ptr, rct0_ptr.get() < rct1_ptr.get());
  EXPECT_EQ(rct0_ptr <= rct1_ptr, rct0_ptr.get() <= rct1_ptr.get());
  EXPECT_EQ(rct0_ptr > rct1_ptr, rct0_ptr.get() > rct1_ptr.get());
  EXPECT_EQ(rct0_ptr >= rct1_ptr, rct0_ptr.get() >= rct1_ptr.get());

  EXPECT_EQ(rct1_ptr < rct0_ptr, rct1_ptr.get() < rct0_ptr.get());
  EXPECT_EQ(rct1_ptr <= rct0_ptr, rct1_ptr.get() <= rct0_ptr.get());
  EXPECT_EQ(rct1_ptr > rct0_ptr, rct1_ptr.get() > rct0_ptr.get());
  EXPECT_EQ(rct1_ptr >= rct0_ptr, rct1_ptr.get() >= rct0_ptr.get());
}

TEST(ReffedPtrTest, Reset) {
  RCTester rct_a;

  // From null to pointer:
  {
    reffed_ptr<RCTester> p;
    p.reset(&rct_a);
    ASSERT_EQ(rct_a.count(), 1);
  }
  ASSERT_EQ(rct_a.count(), 0);

  {
    rct_a.Ref();
    ASSERT_EQ(rct_a.count(), 1);
    reffed_ptr<RCTester> p;
    p.reset(&rct_a, RefTakingMode::kAdopt);
    ASSERT_EQ(rct_a.count(), 1);
  }
  ASSERT_EQ(rct_a.count(), 0);

  {
    reffed_ptr<RCTester> p;
    p.reset(&rct_a, RefTakingMode::kCreate);
    ASSERT_EQ(rct_a.count(), 1);
  }
  ASSERT_EQ(rct_a.count(), 0);

  // From one ptr to another:
  rct_a.Ref();
  RCTester rct_b;
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);
  reffed_ptr<RCTester> p(&rct_a);
  p.reset(&rct_b);
  ASSERT_EQ(rct_a.count(), 0);
  ASSERT_EQ(rct_b.count(), 1);

  // From pointer to null
  p.reset(nullptr);
  ASSERT_EQ(rct_a.count(), 0);
  ASSERT_EQ(rct_b.count(), 0);

  // From one ptr to same:
  p.reset(&rct_a, RefTakingMode::kCreate);
  ASSERT_EQ(rct_a.count(), 1);
  p.reset(&rct_a, RefTakingMode::kCreate);
  ASSERT_EQ(rct_a.count(), 1);
  p.reset(&rct_a, RefTakingMode::kAdopt);
  ASSERT_EQ(rct_a.count(), 0);
}

TEST(ReffedPtrTest, ReleaseReset) {
  RCTester rct;
  ASSERT_EQ(rct.count(), 1);
  reffed_ptr<RCTester> ptr_a(&rct, RefTakingMode::kCreate);
  reffed_ptr<RCTester> ptr_b(&rct, RefTakingMode::kCreate);
  ASSERT_EQ(rct.count(), 3);
  ptr_a.reset(ptr_b.release());
  ASSERT_EQ(rct.count(), 2);
  ptr_a.reset(nullptr);
  ASSERT_EQ(rct.count(), 1);
}

TEST(ReffedPtrTest, Swap) {
  RCTester rct_a;
  reffed_ptr<RCTester> a(&rct_a);
  RCTester rct_b;
  reffed_ptr<RCTester> b(&rct_b);
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);
  ASSERT_EQ(a.get(), &rct_a);
  ASSERT_EQ(b.get(), &rct_b);

  a.swap(b);
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);
  ASSERT_EQ(a.get(), &rct_b);
  ASSERT_EQ(b.get(), &rct_a);

  swap(a, b);
  ASSERT_EQ(rct_a.count(), 1);
  ASSERT_EQ(rct_b.count(), 1);
  ASSERT_EQ(a.get(), &rct_a);
  ASSERT_EQ(b.get(), &rct_b);
}

TEST(ReffedPtrTest, STLMembership) {
  std::vector<reffed_ptr<RCTester>> v;
  absl::node_hash_map<int, reffed_ptr<RCTester>> m;
}

TEST(ReffedPtrTest, CanUseWithGmockReturnNullAction) {
  class FooMock {
   public:
    MOCK_METHOD(reffed_ptr<RCTester>, Foo, ());
  };
  FooMock mock;
  EXPECT_CALL(mock, Foo()).WillOnce(testing::ReturnNull());
  reffed_ptr<RCTester> ptr = mock.Foo();
  EXPECT_TRUE(ptr.get() == nullptr);
}

class ReffedPtrAssignTest : public testing::Test {
 protected:
  struct RC {
    RC() : refs(0), unrefs(0) {}
    void Ref() const {
      ++refs;
      VLOG(1) << "Ref";
    }
    void Unref() const {
      ++unrefs;
      VLOG(1) << "Unref";
    }
    mutable int refs;
    mutable int unrefs;
  };

  template <typename T>
  static reffed_ptr<T> MakeReffed(T* p) ABSL_ATTRIBUTE_NOINLINE;
};

template <typename T>
reffed_ptr<T> ReffedPtrAssignTest::MakeReffed(T* p) {
  return reffed_ptr<T>(p, RefTakingMode::kCreate);
}

TEST_F(ReffedPtrAssignTest, CopyElisionNotPossible) {
  // The argument to operator= is an lvalue. There's no way for
  // the compiler to assign it without copying the ref.
  RC obj;
  {
    reffed_ptr<RC> old_ptr;
    reffed_ptr<RC> new_ptr(&obj, RefTakingMode::kCreate);
    EXPECT_EQ(1, obj.refs);
    old_ptr = new_ptr;
    EXPECT_EQ(2, obj.refs);
  }
  EXPECT_EQ(obj.refs, obj.unrefs);
}

TEST_F(ReffedPtrAssignTest, CopyOfReturnedTemporary) {
  // Verify that copy elision of temporaries saves a call to Ref().
  // Test that if operator= takes a temporary reffed_ptr by value,
  // the compiler can elide that copy and construct the temporary
  // directly into the stack frame of the assignment operator.
  // This elision saves a Ref() and Unref() call which would be
  // necessary otherwise. When the source is passed by reference,
  // the implementation can only increment the ref count on the 'other'
  // object. The body of operator= has to be written to increment
  // a reference somehow. It cannot distinguish a temporary from a
  // live value, only the compiler can do that.
  RC obj;
  {
    reffed_ptr<RC> old_ptr;
    old_ptr = MakeReffed(&obj);
  }
  EXPECT_EQ(1, obj.refs);
  EXPECT_EQ(obj.refs, obj.unrefs);
}

TEST_F(ReffedPtrAssignTest, CopyOfExplicitTemporary) {
  // Verify that copy elision of temporaries saves a call to Ref().
  RC obj;
  {
    reffed_ptr<RC> old_ptr;
    old_ptr = reffed_ptr<RC>(&obj, RefTakingMode::kCreate);
  }
  EXPECT_EQ(1, obj.refs);
  EXPECT_EQ(obj.refs, obj.unrefs);
}

TEST_F(ReffedPtrAssignTest, StdMove) {
  RC rc_a;
  RC rc_b;
  {
    auto a = MakeReffed(&rc_a);
    auto b = MakeReffed(&rc_b);
    EXPECT_EQ(1, rc_a.refs);
    EXPECT_EQ(0, rc_a.unrefs);
    EXPECT_EQ(1, rc_b.refs);
    EXPECT_EQ(0, rc_b.unrefs);
    b = std::move(a);
    EXPECT_EQ(nullptr, a.get());
    EXPECT_EQ(&rc_a, b.get());
    EXPECT_EQ(1, rc_a.refs);
    EXPECT_EQ(0, rc_a.unrefs);
    EXPECT_EQ(1, rc_b.refs);
    EXPECT_EQ(1, rc_b.unrefs);
  }
  EXPECT_EQ(1, rc_a.refs);
  EXPECT_EQ(1, rc_a.unrefs);
  EXPECT_EQ(1, rc_b.refs);
  EXPECT_EQ(1, rc_b.unrefs);
}

TEST_F(ReffedPtrAssignTest, NoThrowMoves) {
  static_assert(std::is_nothrow_move_constructible<reffed_ptr<RC>>::value, "");
  static_assert(std::is_nothrow_move_assignable<reffed_ptr<RC>>::value, "");

  // Since reffed_ptr is nothrow move constructible, vector resizes should move
  // rather than copy reffed_ptrs.
  RC rc;

  std::vector<reffed_ptr<RC>> rcs;
  rcs.push_back(MakeReffed(&rc));
  EXPECT_EQ(1, rc.refs);
  EXPECT_EQ(0, rc.unrefs);

  rcs.reserve(rcs.capacity() * 2);
  EXPECT_EQ(1, rc.refs);
  EXPECT_EQ(0, rc.unrefs);
}

TEST(WrapReffedTest, Default) {
  RCTester rct;
  auto p = WrapReffed(&rct);
  EXPECT_EQ(1, p->count());
  EXPECT_EQ(&rct, p.get());
}

TEST(WrapReffedTest, Adopt) {
  RCTester rct;
  auto p = WrapReffed(&rct, RefTakingMode::kAdopt);
  EXPECT_EQ(1, p->count());
  EXPECT_EQ(&rct, p.get());
}

TEST(WrapReffedTest, Create) {
  RCTester rct;
  auto p = WrapReffed(&rct, RefTakingMode::kCreate);
  EXPECT_EQ(2, p->count());
  EXPECT_EQ(&rct, p.get());
}

TEST(MakeReffedTest, StartsAtOneDefault) {
  reffed_ptr p = MakeReffed<RCTester>();
  EXPECT_EQ(1, p->count());
  delete p.release();
}

class RCTesterZero {
 public:
  using ref_count_starts_at_zero = void;
  void Ref() { ++count_; }
  void Unref() { --count_; }
  int Count() const { return count_; }

 private:
  int count_ = 0;
};

TEST(MakeReffedTest, StartsAtZero) {
  auto p = MakeReffed<RCTesterZero>();
  EXPECT_EQ(1, p->Count());
  delete p.release();
}

class RCTesterSubclassOne : public RCTesterZero {
 public:
  void Ref() { ++count_; }
  void Unref() { --count_; }
  int Count() const { return count_; }

 private:
  using ref_count_starts_at_zero = void;
  int count_ = 1;
};

TEST(MakeReffedTest, SubclassCanGoBackToOne) {
  auto p = MakeReffed<RCTesterSubclassOne>();
  EXPECT_EQ(1, p->Count());
  delete p.release();
}

}  // namespace
}  // namespace refcount
