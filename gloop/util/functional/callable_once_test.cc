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

#include "gloop/util/functional/callable_once.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "gloop/base/callback.h"
#include "gloop/gloop_test.h"
#include "gloop/util/functional/to_callback.h"

namespace util {
namespace functional {
namespace {

void Sink(std::unique_ptr<int> p) { EXPECT_EQ(*p, 42); }

struct AFunctor {
  std::string operator()() & { return "&"; }
  std::string operator()() const& { return "const &"; }
  // The functor will be called thourgh a temporary. So we expect "&&" when
  // wrapped in CallExactlyOnce.
  std::string operator()() && { return "&&"; }
  // The wrapped functor will always resolve to the non-const version, even
  // through its const interface. This is fine, since no more than one call is
  // allowed and thus, we do not introduce any race in the original functor.
  std::string operator()() const&& { return "const &&"; }
};

// A type that can be copied only once. This ensures that CallAtMostOnce does
// not induce extra copies.  It may be moved multiple times.
class CopyableOnce {
 public:
  CopyableOnce() : copied_(false) {}
  CopyableOnce(const CopyableOnce& other) : copied_(true) {
    CHECK(!other.copied_);
    // We also can copy only once from a CopyableOnce.
    other.copied_ = true;
  }
  CopyableOnce(CopyableOnce&& arg) : copied_(arg.copied_) {}

 private:
  mutable bool copied_{false};
};

void TakeCopyableOnce(const CopyableOnce& once, int i) {}

TEST(CallAtMostOnceTest, Binds) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallAtMostOnce(absl::bind_front(Sink, std::move(p)));
  f();
}

TEST(CallAtMostOnceDeathTest, CannotBeCalledTwice) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallAtMostOnce(absl::bind_front(Sink, std::move(p)));
  f();
  GLOOP_EXPECT_DEATH_IF_SUPPORTED(f(), "already called");
}

TEST(CallAtMostOnceTest, FromMovable) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink, std::move(p));
  auto f2 = CallAtMostOnce(std::move(f));
  f2();
}

TEST(CallAtMostOnceTest, FromCopyable) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink);
  auto f2 = CallAtMostOnce(f);
  f2(std::move(p));
}

TEST(CallAtMostOnceTest, FromCopyableOnce) {
  CopyableOnce copyable_once;
  auto f = CallAtMostOnce(
      absl::bind_front(TakeCopyableOnce, std::move(copyable_once)));
  f(3);
}

TEST(CallAtMostOnceTest, DirectlyInFunction) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  std::function<void()> f(CallAtMostOnce(absl::bind_front(Sink, std::move(p))));
  f();
}

TEST(CallAtMostOnceTest, CopiedInFunction) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallAtMostOnce(absl::bind_front(Sink, std::move(p)));
  std::function<void()> f2(f);
  f2();
}

TEST(CallAtMostOnceTest, MovedInFunction) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallAtMostOnce(absl::bind_front(Sink, std::move(p)));
  std::function<void()> f2(std::move(f));
  f2();
}

TEST(CallAtMostOnceTest, CanMove) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink, std::move(p));
  auto f2 = CallAtMostOnce(std::move(f));
  auto f3 = std::move(f2);
  f3();
}

TEST(CallAtMostOnceTest, CanMoveAssign) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink, std::move(p));
  auto f2 = CallAtMostOnce(std::move(f));
  auto f3 = std::move(f2);
  f3();
  std::unique_ptr<int> q = std::make_unique<int>(42);
  f = absl::bind_front(Sink, std::move(q));
  f2 = CallAtMostOnce(std::move(f));
  f2();
}

TEST(CallAtMostOnceDeathTest, CanBeCopiedButItIsShared) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallAtMostOnce(absl::bind_front(Sink, std::move(p)));
  auto f2 = f;
  f();
  GLOOP_EXPECT_DEATH_IF_SUPPORTED(f2(), "already called");
}

TEST(CallAtMostOnceTest, ConstNoConst) {
  auto f = CallAtMostOnce(AFunctor());
  EXPECT_EQ(f(), "&&");
  auto f2 = CallAtMostOnce(AFunctor());
  const auto& f3 = f2;
  EXPECT_EQ(f3(), "&&");  // It is safe to call the non-const version, since it
                          // is only called once. No race is introduced.
}

TEST(CallExactlyOnceTest, Binds) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
  f();
}

TEST(CallExactlyOnceDeathTest, CannotBeCalledTwice) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
  f();
  GLOOP_EXPECT_DEATH_IF_SUPPORTED(f(), "already called");
}

void NeverCalled() {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
}

TEST(CallExactlyOnceDeathTest, NeverCalled) {
  GLOOP_EXPECT_DEATH_IF_SUPPORTED(NeverCalled(), "never called");
}

TEST(CallExactlyOnceTest, FromMovable) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink, std::move(p));
  auto f2 = CallExactlyOnce(std::move(f));
  f2();
}

TEST(CallExactlyOnceTest, FromCopyable) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink);
  auto f2 = CallExactlyOnce(f);
  f2(std::move(p));
}

