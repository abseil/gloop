# Copyright 2026 Google LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load(
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "os_semaphore",
    hdrs = ["os_semaphore.h"],
    visibility = [
        "//gloop/thread:__subpackages__",
        "@gloop//gloop/thread:__subpackages__",
        "@gloop//gloop/thread/fiber:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "os_semaphore_test",
    size = "small",
    srcs = ["os_semaphore_test.cc"],
    deps = [
        ":os_semaphore",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "run_in_thread",
    srcs = ["run_in_thread.cc"],
    hdrs = ["run_in_thread.h"],
    visibility = [
    ],
    deps = [
        ":os_semaphore",
        ":thread-internal",
        "//gloop/base:port",
        "//gloop/base:signal-handler",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "thread-internal",
    hdrs = ["thread-internal.h"],
    visibility = [
        "//gloop/thread:__subpackages__",
    ],
    deps = [
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/util/gtl:intrusive_list",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)
