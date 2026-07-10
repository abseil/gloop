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

#include "gloop/util/functional/from_callback.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "gloop/base/callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

using util::functional::FromCallback;
using util::functional::FromCallbackWithOwnership;
using util::functional::ToCallback;
using util::functional::ToPermanentCallback;

namespace {

void Nop() {}
void Nop1(int unused) {}
int Sum(int a, int b) { return a + b; }

TEST(CallbackToFunctor, Examples) {
  // Example 1
  Closure* a = ::util::functional::ToCallback([] { Nop1(2); });
  ::util::functional::CallbackFunctor<int> b = ToCallback(Nop1);
  ::util::functional::CallbackFunctor<int> c = ToPermanentCallback(Nop1);
  ::util::functional::CallbackFunctor<int> d = ToCallback(Nop1);

  std::function<void()> f_a = FromCallback(a);
  std::function<void(int)> f_b = FromCallback(b);
  std::function<void(int)> f_c = FromCallback(c);
  std::function<void(int)> f_d = FromCallback(d);

  f_a();
  f_b(3);
  f_c(4);
  f_c(5);
}

TEST(CallbackToFunctor, ConvertsToFunction) {
  std::function<void()> a =
      FromCallback(::util::functional::ToCallback<Closure>(Nop));
  std::function<void(int)> b = FromCallback(
      ::util::functional::ToCallback<::util::functional::CallbackFunctor<int>>(
          Nop1));
  std::function<int(int, int)> c = FromCallback(
      ::util::functional::ToCallback<
          util::functional::ResultCallbackFunctor<int, int, int>>(Sum));

  a();
  b(2);
  EXPECT_EQ(19, c(2, 17));
}

TEST(CallbackToFunctor, Copyable) {
  struct AccountCopy {
    explicit AccountCopy(int* c) : count(c) {}
    // NewCallback binding depends on allowing implicit copies.
    AccountCopy(const AccountCopy& other) {  // NOLINT
      count = other.count;
      (*count)++;
    }
    int* count;
  };

  struct Helper {
    static void IncAndCountCopies(AccountCopy unused, int* x) { (*x)++; }
  };

  int x = 0, copies = 0;
  Closure* c = ::util::functional::ToPermanentCallback(
      absl::bind_front(Helper::IncAndCountCopies, AccountCopy(&copies), &x));

  EXPECT_EQ(3, copies);  // Binding should copy once.
  std::function<void()> f = FromCallback(c);
  EXPECT_EQ(3, copies);              // But conversion shouldn't.
  std::function<void()> copy_f = f;  // "f" should be copyable.
  EXPECT_EQ(3, copies);  // And neither should copying the conversion.

  f();
  EXPECT_EQ(1, x);
  copy_f();
  EXPECT_EQ(2, x);  // The copy should also invoke Inc, with the same prebound.

  delete c;
}

enum CallbackType { TEMPORARY, PERMANENT };

struct ObserveDestruction {
  explicit ObserveDestruction(bool* set_on_destroy)
      : s(new SetOnDestroy(set_on_destroy)) {}

  struct SetOnDestroy {
    explicit SetOnDestroy(bool* set_on_destroy) : on_destroy(set_on_destroy) {}
    ~SetOnDestroy() { *on_destroy = true; }

    bool* on_destroy;
  };

