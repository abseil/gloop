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

#include "gloop/concurrent/futures/join.h"

#include <string>
#include <utility>

#include "absl/base/casts.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/walltime.h"
#include "gloop/concurrent/futures/creation.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/future/make_future_pair.h"
#include "gloop/thread/threadpool.h"
#include "gloop/thread/timedcall.h"
#include "gtest/gtest.h"
