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

// Provides a generic test case for implementations of the Domain interface.
//
// Specific implementations may be tested by passing an appropriate
// (*NewDomainFunction)(...) as a parameter.  E.g.:
//   INSTANTIATE_TEST_CASE_P(DomainImplementation, DomainTest,
//                           ::testing::Values(<NewDomainFunction>));
//
// For a full example see:
//   https://github.com/abseil/gloop/tree/main/gloop/thread/fiber/pthread-domain_test.cc
//
// The Domain interface is defined at:
//   https://github.com/abseil/gloop/tree/main/gloop/base/scheduling/domain.h

#ifndef THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_TEST_H_
#define THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_TEST_H_

#include <tuple>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/base/callback.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gtest/gtest.h"

namespace base {
namespace scheduling {

// Must return a new instance of the domain to be tested.
typedef Domain* (*NewDomainFunction)(absl::string_view name_prefix,
                                     int concurrency);

class AtomicCompletion;

// Test parameters:
//    NewDomainFunction: which domain to test?
//    bool: should test blocking regions?
class DomainTest
    : public ::testing::TestWithParam<std::tuple<NewDomainFunction, bool>> {
 public:
  DomainTest();

  // For every test case we assert that the Domain can be deleted and that no
  // schedulables did not run to completion.
  ~DomainTest();

 protected:
  // Creates a new schedulable, binds "work" to it and runs it within
  // domain(). "completion" will be signalled at the start and finish of
  // "work"'s execution.
  void RunClosure(Schedulable** schedulable, Closure* work,
                  AtomicCompletion* completion);

  // Execute "work" after "when_started" has started.
  Closure* WhenStarted(AtomicCompletion* when_started, Closure* work);

  // Execute "work" after "when_finished" has finished.
  Closure* WhenFinished(AtomicCompletion* when_finished, Closure* work);

  // Wait for all completions in a set to finish.
  void WaitUntilAllFinished(absl::Span<const AtomicCompletion> done);

  NewDomainFunction GetNewDomainFunction() { return std::get<0>(GetParam()); }

  bool ShouldTestBlockingRegions() { return std::get<1>(GetParam()); }

  Domain* const domain() const { return domain_; }

 private:
  // Internal helper for WhenStarted()/WhenFinished().
  // If sync_on_start, then execute 'work' once 'completion' starts.
  // If !sync_on_start, then execute 'work' once 'completion' finishes.
  static void CompletionHelper(AtomicCompletion* completion, Closure* work,
                               bool sync_on_start);

  Domain* const domain_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(DomainTest);  // TODO <link>

}  // namespace scheduling
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_TEST_H_
