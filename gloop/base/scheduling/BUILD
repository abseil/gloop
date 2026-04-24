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
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

# Base scheduling support.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "domain_thread_assignment_callback_accessor",
    hdrs = ["domain_thread_assignment_callback_accessor.h"],
    visibility = [
        "//gloop/base/scheduling:__subpackages__",
        "//gloop/thread/fiber/internal:__subpackages__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "low-level-support",
    hdrs = ["low-level-support.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_library(
    name = "scheduling",
    # Work around circular references.
    srcs = [
        "domain.cc",
        "domain_thread_assignment_callback_accessor.cc",
        "downcalls.cc",
        "scheduler.cc",
    ],
    hdrs = [
        "domain.h",
        "downcalls.h",
        "scheduler.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":domain_thread_assignment_callback_accessor",
        ":low-level-support",
        "//gloop:enforce_gloop_support",
        "//gloop/base:examine_stack",
        "//gloop/base:spinlock",
        "//gloop/base:static_threadlocal",
        "//gloop/base:thread_identity",
        "//gloop/thread/fiber:per-domain-counters",
        "//gloop/util/atomic_danger",
        "//gloop/util/atomic_danger:refcount",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
    ],
    alwayslink = 1,
)

cc_test(
    name = "scheduler_test",
    srcs = ["scheduler_test.cc"],
    deps = [
        ":scheduling",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "scheduling_mode",
    hdrs = ["scheduling_mode.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:base_internal",
    ],
)

cc_test(
    name = "downcalls_test",
    srcs = ["downcalls_test.cc"],
    deps = [
        ":low-level-support",
        ":scheduling",
        "//gloop/base:init_google",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "domain-test",
    testonly = 1,
    srcs = ["domain-test.cc"],
    hdrs = ["domain-test.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        ":low-level-support",
        ":scheduling",
        "//gloop:enforce_gloop_support",
        "//gloop/base:callback",
        "//gloop/base:sysinfo",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:fifolifo-schedulers",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
    alwayslink = 1,
)
