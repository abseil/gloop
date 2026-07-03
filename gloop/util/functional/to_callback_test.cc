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

#include "gloop/util/functional/to_callback.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gloop/util/functional/from_callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using std::placeholders::_1;

using testing::ElementsAre;
using util::functional::ToCallback;
using util::functional::ToPermanentCallback;

namespace {
// When updating examples in to_callback.h, please ensure edits are
// replicated here.
namespace examples {

void Nop() {}
void Nop1(int unused) {}
int Sum(int a, int b) { return a + b; }

TEST(FunctorToCallback, Examples) {
  Closure* a = ToCallback(Nop);
  ::util::functional::CallbackFunctor<int> b = ToCallback(Nop1);
  Closure* c = ToCallback(std::bind(Nop));
  Closure* d = ToCallback(std::bind(Nop1, 42));
  ::util::functional::CallbackFunctor<int> e = ToCallback(std::bind(Nop1, _1));
  struct M {
    void member_nop() {}
  };
  M m;
  Closure* f = ToCallback(std::bind(&M::member_nop, m));
  ::util::functional::ResultCallbackFunctor<int, int> g =
      ToCallback(std::bind(Sum, 5, _1));
  util::functional::ResultCallbackFunctor<int, int, int> h = ToCallback(Sum);
  struct F {
    int operator()() { return 1; }
  };
  ::util::functional::ResultCallbackFunctor<int> i = ToCallback(F());
  ::util::functional::CallbackFunctor<std::string> j =
      ToCallback([](std::string x) { LOG(INFO) << x; });
  Closure* k = ToCallback(std::function<void()>());
  void (*null_function_ptr)() = nullptr;
  Closure* l = ToCallback(null_function_ptr);

  // Make sure examples actually run; this also handles cleanup as the above
  // were all created as temporary callbacks.
  a->Run();                  // Nop()
  (*b)(1);                   // Nop1(1)
  c->Run();                  // Nop()
  d->Run();                  // Nop1(42)
  (*e)(1);                   // Nop1(1)
  f->Run();                  // M::member_nop()
  EXPECT_EQ(7, (*g)(2));     // Sum(5, 2)
  EXPECT_EQ(5, (*h)(3, 2));  // Sum(3, 2)
  EXPECT_EQ(1, (*i)());      // (F())()
  (*j)("Example 3");         // LOG(INFO) << "Example 3";
  EXPECT_EQ(k, nullptr);     // Empty std::function
  EXPECT_EQ(l, nullptr);     // Null function pointer
}

}  // namespace examples

// Tests below adapted from ToCallback.
int32_t Inc(int32_t val) { return val + 1; }
int32_t One() { return 1; }

std::string GetTraceStatus() { return base::CurrentContext().thread_status(); }

TEST(FunctorToCallback, Closure) {
  Closure* c = ToCallback([] {});
  c->Run();
  std::unique_ptr<Closure> p(ToPermanentCallback([] {}));
  p->Run();
}

TEST(FunctorToCallback, ResultCallback) {
  ::util::functional::ResultCallbackFunctor<int32_t> c = ToCallback(One);
  EXPECT_EQ(1, (*c)());
  ::util::functional::ResultCallbackFunctor<int32_t> p(
      ToPermanentCallback(One));
  EXPECT_EQ(1, (*p)());
}

TEST(FunctorToCallback, ResultCallback1) {
  ::util::functional::ResultCallbackFunctor<int32_t, int32_t> c =
      ToCallback(Inc);
  EXPECT_EQ(1, (*c)(0));
  ::util::functional::ResultCallbackFunctor<int32_t, int32_t> p(
      ToPermanentCallback(Inc));
  EXPECT_EQ(1, (*p)(0));
}

TEST(FunctorToCallback, Binding) {
  ::util::functional::ResultCallbackFunctor<int32_t> c =
      ToCallback(std::bind(Inc, 2));
  EXPECT_EQ(3, (*c)());
  ::util::functional::ResultCallbackFunctor<int32_t> p(
      ToPermanentCallback(std::bind(Inc, 2)));
  EXPECT_EQ(3, (*p)());
}

TEST(FunctorToCallback, ExplicitConversion) {
  ToCallback<Closure>([] {})->Run();
  std::unique_ptr<Closure>(ToPermanentCallback<Closure>([] {}))->Run();
}

TEST(FunctorToCallback, ResultTypeConversion) {
  ::util::functional::ResultCallbackFunctor<int64_t> c = ToCallback(One);
  EXPECT_EQ(1, (*c)());
}

TEST(FunctorToCallback, ArgumentTypeConversion) {
  ::util::functional::ResultCallbackFunctor<int32_t, int64_t> c =
      ToCallback(Inc);
  EXPECT_EQ(1, (*c)(0));
}

TEST(FunctorToCallback, FunctorObject) {
  struct FunctorTwo {
    int32_t operator()() const { return 2; }
  };

  ::util::functional::ResultCallbackFunctor<int32_t> c =
      ToCallback(FunctorTwo());
  EXPECT_EQ(2, (*c)());
}

TEST(FunctorToCallback, PassesMoveOnlyArguments) {
  struct Helper {
    static int TakesUnique(std::unique_ptr<int> v) { return *v; }
  };
  ::util::functional::ResultCallbackFunctor<int, std::unique_ptr<int>> c =
      ToCallback(std::bind(Helper::TakesUnique, _1));
  EXPECT_EQ(1, (*c)(std::unique_ptr<int>(new int(1))));
}

::util::functional::ResultCallbackFunctor<int32_t> Identity(
    ::util::functional::ResultCallbackFunctor<int32_t> c) {
  return c;
}

::util::functional::ResultCallbackFunctor<int32_t, int32_t> Identity(
    ::util::functional::ResultCallbackFunctor<int32_t, int32_t> c) {
  return c;
}

TEST(FunctorToCallback, InvalidConversions) {
  EXPECT_EQ(1, (*Identity(ToCallback(One)))());
  EXPECT_EQ(1, (*Identity(ToCallback(Inc)))(0));
}

TEST(FunctorToCallback, CopyOnlyFunctor) {
  // Copyable, but not movable.
  struct CopyOnlyFunctor {
    CopyOnlyFunctor() {}
    CopyOnlyFunctor(const CopyOnlyFunctor&) {}
    int operator()() { return 1; }
  };

  ::util::functional::ResultCallbackFunctor<int> c =
      ToCallback(CopyOnlyFunctor());
  EXPECT_EQ(1, (*c)());
  c = ToCallback(std::bind(CopyOnlyFunctor()));
  EXPECT_EQ(1, (*c)());
  CopyOnlyFunctor f;
  c = ToCallback(std::bind(f));
  EXPECT_EQ(1, (*c)());
}

TEST(FunctorToCallback, MoveOnlyFunctor) {
  // Movable, but not copyable.
  struct MoveOnlyFunctor {
    MoveOnlyFunctor() {}
    MoveOnlyFunctor(MoveOnlyFunctor&&) = default;
    MoveOnlyFunctor& operator=(MoveOnlyFunctor&&) = default;
    int operator()() const { return 1; }
  };

  ::util::functional::ResultCallbackFunctor<int> c =
      ToCallback(MoveOnlyFunctor());
  EXPECT_EQ(1, (*c)());
}

// SUBTLE: C++ allows operator()() to distinguish between being invoked on a
// temporary and an lvalue.  This should only be used to optimize the
// construction of the result.
//
// Interaction with ToCallback/ToPermanentCallback:
// The functor bound into a temporary callback is itself treated like a
// temporary, allowing this effect to be potentially observed (if a functor
// chooses to taken advantage of it).  Permanent callbacks will never treat
// their bound functor as an rvalue.
TEST(FunctorToCallback, LvalueRvalueInvocation) {
  // Only a functor when referenced as an lvalue.  May only be bound into a
  // permanent callback.
  struct LvalueOnlyFunctor {
    std::string operator()() & { return "cat"; }
  };
  const LvalueOnlyFunctor lvf = {};
  ::util::functional::ResultCallbackFunctor<std::string> pcb_lvf1(
      ToPermanentCallback(lvf));
  EXPECT_EQ("cat", (*pcb_lvf1)());
  // Note that we can still construct the actual callback with a temporary.  It
  // is copied to an lvalue within the returned callback.
  ::util::functional::ResultCallbackFunctor<std::string> pcb_lvf2(
      ToPermanentCallback(LvalueOnlyFunctor()));
  EXPECT_EQ("cat", (*pcb_lvf2)());

  // Only a functor when referenced as an rvalue.  May only be bound into a
  // temporary callback.  Here, we take advantage of this to encapsulate data
  // directly into the functor and move it into the result.
  struct RvalueIsFunctor {
    std::string operator()() && { return std::move(pet); }
    std::string pet = "dog";
  };
  ::util::functional::ResultCallbackFunctor<std::string> cb_rvf1 =
      ToCallback(RvalueIsFunctor());
  EXPECT_EQ("dog", (*cb_rvf1)());
  const RvalueIsFunctor rvf = {};  // Not invokable.
  ::util::functional::ResultCallbackFunctor<std::string> cb_rvf2 =
      ToCallback(rvf);
  // As above, even though "cb_rvf2" was constructed with an lvalue, we invoke
  // against the temporary copy embedded in the returned callback.
  EXPECT_EQ("dog", (*cb_rvf2)());
}

TEST(FunctorToCallback, LvalueFunctor) {
  struct Functor {
   public:
    ~Functor() { value_ = 24; }
    void operator()() const { EXPECT_EQ(42, value_); }

   private:
    int value_ = 42;
  };

  Closure* cb = nullptr;
  {
    Functor f;
    cb = ToCallback(f);
  }
  cb->Run();
}

TEST(FunctorToCallback, ForwardByValue) {
  struct Helper {
    static void F(std::vector<int> v) { v.push_back(4); }  // Called by value.
  };

  ::util::functional::CallbackFunctor<std::vector<int>&> cb =
      ToCallback(Helper::F);
  std::vector<int> v = {1, 2, 3};
  (*cb)(v);
  EXPECT_THAT(v, ElementsAre(1, 2, 3));
}

TEST(FunctorToCallback, ConstFunctorWithNonConstCallOperator) {
  struct Functor {
    void operator()() {}
  };
  const Functor f = {};
  Closure* cb = ToCallback(f);
  cb->Run();
}

TEST(FunctorToCallback, TraceContext) {
  base::WithThreadStatus w1("foo");
  ::util::functional::ResultCallbackFunctor<std::string> c =
      ToCallback(GetTraceStatus);
  ::util::functional::ResultCallbackFunctor<std::string> p(
      ToPermanentCallback(GetTraceStatus));
  base::WithThreadStatus w2("bar");
  EXPECT_EQ("foo", (*c)());
  EXPECT_EQ("bar", GetTraceStatus());
  EXPECT_EQ("bar", (*p)());
}

TEST(FunctorToCallback, ConvertibleAmbiguities) {
  // Test that FunctorCallbackBinder's conversion operator
  // correctly resolves overloads via substitution failure.
  struct X {
    static void Fn() {}
    static void Fn(int) {}
    static void Fn(void*) {}
    static void Fn(::util::functional::ResultCallbackFunctor<int> cb) { ; }
    static void Fn(::util::functional::ResultCallbackFunctor<int, int> cb) { ; }
  };
  // Fn argument convertible to a
  // ::util::functional::ResultCallbackFunctor<int>.
  X::Fn(ToPermanentCallback([] { return 123; }));
  // Fn argument convertible to a
  // ::util::functional::ResultCallbackFunctor<int,int>.
  X::Fn(ToPermanentCallback([](int x) { return 123 + x; }));
}

TEST(FunctorToCallback, EmptyFunctors) {
  // Empty std::functions should be converted to null.
  std::function<void()> empty_function;
  Closure* empty_closure = ToPermanentCallback(empty_function);
  ASSERT_EQ(nullptr, empty_closure);

  // Null function pointers should be converted to null.
  void (*function_ptr)() = nullptr;
  Closure* empty_closure2 = ToCallback(function_ptr);
  ASSERT_EQ(nullptr, empty_closure2);

  // Non-null function pointers are functors and should convert.
  function_ptr = examples::Nop;
  Closure* non_empty = ToCallback(function_ptr);
  ASSERT_NE(nullptr, non_empty);
  non_empty->Run();

  // Empty move-only absl::AnyInvocable should be converted to null.
  absl::AnyInvocable<bool(int, void*)> empty_invocable = nullptr;
  ::util::functional::ResultCallbackFunctor<bool, int, void*> cb =
      ::util::functional::ToCallback(std::move(empty_invocable));
  EXPECT_TRUE(cb == nullptr);

  absl::AnyInvocable<void()> empty_void_invocable = nullptr;
  Closure* cb_void =
      ::util::functional::ToCallback(std::move(empty_void_invocable));
  EXPECT_TRUE(cb_void == nullptr);
}

TEST(FunctorToCallback, ResultCallbackVoid) {
  ::util::functional::ResultCallbackFunctor<void> c =
      ToPermanentCallback([] {});
}

}  // namespace