TEST(CallExactlyOnceTest, FromCopyableOnce) {
  CopyableOnce copyable_once;
  auto f = CallExactlyOnce(TakeCopyableOnce, copyable_once);
  f(3);
}

TEST(CallExactlyOnceTest, DirectlyInFunction) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  std::function<void()> f(CallExactlyOnce(Sink, std::move(p)));
  f();
}

TEST(CallExactlyOnceTest, CopiedInFunction) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
  std::function<void()> f2(f);
  f2();
}

TEST(CallExactlyOnceTest, MovedInFunction) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
  std::function<void()> f2(std::move(f));
  f2();
}

TEST(CallExactlyOnceTest, CanMove) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink, std::move(p));
  auto f2 = CallExactlyOnce(std::move(f));
  auto f3 = std::move(f2);
  f3();
}

TEST(CallExactlyOnceTest, CanMoveAssign) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = absl::bind_front(Sink, std::move(p));
  auto f2 = CallExactlyOnce(std::move(f));
  auto f3 = std::move(f2);
  f3();
  std::unique_ptr<int> q = std::make_unique<int>(42);
  f = absl::bind_front(Sink, std::move(q));
  f2 = CallExactlyOnce(std::move(f));
  f2();
}

TEST(CallExactlyOnceDeathTest, CanBeCopiedButItIsShared) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
  auto f2 = f;
  f();
  GLOOP_EXPECT_DEATH_IF_SUPPORTED(f2(), "already called");
}

TEST(CallExactlyOnceTest, CanBeCopiedButItIsSharedSoOnlyOneCall) {
  std::unique_ptr<int> p = std::make_unique<int>(42);
  auto f = CallExactlyOnce(Sink, std::move(p));
  auto f2 = f;
  auto f3 = f2;
  f3();
}

TEST(CallExactlyOnceTest, ConstNoConst) {
  auto f = CallExactlyOnce(AFunctor());
  EXPECT_EQ(f(), "&&");
  auto f2 = CallExactlyOnce(AFunctor());
  const auto& f3 = f2;
  EXPECT_EQ(f3(), "&&");  // It is safe to call the non-const version, since it
                          // is only called once. No race is introduced.
}

class DestructorTattletale {
 public:
  explicit DestructorTattletale(bool* destroyed) : destroyed_(*destroyed) {}
  ~DestructorTattletale() { destroyed_ = true; }

 private:
  bool& destroyed_;
};

void CallMeMaybe(std::unique_ptr<DestructorTattletale> arg) {}

TEST(CallExactlyOnceTest, ArgsDestroyIfNeverCalled) {
  bool dtor_called = false;
  auto arg = std::make_unique<DestructorTattletale>(&dtor_called);
  {
    std::function<void()> fn = util::functional::CallAtMostOnce(
        absl::bind_front(CallMeMaybe, std::move(arg)));
    (void)fn;
  }
  EXPECT_TRUE(dtor_called);
}

TEST(CallExactlyOnceTest, ArgsDestroyAfterMove) {
  bool dtor_called = false;
  auto arg = std::make_unique<DestructorTattletale>(&dtor_called);
  std::function<void()> fn = util::functional::CallAtMostOnce(
      absl::bind_front(CallMeMaybe, std::move(arg)));
  {
    std::function<void()> inner = std::move(fn);
    fn = nullptr;  // Ensure fn's moved-from state is empty.
    (void)inner;
  }
  EXPECT_TRUE(dtor_called);
}

void DoNothing(std::string arg) { CHECK(!arg.empty()); }

// The following benchmarks show the overheads associated with the functor
// itself. If it is possible to use CallAtMostOnce in order to avoid a copy,
// then that can clearly be a huge performance win.
const char kTestStr[] = "this is longer than 16 characters so it is not inline";
static void BM_ToCallback(benchmark::State& state) {
  std::string test_string(kTestStr);
  for (auto _ : state) {
    Closure* cb = ToCallback(absl::bind_front(DoNothing, test_string));
    cb->Run();
  }
}
BENCHMARK(BM_ToCallback);

static void BM_Function(benchmark::State& state) {
  std::string test_string(kTestStr);
  for (auto _ : state) {
    std::function<void()> f = [test_string]() { DoNothing(test_string); };
    f();
  }
}
BENCHMARK(BM_Function);

static void BM_FunctionMove(benchmark::State& state) {
  std::string test_string(kTestStr);
  for (auto _ : state) {
    std::function<void()> f = [test_string]() mutable {
      DoNothing(std::move(test_string));
    };
    f();
  }
}
BENCHMARK(BM_FunctionMove);

static void BM_FunctionCallAtMostOnce(benchmark::State& state) {
  std::string test_string(kTestStr);
  for (auto _ : state) {
    std::function<void()> f =
        CallAtMostOnce(absl::bind_front(DoNothing, test_string));
    f();
  }
}
BENCHMARK(BM_FunctionCallAtMostOnce);

static void BM_FunctionCallExactlyOnce(benchmark::State& state) {
  std::string test_string(kTestStr);
  for (auto _ : state) {
    std::function<void()> f = CallExactlyOnce(DoNothing, test_string);
    f();
  }
}
BENCHMARK(BM_FunctionCallExactlyOnce);

}  // namespace
}  // namespace functional
}  // namespace util
