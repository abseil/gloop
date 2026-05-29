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

// The functions provided in this module allow test code to reach into the guts
// of in-memory production code and:
//
// - Monitor the intermediate value of a variable;
// - Adjust the value of a variable;
// - Invoke an arbitrary callback function provided by the test code.
//
// The most common use for this is to fake special error codes from routines
// invoked by production code. Until explicitly enabled, the test stubs are
// no-ops with minimal overhead in production.
//
// To interact with the guts of remotely executing code, consider using
// <path>
//
// Adjustment sites are looked up by global string labels, allowing easy
// injection even in cases with multiple layers of nested objects.
//
// Tips:
// 1. Use "<namespace>::<identifier>" syntax for labels.
//    This prevents name collisions from the global labels.
// 2. Use compile time constants (public for testing) for labels.
//    This allows them to be traced in Code Search, and allows dead code to be
//    detected. In particular, don't synthesize labels at run time.
// 3. Keep the types involved simple (e.g., value types like integer, Status
//    objects, etc.). Don't try to inject a new thread::Executor for example.
// 4. Prefer testvalue::Force() to testvalue::SetCallback() if you can.
//
// Example of error injection:
//
//   constexpr absl::string_view kSomeLabel = "current_namespace::some_label";
//
//   // Production code whose error handling we want to test
//   void ProductionCode() {
//     util::Status s = DoSomething(...);
//     testing::testvalue::Adjust(kSomeLabel, &s);
//     if (!s.ok()) {
//       ... error handling code ...
//     }
//   }
//
//   // Testing code
//   static void SetUpTestSuite() {
//     testing::testvalue::Enable();
//   }
//   TEST(MyTest, Case) {
//     testing::testvalue::Force(kSomeLabel, error::Aborted(...));
//     ProductionCode();
//     ... check that error handling code ran properly ...
//     testing::testvalue::Clear(kSomeLabel);
//   }
//
// When the production code runs, "s" will be modified to contain an
// aborted status.
//
//
// Example of a callback:
//
// Suppose we wish to introduce a one-second delay at a particular
// point in the non-test code.  First, we modify the non-test code
// to call into "testvalue":
//
//   constexpr absl::string_view kBeforeCall = "current_namespace::before_call";
//
//   void ProductionCode() {
//     testing::testvalue::Adjust<void>(kBeforeCall, nullptr);
//     util::Status s = DoSomething();
//     ...
//   }
//
// The test code associates a callback which sleeps:
//
//   TEST(MyTest, Case) {
//     testing::testvalue::SetCallback<void>(kBeforeCall,
//         [] (void*) { SleepForSeconds(1.0); });
//     ... test code ...
//     testing::testvalue::Clear(kBeforeCall);
//   }

#ifndef THIRD_PARTY_GLOOP_TESTING_PRODUCTION_STUB_TESTVALUE_H_
#define THIRD_PARTY_GLOOP_TESTING_PRODUCTION_STUB_TESTVALUE_H_

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "gloop/util/gtl/typeid.h"

namespace testing {
namespace testvalue {

// -----------------------------
// Interface for production code

// If Enable() has been called, change the contents of "var" if some
// adjustment policy has been supplied for the specified "label" by
// calling either "Force" or "SetCallback".
template <class T>
void Adjust(absl::string_view label, T* var);

// Are testvalues enabled?  Can be used by production code that wants
// to avoid doing expensive work in preparation for an Adjust() call.
// The Adjust() call itself is very fast when testvalues are not
// enabled, so this function should be rarely needed.
inline bool IsEnabled();

// -----------------------------
// Interface for testing code

// Tests have to call this method.  If this method has not been
// called, Adjust() will do nothing, regardless of any Force() or
// SetCallback() calls.
void Enable();

// Future "Adjust(label, var)" calls will set "*var" to "value"
// REQUIRES: Enable() has been called
// REQUIRES: types of the Adjust(label) and Force(label) calls must match.
template <typename T>
void Force(absl::string_view label, T&& value);

// Future Adjust(label, var) calls will invoke cb(var).
//
// Example of how to use this method with an inline lambda:
//   int captured_int = 0;
//   SetCallback<int>("my_value",
//                    [&captured_int](int *v) { captured_int = *v; });
//   DoSomething();
//   EXPECT_GT(captured_int, 0);
//
// REQUIRES: Enable() has been called
// REQUIRES: types of the Adjust(label) and SetCallback(label) calls
// must match.
template <typename T>
void SetCallback(absl::string_view label, std::function<void(T*)> cb);

// Future "Adjust(label, ...)" calls will do nothing.
// Blocks the caller until outstanding Adjust calls, if any, finish.
void Clear(absl::string_view label);

// Scoped version of SetCallback, which clears the testvalue when it goes out of
// scope.
//
// REQUIRES: Enable() has been called
// REQUIRES: types of the Adjust(label) and SetCallback(label) calls
// must match.
template <typename T>
class [[nodiscard]] ScopedSetCallback {
 public:
  ScopedSetCallback(absl::string_view label, std::function<void(T*)> cb)
      : label_(label) {
    SetCallback(label, std::move(cb));
  }

  ~ScopedSetCallback() { Clear(label_); }

  ScopedSetCallback(const ScopedSetCallback&) = delete;
  ScopedSetCallback& operator=(const ScopedSetCallback&) = delete;

 private:
  const std::string label_;
};

// Scoped version of Force, which clears the testvalue when it goes out of
// scope.
//
// REQUIRES: Enable() has been called
// REQUIRES: T must match the type passed to Adjust(label).
class [[nodiscard]] ScopedForce {
 public:
  template <typename T>
  explicit ScopedForce(absl::string_view label, T&& value) : label_(label) {
    Force(label, std::forward<T>(value));
  }

  ~ScopedForce() { Clear(label_); }

  // Not copyable or movable.
  ScopedForce(const ScopedForce&) = delete;
  ScopedForce& operator=(const ScopedForce&) = delete;

 private:
  const std::string label_;
};

// Call Clear() for all registered labels.
void Reset();

// -----------------------------
// Implementation details

extern std::atomic<bool> internal_enable;

inline bool IsEnabled() {
  // No need for synchronization here -- all access to other state used when we
  // return true here is synchronized with a lock.
  return internal_enable.load(std::memory_order_relaxed);
}

extern void InternalAdjust(absl::string_view label, size_t type_id, void* dst);
extern void InternalSetCallback(absl::string_view label, size_t type_id,
                                std::function<void(void*)> run_callback);

// Callback to use for implementation of Force()
template <class T>
void InternalSetter(T src, T* dst) {
  *dst = src;
}

template <class T>
void Adjust(absl::string_view label, T* var) {
  if (IsEnabled()) {
    InternalAdjust(label, gtl::FastTypeId<T>(), var);
  }
}

template <typename T>
void Force(absl::string_view label, const T& value) {
  SetCallback<T>(label, absl::bind_front(&InternalSetter<T>, value));
}

template <typename T>
void Force(absl::string_view label, T&& value) {
  using RawT = std::remove_const_t<std::remove_reference_t<T>>;
  SetCallback<RawT>(
      label, absl::bind_front(&InternalSetter<RawT>, std::forward<T>(value)));
}

template <typename T>
void SetCallback(absl::string_view label, std::function<void(T*)> cb) {
  InternalSetCallback(label, gtl::FastTypeId<T>(),
                      [cb](void* dst) { cb(reinterpret_cast<T*>(dst)); });
}

}  // end namespace testvalue
}  // end namespace testing

#endif  // THIRD_PARTY_GLOOP_TESTING_PRODUCTION_STUB_TESTVALUE_H_
