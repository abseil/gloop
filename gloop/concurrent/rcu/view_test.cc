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

#include "gloop/concurrent/rcu/view.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/notification.h"
#include "benchmark/benchmark.h"
#include "gloop/base/sysinfo.h"
#include "gloop/concurrent/rcu/view_advanced.h"
#include "gtest/gtest.h"

namespace rcu {
namespace {

bool SetupFunc() {
  // Validate that there are no ASAN errors when using View<> from a
  // static-initializer.
  View<int> rv(std::make_unique<int>(1));
  rv.Update(std::make_unique<int>(2));
  rv.Update(nullptr);
  return true;
}
bool unused = SetupFunc();

TEST(ViewTest, DefaultIsNull) {
  View<int> rv_default;
  EXPECT_TRUE(rv_default.IsNull());
}

TEST(ViewTest, IsNull) {
  View<int> rv(std::make_unique<int>(-1));
  EXPECT_FALSE(rv.IsNull());
  rv.Update(nullptr);
  EXPECT_TRUE(rv.IsNull());
}

// Test that View<AbstractType> can be correctly constructed and updated from
// derived types. This is mainly a compile test; the actual checks aren't very
// important.
TEST(ViewTest, AbstractType) {
  class AbstractBase {
   public:
    virtual ~AbstractBase() = default;
    virtual int Sum() const = 0;
  };
  class Base1 : public AbstractBase {
   public:
    Base1(int x, int y) : x_(x), y_(y) {}
    ~Base1() override = default;
    int Sum() const override { return x_ + y_; }

   protected:
    int x_;
    int y_;
  };
  class Derived2 : public Base1 {
   public:
    explicit Derived2(int x) : Base1(x, -x) {}
    ~Derived2() override = default;
  };
  {
    std::unique_ptr<AbstractBase> b = std::make_unique<Base1>(1, 7);
    View<AbstractBase> v(std::move(b));
    EXPECT_EQ(v.Get()->Sum(), 8);
  }
  {
    std::unique_ptr<AbstractBase> d_as_b = std::make_unique<Derived2>(631);
    View<AbstractBase> v(std::move(d_as_b));
    EXPECT_EQ(v.Get()->Sum(), 0);
  }
  {
    View<AbstractBase> v(std::make_unique<Base1>(1, 2));
    EXPECT_EQ(v.Get()->Sum(), 3);
  }
  {
    std::unique_ptr<Derived2> d = std::make_unique<Derived2>(437);
    View<AbstractBase> v(std::move(d));
    EXPECT_EQ(v.Get()->Sum(), 0);
  }
  {
    View<AbstractBase> v(std::make_unique<Derived2>(123));
    EXPECT_EQ(v.Get()->Sum(), 0);
  }
  {
    View<AbstractBase> v;
    std::unique_ptr<AbstractBase> b = std::make_unique<Base1>(1, 7);
    v.Update(std::move(b));
    EXPECT_EQ(v.Get()->Sum(), 8);
    std::unique_ptr<AbstractBase> d_as_b = std::make_unique<Derived2>(631);
    v.Update(std::move(d_as_b));
    EXPECT_EQ(v.Get()->Sum(), 0);
    std::unique_ptr<Derived2> d = std::make_unique<Derived2>(437);
    v.Update(std::move(d));
    EXPECT_EQ(v.Get()->Sum(), 0);
    v.Update(std::make_unique<Base1>(8, -5));
    EXPECT_EQ(v.Get()->Sum(), 3);
    v.Update(std::make_unique<Derived2>(321));
    EXPECT_EQ(v.Get()->Sum(), 0);
  }
  {
    // This one's a bit weird because of the extra unique_ptr layer. This line
    // should invoke the in-place constructor, not the normal unique_ptr<T>
    // constructor (which requires unique_ptr<unique_ptr<A>>). It still works
    // because T=unique_ptr<A> is constructable from unique_ptr<B>.
    View<std::unique_ptr<AbstractBase>> v(std::make_unique<Base1>(1, 2));
    EXPECT_EQ(v.Get()->get()->Sum(), 3);
    v.Update(std::make_unique<Derived2>(-50));
    EXPECT_EQ(v.Get()->get()->Sum(), 0);
  }
}

TEST(ViewTest, PointerManipulation) {
  class Foo {
   public:
    explicit Foo(absl::Notification* n) : n_(n) {}
    ~Foo() { n_->Notify(); }

   private:
    absl::Notification* n_;
  };

  View<Foo> rv;

  // simple usage:
  {
    absl::Notification n;
    rv.Update(std::make_unique<Foo>(&n));
    {
      auto h = rv.Get();
    }
    rv.Update(nullptr);
    n.WaitForNotification();
  }
  // Construct new T from args
  {
    absl::Notification n;
    rv.Update(&n);
    {
      auto h = rv.Get();
    }
    rv.Update(nullptr);
    n.WaitForNotification();
  }
  // moves
  {
    absl::Notification n;
    rv.Update(std::make_unique<Foo>(&n));
    {
      auto h = rv.Get();
      auto h2 = std::move(h);
      h = rv.Get();
    }
    rv.Update(nullptr);
    n.WaitForNotification();
  }
  // shares
  // N.B. Converting Snapshot<T> to shared_ptr<T> is an undocumented feature
  // and somewhat likely to be removed; please avoid using this.
  {
    absl::Notification n;
    rv.Update(std::make_unique<Foo>(&n));
    {
      std::shared_ptr<const Foo> h = rv.Get();
      auto h2 = h;
      auto h3 = h;
    }
    rv.Update(nullptr);
    n.WaitForNotification();
    {
      std::shared_ptr<const Foo> h = rv.Get();
      auto h2 = h;
      auto h3 = h;
    }
  }
}

TEST(ViewTest, WriteVariants) {
  View<int> rv1(std::make_unique<int>(0));
  View<int> rv2(0);
  // pointers inside pointers are silly, but it's an easy move-only type
  View<std::unique_ptr<int>> rv3(std::make_unique<int>(0));

  // now updates
  rv1.Update(0);
  rv1.Update(std::make_unique<int>(0));
  rv1.Update(nullptr);
  rv3.Update(std::make_unique<int>(0));

  rv1.Update(0);
  View<int> rv4(nullptr);

  // multi-arg construction
  View<std::string> rv5("foobar", 2);
  EXPECT_EQ("fo", *rv5.Get());
  rv5.Update("asdf", 3);
  EXPECT_EQ("asd", *rv5.Get());
}

TEST(ViewTest, ThreadSafeType) {
  View<KnownThreadSafe<std::atomic<int>>> rv(1);
  EXPECT_EQ(1, rv.Get()->load());
  ++*rv.Get();
  EXPECT_EQ(2, rv.Get()->load());
}

TEST(ViewTest, Cleanup) {
  class Inc {
   public:
    void operator()(std::atomic<intptr_t>* p) {
      p->fetch_add(1, std::memory_order_relaxed);
    }
  };
  typedef std::unique_ptr<std::atomic<intptr_t>, Inc> IncOnDelete;
  View<IncOnDelete> v;
  std::atomic<intptr_t> w{0};
  static const int kReps = 1024;
  {
    auto snap = v.Get();

    for (int i = 0; i < kReps; ++i) {
      auto snap2 = v.Get();
      v.Update(IncOnDelete(&w));
    }
    v.Update(nullptr);
    // drop snapshots here so Cleanup doesn't deadlock.
  }
  rcu::CleanUpAllViews();
  EXPECT_EQ(kReps, w.load(std::memory_order_relaxed));
}

TEST(ViewTest, TryUpdate) {
  View<int> v(1);
  auto s1 = v.Get();

  EXPECT_TRUE(v.TryUpdate(s1, std::make_unique<int>(2)));
  auto s2 = v.Get();
  EXPECT_EQ(2, *s2);

  EXPECT_FALSE(v.TryUpdate(s1, std::make_unique<int>(3)));
  EXPECT_TRUE(v.TryUpdate(s2, std::make_unique<int>(3)));
  auto s3 = v.Get();
  EXPECT_EQ(3, *s3);

  // View should be CASing on pointer values, not contents.
  EXPECT_TRUE(v.TryUpdate(s3, std::make_unique<int>(3)));
  EXPECT_FALSE(v.TryUpdate(s3, std::make_unique<int>(4)));

  // Make sure we don't get screwed up by static analysis in the expected
  // use pattern.
  auto p5 = std::make_unique<int>(5);
  while (true) {
    auto s = v.Get();
    if (v.TryUpdate(s, std::move(p5))) break;
  }
}

TEST(ViewTest, CustomDelete) {
  class Inc {
   public:
    void operator()(std::atomic<int>* p) { p->fetch_add(1); }
  };
  View<std::atomic<int>, Inc> v;
  std::atomic<int> w{0};
  static const int kReps = 1024;
  {
    auto snap = v.Get();

    for (int i = 0; i < kReps; ++i) {
      auto snap2 = v.Get();
      v.Update(std::unique_ptr<std::atomic<int>, Inc>(&w));
      std::swap(snap, snap2);
    }
    v.Update(nullptr);
    // drop snapshots here so Cleanup doesn't deadlock.
  }
  rcu::CleanUpAllViews();
  EXPECT_EQ(kReps, w.load());
}

TEST(ViewTest, SupportsConst) {
  View<const int> v;
  v.Update(5);
  v.Update(6);
  v.Update(nullptr);
}

static void BM_GetAndCheckNull(benchmark::State& state) {
  static auto* rv = new View<int>(std::make_unique<int>());
  for (auto s : state) {
    bool is_null = (rv->Get() == nullptr);
    benchmark::DoNotOptimize(is_null);
  }
}

static void BM_IsNull(benchmark::State& state) {
  static auto* rv = new View<int>(std::make_unique<int>());
  for (auto s : state) {
    bool is_null = rv->IsNull();
    benchmark::DoNotOptimize(is_null);
  }
}

static void BM_GetAndRead(benchmark::State& state) {
  static auto* rv = new View<int>(std::make_unique<int>(1));
  for (auto s : state) {
    auto r = rv->Get();
    int x = *r;
    benchmark::DoNotOptimize(x);
  }
}

static void BM_GetAndUpdate(benchmark::State& state) {
  static auto* rv = new View<int>(std::make_unique<int>(0));
  for (auto s : state) {
    auto r = rv->Get();
    rv->Update(std::make_unique<int>(*r + 1));
    benchmark::DoNotOptimize(*rv);
  }
}

BENCHMARK(BM_GetAndCheckNull)->ThreadRange(1, NumCPUs() - 1);
BENCHMARK(BM_IsNull)->ThreadRange(1, NumCPUs() - 1);
BENCHMARK(BM_GetAndRead)->ThreadRange(1, NumCPUs() - 1);
BENCHMARK(BM_GetAndUpdate)->ThreadRange(1, NumCPUs() - 1);

}  // namespace
}  // namespace rcu
