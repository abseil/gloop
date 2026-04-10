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
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "arrival-order-scheduler",
    srcs = ["arrival-order-scheduler.cc"],
    hdrs = ["arrival-order-scheduler.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scheduler_types",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:inlined_vector",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "per-domain-counters",
    hdrs = ["per-domain-counters.h"],
    visibility = [
        "//gloop/base/scheduling:__subpackages__",
    ],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "probabilistic_test_util",
    testonly = 1,
    hdrs = ["probabilistic_test_util.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/log",
        "@gloop//gloop:enforce_gloop_support",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "probabilistic_test_util_test",
    srcs = ["probabilistic_test_util_test.cc"],
    deps = [
        ":probabilistic_test_util",
        "@abseil-cpp//absl/log",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "scheduler_types",
    srcs = ["scheduler-types.cc"],
    hdrs = ["scheduler-types.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/base:spinlock_wait",
        "@abseil-cpp//absl/container:fixed_array",
        "@gloop//gloop:enforce_gloop_support",
        "@gloop//gloop/util/atomic_danger",
    ],
)

cc_test(
    name = "scheduler-types_test",
    srcs = ["scheduler-types_test.cc"],
    deps = [
        ":scheduler_types",
        "//gloop/thread",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:gloop_test_main",
        "@gloop//gloop/base:callback",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
