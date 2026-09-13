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

#ifndef THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_THREAD_ASSIGNMENT_CALLBACK_ACCESSOR_H_
#define THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_THREAD_ASSIGNMENT_CALLBACK_ACCESSOR_H_

#ifndef _WIN32
#include <pthread.h>
#else
typedef unsigned long int pthread_t;
#endif

#include <functional>

namespace base {
namespace scheduling {

class Domain;
class Schedulable;

using ThreadAssignmentCallback =
    std::function<void(pthread_t tid, Schedulable* schedulable)>;

class DomainThreadAssignmentCallbackAccessor {
 public:
  static void SetThreadAssignmentCallback(Domain& domain,
                                          ThreadAssignmentCallback callback);
};

}  // namespace scheduling
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_THREAD_ASSIGNMENT_CALLBACK_ACCESSOR_H_
