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

// The `PERFTOOLS_VERIFY(cond)` and `PERFTOOLS_VERIFY_<op>(lhs, rhs)` macros
// defined in this header are similar to Abseil's CHECK macros. They verify that
// the required condition holds, and if not, print the failed condition, values
// and the current stack trace. The main difference is that under NDEBUG the
// application does log the failure and stack trace, but does not abort.
//
// The main use case for these macros are to report on broken in-variants that
// we should survive in production, but that we want to fail in TEST and loudly
// log in production. The macros return the true/false result of the condition
// which allows the code to take an alternative path on failures, e.g.:
//
//   void Foo::ShallNeverCrash(Bar* bar) {
//     if (PERFTOOLS_VERIFY(bar != nullptr)) {
//       PERFTOOLS_VERIFY_GT(bar->count, 0);
//       count_ += bar->count;
//     }
//   }
//
// We obviously could implement this using abseil DFATAL macros, but that
// requires us to explicitly add StackTrace() calls, and printing the output
// of the <op> values. For comparison, the same code using abseil macros:
//
//   void Foo::ShallNeverCrash(Bar * bar) {
//     if (bar != nullptr) {
//       LOG_IF_EVERY_N_SEC(DFATAL, bar->count > 0, 60)
//           << "bar->count > 0 (" << bar->count << " vs 0)"
//           << util::CurrentStackTrace();
//       count_ += bar->count;
//     } else {
//       LOG_EVERY_N_SEC(DFATAL)
//           << "bar != nullptr @" << util::CurrentStackTrace();
//     }
//   }
//
// The macros will evaluate the provided arguments exactly once.
//
#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_PERFTOOLS_VERIFY_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_PERFTOOLS_VERIFY_H_

#include "absl/log/log.h"              // IWYU pragma: keep
#include "gloop/base/examine_stack.h"  // IWYU pragma: keep

#define PERFTOOLS_VERIFY(cond)                             \
  [](bool PERFTOOLS_VERIFY_cond) {                         \
    LOG_IF_EVERY_N_SEC(DFATAL, !PERFTOOLS_VERIFY_cond, 60) \
        << "Condition failed: " #cond << " at "            \
        << ::base::CurrentStackTrace();                    \
    return PERFTOOLS_VERIFY_cond;                          \
  }(cond)

#define PERFTOOLS_VERIFY_OP(lhs, op, rhs)                                      \
  [](const auto& PERFTOOLS_VERIFY_lhs, const auto& PERFTOOLS_VERIFY_rhs) {     \
    bool PERFTOOLS_VERIFY_cond = PERFTOOLS_VERIFY_lhs op PERFTOOLS_VERIFY_rhs; \
    LOG_IF_EVERY_N_SEC(DFATAL, !PERFTOOLS_VERIFY_cond, 60)                     \
        << "Condition failed: " << #lhs " " << #op << " " << #rhs << " ("      \
        << PERFTOOLS_VERIFY_lhs << " vs " << PERFTOOLS_VERIFY_rhs << ") at "   \
        << ::base::CurrentStackTrace();                                        \
    return PERFTOOLS_VERIFY_cond;                                              \
  }((lhs), (rhs))

#define PERFTOOLS_VERIFY_EQ(l, r) PERFTOOLS_VERIFY_OP(l, ==, r)
#define PERFTOOLS_VERIFY_NE(l, r) PERFTOOLS_VERIFY_OP(l, !=, r)
#define PERFTOOLS_VERIFY_LT(l, r) PERFTOOLS_VERIFY_OP(l, <, r)
#define PERFTOOLS_VERIFY_LE(l, r) PERFTOOLS_VERIFY_OP(l, <=, r)
#define PERFTOOLS_VERIFY_GT(l, r) PERFTOOLS_VERIFY_OP(l, >, r)
#define PERFTOOLS_VERIFY_GE(l, r) PERFTOOLS_VERIFY_OP(l, >=, r)

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_PERFTOOLS_VERIFY_H_