  // We need to use a shared_ptr to reference count when this should actually
  // be counted as freed as since NewCallback copies (and the original
  // temporary is destroyed) at time of binding.
  std::shared_ptr<SetOnDestroy> s;
};

// Returns a Callback that will set "*set_on_destroy = true" on destruction.
Closure* NewCallbackReportsDestruction(CallbackType type,
                                       bool* set_on_destroy) {
  struct Helper {
    // By including ObserveDestruction as a pre-bound argument we're able to
    // observe the destruction of the returned callback.
    static void AcceptObservable(ObserveDestruction o) {}
  };

  if (type == PERMANENT) {
    return ::util::functional::ToPermanentCallback(absl::bind_front(
        Helper::AcceptObservable, ObserveDestruction(set_on_destroy)));
  } else {
    return ::util::functional::ToCallback(absl::bind_front(
        Helper::AcceptObservable, ObserveDestruction(set_on_destroy)));
  }
}

TEST(CallbackToFunctor, TemporaryCallbacksFreedByRun) {
  bool freed[2] = {false, false};
  Closure* a = NewCallbackReportsDestruction(TEMPORARY, &freed[0]);
  Closure* b = NewCallbackReportsDestruction(TEMPORARY, &freed[1]);
  EXPECT_FALSE(freed[0]);
  EXPECT_FALSE(freed[1]);
  FromCallback(a)();
  EXPECT_TRUE(freed[0]);
  FromCallbackWithOwnership(std::move(b))();
  EXPECT_TRUE(freed[1]);
}

TEST(CallbackToFunctor, DoesNotTakeOwnership) {
  bool a_destroyed = false, b_destroyed = false;
  Closure* a = NewCallbackReportsDestruction(PERMANENT, &a_destroyed);
  Closure* b = NewCallbackReportsDestruction(TEMPORARY, &b_destroyed);

  {
    auto f1 = FromCallback(a);
    auto f2 ABSL_ATTRIBUTE_UNUSED = FromCallback(b);
    EXPECT_FALSE(a_destroyed);
    EXPECT_FALSE(b_destroyed);
    f1();
    // Don't invoke the temporary callback; we want f2 to go out of scope
    // without it having been run.
  }
  EXPECT_FALSE(a_destroyed);
  EXPECT_FALSE(b_destroyed);

  // Underlying "a, b" should still be extant.
  a->Run();
  b->Run();
  EXPECT_FALSE(a_destroyed);
  EXPECT_TRUE(b_destroyed);  // b is still self deleting.
  delete a;
  EXPECT_TRUE(a_destroyed);
}

TEST(CallbackToFunctor, FromCallbackIsRepeatable) {
  util::functional::ResultCallbackFunctor<int, int, int> sum_callback =
      util::functional::ToPermanentCallback(Sum);
  std::function<int(int, int)> sum = FromCallback(sum_callback);

  int total1 = 0, total2 = 0;
  for (int i = 1; i < 100; i++) {
    total1 = sum(total1, i);
    total2 = (*sum_callback)(total2, i);
    EXPECT_EQ((i * i + i) / 2, total1);
    EXPECT_EQ(total1, total2);
  }
}

static void Nop5(std::string a, float b, int c, int d, int e) {}
static int IntNop5(std::string a, float b, int c, int d, int e) { return 0; }

TEST(CallbackToFunctor, ConvertTemporaryCallbackPreboundAndVariableArgs) {
  ::util::functional::CallbackFunctor<int, int> cb2 =
      ::util::functional::ToCallback(absl::bind_front(Nop5, "", 0.0, 0));
  ::util::functional::CallbackFunctor<int> cb1 =
      ::util::functional::ToCallback(absl::bind_front(Nop5, "", 0.0, 0, 0));
  Closure* cb0 =
      ::util::functional::ToCallback(absl::bind_front(Nop5, "", 0.0, 0, 0, 0));

  FromCallback(cb2)(0, 0);
  FromCallback(cb1)(0);
  FromCallback(cb0)();
}

TEST(CallbackToFunctor, ConvertTemporaryResultCallbackPreboundAndVariableArgs) {
  util::functional::ResultCallbackFunctor<int, int, int> rcb2 =
      ::util::functional::ToCallback(absl::bind_front(IntNop5, "", 0.0, 0));
  ::util::functional::ResultCallbackFunctor<int, int> rcb1 =
      ::util::functional::ToCallback(absl::bind_front(IntNop5, "", 0.0, 0, 0));
  ::util::functional::ResultCallbackFunctor<int> rcb0 =
      ::util::functional::ToCallback(
          absl::bind_front(IntNop5, "", 0.0, 0, 0, 0));

  EXPECT_EQ(0, FromCallback(rcb2)(0, 0));
  EXPECT_EQ(0, FromCallback(rcb1)(0));
  EXPECT_EQ(0, FromCallback(rcb0)());
}

TEST(CallbackToFunctor, ConvertPermanentCallbackPreboundAndVariableArgs) {
  ::util::functional::CallbackFunctor<int, int> cb2 =
      ::util::functional::ToPermanentCallback(
          absl::bind_front(Nop5, "", 0.0, 0));
  ::util::functional::CallbackFunctor<int> cb1 =
      ::util::functional::ToPermanentCallback(
          absl::bind_front(Nop5, "", 0.0, 0, 0));
  Closure* cb0 = ::util::functional::ToPermanentCallback(
      absl::bind_front(Nop5, "", 0.0, 0, 0, 0));

  FromCallback(cb2)(0, 0);
  FromCallback(cb1)(0);
  FromCallback(cb0)();

  delete cb0;
}

TEST(CallbackToFunctor, ConvertPermanentResultCallbackPreboundAndVariableArgs) {
  util::functional::ResultCallbackFunctor<int, int, int> rcb2 =
      ::util::functional::ToPermanentCallback(
          absl::bind_front(IntNop5, "", 0.0, 0));
  ::util::functional::ResultCallbackFunctor<int, int> rcb1 =
      ::util::functional::ToPermanentCallback(
          absl::bind_front(IntNop5, "", 0.0, 0, 0));
  ::util::functional::ResultCallbackFunctor<int> rcb0 =
      ::util::functional::ToPermanentCallback(
          absl::bind_front(IntNop5, "", 0.0, 0, 0, 0));

  EXPECT_EQ(0, FromCallback(rcb2)(0, 0));
  EXPECT_EQ(0, FromCallback(rcb1)(0));
  EXPECT_EQ(0, FromCallback(rcb0)());
}

static int Adds2(int x) { return x + 2; }

TEST(CallbackToFunctor, MultipleConversions) {
  typedef ::util::functional::ResultCallbackFunctor<int, int> ResultCB;
  EXPECT_EQ(7, FromCallback(ToCallback<ResultCB>(Adds2))(5));
  EXPECT_EQ(7, (*ToCallback<ResultCB>(Adds2))(5));
}

TEST(CallbackToFunctor, WithOwnership) {
  auto a = Nop;
  Closure* b = util::functional::ToPermanentCallback(Nop);

  ABSL_ATTRIBUTE_UNUSED auto f1 = FromCallbackWithOwnership(
      std::move(::util::functional::ToCallback<Closure>(a)));
  ABSL_ATTRIBUTE_UNUSED auto f2 = FromCallbackWithOwnership(std::move(b));
  // "a" / "b" should now be owned & deleted by their respective functors.
}

TEST(CallbackToFunctor, OwnedPermanent) {
  auto a = FromCallbackWithOwnership(
      util::functional::ToPermanentCallback<Closure>(Nop));
  a();
  a();  // Should be multiply invokable.
  // Underlying callback should be freed.
}

TEST(CallbackToFunctor, OwnedTemporary) {
  auto a =
      FromCallbackWithOwnership(::util::functional::ToCallback<Closure>(Nop));
  a();
  // "~a" should not release the underlying callback, being temporary and
  // invoked.
}

// Verifies that we can copy an owning functor, and that the callback is
// deleted when the last copy is destroyed.
TEST(CallbackToFunctor, ReferenceCountPermanent) {
  auto a = FromCallbackWithOwnership(
      util::functional::ToPermanentCallback<Closure>(Nop));
  auto b(a);
  a();
  b();
  // Underlying callback should be freed exactly once.
}

// Verifies that we can copy an owning functor, and that (for an uninvoked
// temporary callback) the callback is deleted when the last copy is destroyed.
TEST(CallbackToFunctor, ReferenceCountTemporaryNotInvoked) {
  auto a =
      FromCallbackWithOwnership(::util::functional::ToCallback<Closure>(Nop));
  auto b(a);
  // Underlying callback should be freed exactly once.
}

// Verifies that we can copy an owning functor, and that the temporary
// callback is deleted when run.
TEST(CallbackToFunctor, ReferenceCountTemporaryInvoked) {
  auto a =
      FromCallbackWithOwnership(::util::functional::ToCallback<Closure>(Nop));
  auto b(a);
  b();
  // Underlying callback should be freed exactly once.
}

struct CallbackDerived
    : util::functional::ResultCallbackFunctor<int, int, int>::CallbackType {
  int Run(int a, int b) override { return a + b; }
  bool IsRepeatable() const override { return true; }
};

TEST(CallbackToFunctor, FromCallbackDerived) {
  CallbackDerived a;
  auto f = FromCallback(&a);
  EXPECT_EQ(f(3, 4), 7);
}

struct CallbackDerived2
    : util::functional::ResultCallbackFunctor<void, int, int>::CallbackType {
  void Run(int a, int b) override {}
  bool IsRepeatable() const override { return true; }
};

TEST(CallbackToFunctor, FromCallbackDerived2) {
  CallbackDerived2 a;
  auto f = FromCallback(&a);
  f(3, 4);
}

struct CallbackDerived3 : ::util::functional::ResultCallbackFunctor<
                              bool, const std::pair<int, int>&>::CallbackType {
  bool Run(const std::pair<int, int>& p) override { return p.first > p.second; }
  bool IsRepeatable() const override { return true; }
};

TEST(CallbackToFunctor, FromCallbackConstMethod) {
  CallbackDerived3 a;
  const auto& cfunctor = FromCallback(&a);
  EXPECT_TRUE(cfunctor(std::make_pair(42, 12)));
  EXPECT_FALSE(cfunctor(std::make_pair(12, 42)));
}

TEST(CallbackFunctor, CanBeCalledLikeCallback) {
  util::functional::ResultCallbackFunctor<int, int> cb =
      util::functional::ToCallback([](int n) { return n - 1; });
  EXPECT_EQ((*cb)(42), 41);

  int sum = 0;
  auto cb2 =
      util::functional::ToCallback<util::functional::CallbackFunctor<int, int>>(
          [&](int a, int b) { sum += a + b; });
  cb2->Run(1, 2);
  EXPECT_EQ(sum, 3);
}

TEST(CallbackFunctor, Nullable) {
  absl_nullable util::functional::ResultCallbackFunctor<int, int> cb = nullptr;
  EXPECT_EQ(cb, nullptr);
  EXPECT_FALSE(cb);
}

TEST(CallbackFunctor, CanBeUsedLikeUniquePtrToCallback) {
  util::functional::ResultCallbackFunctor<int, int> cb =
      absl::WrapUnique(util::functional::ToPermanentCallback<
                       ::util::functional::ResultCallbackFunctor<int, int>>(
          [](int n) { return n - 1; }));
  EXPECT_EQ((*cb.get())(42), 41);

  cb = util::functional::ToCallback([&](int n) { return n + 1; });
  EXPECT_EQ((*cb)(42), 43);

  cb = nullptr;
  EXPECT_FALSE(cb);

  cb = nullptr;
  EXPECT_FALSE(cb);
}

TEST(CallbackFunctor, EmptyAnyInvocableIsNullable) {
  absl::AnyInvocable<int(int)> empty_invocable = nullptr;
  util::functional::ResultCallbackFunctor<int, int> cb =
      std::move(empty_invocable);
  EXPECT_EQ(cb, nullptr);
  EXPECT_FALSE(cb);
}

}  // namespace
