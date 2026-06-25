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

#include "gloop/thread/fiber/select.h"

#include <cstdint>

#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/fixed_array.h"
#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "gloop/concurrent/percpu/object.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/tracing.h"
#include "gloop/util/random/shared_bit_gen.h"

namespace thread {

// Pseudo-random number generator using Linear Shift Feedback Register (LSFB)
static uint32_t Rand32() {
  // Primitive polynomial: x^32+x^22+x^2+x^1+1
  static const uint32_t poly = (1 << 22) | (1 << 2) | (1 << 1) | (1 << 0);
  static absl::NoDestructor<concurrent::percpu::PerCpu<int32_t>> last_rand32;

  auto ptr = last_rand32->get();  // lock is held until `ptr` goes out of scope.
  uint32_t r = *ptr;
  if (ABSL_PREDICT_FALSE(r == 0)) {
    r = absl::Uniform<uint32_t>(util_random::SharedBitGen());
    // Avoid 0 which generates a sequence of 0s.
    if (r == 0) r = 1;
  }
  r = (r << 1) ^
      ((static_cast<int32_t>(r) >> 31) & poly);  // shift sign-extends
  *ptr = r;
  return r;
}

// Block until a case has been picked or the deadline passes. If deadline is
// a nullptr, block until a case has been picked. Returns whether a case
// was picked before the deadline passed.
//
// This blocking is handled using sel's internal condition variable.
// Note that we always use this path when the Select call is non-expiring.
inline bool CvBlock(absl::Time deadline, internal::Selector* sel)
    ABSL_SHARED_LOCKS_REQUIRED(sel->mu) {
  // We must first check that no notification occurred between registration
  // with Handle and reaching here.
  while (sel->picked == internal::Selector::kNonePicked) {
    // Note that implementations of WaitGeneric below are allowed to return
    // true (indicating timeout) when racing with Signal().  To handle this we
    // re-check against sel.picked before returning expiring_index.
    if (sel->cv.WaitWithDeadline(&sel->mu, deadline) &&
        sel->picked == internal::Selector::kNonePicked) {
      return false;
    }
  }
  return true;
}

// Block until a case has been picked or a deadline has expired, BUT do this
// with respect to a absl::Clock rather than the actual time. Return whether
// a case was picked before the deadline expired.
//
// This is possibly slower than CvBlock. Reasons why this might be a bit slower
// than CvBlock:
//
// * We aren't using the CondVar, which has explicit signaling when the
//   status of the condition changes. Instead, we're using the Mutex::Await
//   path.
// * absl::Clock is virtual, so there's the obvious virtual dispatch overhead.
//   On top of that, it might be doing creative things to implement
//   AwaitWithDeadline, especially if it is not the real clock -- but that
//   is also part of the point of using it.
inline bool ClockBlock(absl::Clock* clock, absl::Time deadline,
                       internal::Selector* sel)
    ABSL_SHARED_LOCKS_REQUIRED(sel->mu) {
  DCHECK_NE(clock, &absl::Clock::GetRealClock())
      << "If you're using the real clock, just use CvBlock.";
  absl::Condition something_picked(
      +[](int* i) { return *i != internal::Selector::kNonePicked; },
      &sel->picked);
  return clock->AwaitWithDeadline(&sel->mu, something_picked, deadline);
}

int SelectUntil(absl::Clock* clock, absl::Time deadline, const CaseArray& cases,
                perftools::tracing::StringRef name) {
  internal::Selector sel;
  sel.picked = internal::Selector::kNonePicked;
  int num_cases = cases.size();

  // If traced, record the possibly blocking wait on the specified event(s)
  // If we wait for exactly one event then record the event identity as well.
  perftools::tracing::TraceScopedWait trace_wait(
      num_cases == 1 ? cases[0].event : nullptr, name);

  // Initialize internal representation of passed Cases
  absl::FixedArray<internal::CaseState, 4> case_states(num_cases);

  // Use inside-out Fisher-Yates shuffle to combine initialization and
  // permutation.
  if (num_cases > 0) {
    case_states[0].index = 0;
  }
  for (int i = 1; i < num_cases; i++) {
    int swap = Rand32() % (i + 1);
    case_states[i].index = case_states[swap].index;
    case_states[swap].index = i;
  }

  bool blocking = deadline != absl::InfinitePast();
  bool ready = false;
  int registered_limit;
  for (registered_limit = 0; registered_limit < num_cases; registered_limit++) {
    internal::CaseState* case_state = &case_states[registered_limit];
    const Case* assoc_case = &cases[case_state->index];
    case_state->params = assoc_case;
    case_state->prev = nullptr;  // Not on any list.
    case_state->sel = &sel;
    if (assoc_case->event->Handle(case_state, /*enqueue=*/blocking)) {
      ready = true;
      break;
    }
  }

  if (!blocking) {
    // Do not wait.  Also, no need to Unregister() any cases since
    // we passed enqueue=false to each Handle() above.
    const int picked = ready ? sel.picked : -1;
    trace_wait.SetBarrierId(picked >= 0 ? cases[picked].event : nullptr);
    return picked;
  }

  if (!ready) {
    const bool expirable = deadline != absl::InfiniteFuture();
    const bool use_real_time = !clock || clock == &absl::Clock::GetRealClock();
    const bool condvar_block = !expirable || use_real_time;

    absl::MutexLock l(sel.mu);
    bool expired;
    if (ABSL_PREDICT_TRUE(condvar_block)) {
      expired = !CvBlock(deadline, &sel);
    } else {
      expired = !ClockBlock(clock, deadline, &sel);
    }
    DCHECK(expirable || !expired);
    if (expired) {
      // Deadline expiry. Ensure nothing is picked from this point.
      sel.picked = num_cases;
    }
  }

  // Unregister from all events with which we are registered.  We know that
  // there was no non-blocking index and that we attempted to enqueue against
  // all cases with index smaller than registered_limit.
  for (int i = 0; i < registered_limit; i++) {
    internal::CaseState* case_state = &case_states[i];
    if (case_state->index != sel.picked) {
      // sel.picked was unregistered by the notifier.
      case_state->params->event->Unregister(case_state);
    }
  }

  // sel.picked == num_cases denotes expiry
  const int picked = sel.picked < num_cases ? sel.picked : -1;
  trace_wait.SetBarrierId(picked >= 0 ? cases[picked].event : nullptr);
  return picked;
}

}  // namespace thread
