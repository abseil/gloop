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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_SELECTOR_SELECTOR_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_SELECTOR_SELECTOR_H_

#include <stddef.h>

#include <tuple>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "absl/utility/utility.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"

namespace thread {

// Selector is an alternative syntactic wrapper for thread::Select.
// It provides a wrapper object that allows (for supported selectables)
// the side-by-side specification of possible cases (Handlers)
// and execution (lambdas) that should occur in response.
// Note that Handlers sometimes provide additional functionality.
// For example, ReadHandlers automatically become non-selectable after
// responding to the underlying Channel closing.
//
// As with thread::Select, it is guaranteed that only a single action
// will be executed and that unevaluated cases have no side-effects.
//
// Selector objects may be re-used, however they are not thread-safe.
// Only a single thread may operate on any instance at a time.
//
// For usage example let's assume we have 2 channels in context:
//
//   thread::Channel<int> int_channel(0);
//   thread::Channel<string> string_channel(0);
//
// Selector usage example:
//
//   bool cancelled = false;
//   auto selector = MakeSelector(
//       ReadHandler<int>(
//           int_channel.reader(),
//           [&](int i) { ... },    // on int_channel read
//           [&] { ... }),          // on int_channel closed
//       ReadHandler<string>(
//           string_channel.reader(),
//           [&](string s) { ... }, // on string_channel read
//           [&] { ... }),          // on string_channel closed
//       CancelHandler([&] { cancelled = true; })
//   );
//
//    while (!cancelled) selector.Execute();
//
// Equivalent example using thread::Select
//
//   thread::Channel<int> int_channel(0);
//   thread::Channel<string> string_channel(0);
//     bool cancelled = false;
//
//     int i;
//     string s;
//     // We must use two separate `ok` bools to avoid races. Selector doesn't
//     // have this problem
//     bool int_read_ok;
//     bool string_read_ok;
//     thread::CaseArray cases = {
//         int_channel.reader()->OnRead(&i, &int_read_ok),        // case 0
//         string_channel.reader()->OnRead(&s, &string_read_ok),  // case 1
//         thread::OnCancel()                                     // case 2
//     };
//     while (!cancelled) {
//       int case_no = thread::Select(cases);
//       switch (case_no) {
//         case 0:  // int_channel read
//           if (int_read_ok) {
//             ... // on int_channel read
//           } else {
//             cases[0] = thread::NonSelectableCase();
//             ... // on int_channel closed
//           }
//           break;
//         case 1:  // string_channel read
//           if (string_read_ok) {
//             ... // on string_channel read
//           } else {
//             cases[1] = thread::NonSelectableCase();
//             ... // on string_channel closed
//           }
//           break;
//         case 2:  // on cancel
//           cancelled = true;
//           break;
//         default:
//           LOG(FATAL) << "Can't happen";
//       }
//     }
//
// These examples can be found in selector_test.cc
// Note: Keep documentation and tests in sync.

template <typename... Handlers>
class Selector;

// Create a Selector with the specified Handlers (see handlers.h) without
// a requirement to explicitly specify template parameter for each Handler.
// Analogue of MakeUnique, std::make_pair, etc.
template <typename... Handlers>
Selector<Handlers...> MakeSelector(Handlers&&... handlers);

// Not thread-safe.
template <typename... Handlers>
class Selector {
 public:
  // Registers all given Handler(s).
  explicit Selector(Handlers&&... handlers);
  Selector(Selector&&) = default;

  // Synchronously waits until either the deadline passes or one of the
  // registered handlers becomes Select-able. If the latter happens, executes
  // its specified action and returns true. Other handlers will have no side
  // effects. Returns false if the deadline expires without an action becoming
  // Select-able. To test whether any handlers are at all selectable pass a
  // deadline of absl::InfinitePast().
  bool ExecuteWithDeadline(absl::Time deadline);

  // Equivalent to ExecuteWithDeadline, except that an arbitrary time-source may
  // be specified.
  bool ExecuteWithDeadline(absl::Clock* clock, absl::Time deadline);

  // Equivalent to ExecuteWithDeadline(absl::InfiniteFuture()).
  void Execute();

 private:
  template <size_t... Indices>
  thread::CaseArray GetCaseArray(absl::integer_sequence<size_t, Indices...>);

  std::tuple<Handlers...> handlers_;
  thread::CaseArray case_array_;
};

// -----------------------------------------------------------------------------
// Implementation details.

template <typename... Handlers>
Selector<Handlers...> MakeSelector(Handlers&&... handlers) {
  return Selector<Handlers...>(std::forward<Handlers>(handlers)...);
}

template <typename... Handlers>
Selector<Handlers...>::Selector(Handlers&&... handlers)
    : handlers_(std::forward<Handlers>(handlers)...),
      case_array_(GetCaseArray(absl::index_sequence_for<Handlers...>())) {}

namespace internal {
// Helper struct for running the handler at the given index of a tuple,
// assuming that 0 <= index < N.
// Sadly, this needs to be a class (not a function) in order to get partial
// template specialization to work properly (i.e., ExecuteIndexHelper<Tuple, 0>
// below).  It also can't be a nested class of Selector.
template <typename Tuple, int N>
struct ExecuteIndexHelper {
  static bool Execute(int index, Tuple* handlers) {
    if (index == N - 1) {
      return std::get<N - 1>(*handlers).Execute();
    } else {
      return ExecuteIndexHelper<Tuple, N - 1>::Execute(index, handlers);
    }
  }
};

template <typename Tuple>
struct ExecuteIndexHelper<Tuple, 0> {
  static bool Execute(int index, Tuple*) {
    LOG(FATAL) << "Should not happen: index=" << index
               << ", tuple_size=" << std::tuple_size<Tuple>::value;
  }
};

// Helper function to run the handler at a specific index of the tuple (chosen
// at runtime).
// Delegates to ExecuteIndexHelper to recursively search for the right index.
template <typename Tuple>
bool ExecuteIndex(int index, Tuple* handlers) {
  constexpr size_t size = std::tuple_size<Tuple>::value;
  return ExecuteIndexHelper<Tuple, size>::Execute(index, handlers);
}
}  // namespace internal

template <typename... Handlers>
bool Selector<Handlers...>::ExecuteWithDeadline(absl::Clock* clock,
                                                absl::Time deadline) {
  const int chosen = thread::SelectUntil(clock, deadline, case_array_);
  if (chosen == -1) {
    return false;
  }
  const bool ok = internal::ExecuteIndex(chosen, &handlers_);
  if (!ok) case_array_[chosen] = thread::NonSelectableCase();
  return true;
}

template <typename... Handlers>
bool Selector<Handlers...>::ExecuteWithDeadline(absl::Time deadline) {
  return ExecuteWithDeadline(&absl::Clock::GetRealClock(), deadline);
}

template <typename... Handlers>
void Selector<Handlers...>::Execute() {
  CHECK(ExecuteWithDeadline(absl::InfiniteFuture()));
}

template <typename... Handlers>
template <size_t... Indices>
thread::CaseArray Selector<Handlers...>::GetCaseArray(
    absl::integer_sequence<size_t, Indices...>) {
  return {std::get<Indices>(handlers_).GetCase()...};
}

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_SELECTOR_SELECTOR_H_
