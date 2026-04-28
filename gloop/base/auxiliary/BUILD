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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# Auxiliary implementation details for base

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_test(
    name = "contentionz_test",
    size = "large",
    timeout = "moderate",
    srcs = ["contentionz_test.cc"],
    flaky = 1,
    deps = [
        ":synchronization_profiling",
        ":synchronization_profiling_test_util",
        ":synchronization_tags",
        "//gloop:gloop_test_main",
        "//gloop/base:profile",
        "//gloop/strings:strip",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "deltacontentionz_test",
    size = "large",
    timeout = "moderate",
    srcs = ["deltacontentionz_test.cc"],
    flaky = 1,
    shard_count = 15,
    deps = [
        ":synchronization_profile",
        ":synchronization_profiling",
        ":synchronization_profiling_test_util",
        "//gloop:gloop_test_main",
        "//gloop/strings:strip",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "parsed_process_stat",
    srcs = ["parsed_process_stat.cc"],
    hdrs = ["parsed_process_stat.h"],
    visibility = [
        "//gloop/base:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "parsed_process_stat_test",
    srcs = ["parsed_process_stat_test.cc"],
    deps = [
        ":parsed_process_stat",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "synchronization_profiling",
    srcs = ["synchronization_profiling.cc"],
    hdrs = ["synchronization_profiling.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
        "//thread/fiber:__pkg__",
    ],
    deps = [
        ":synchronization_profile",
        ":synchronization_tags",
        "//gloop:enforce_gloop_support",
        "//gloop/base:examine_stack",
        "//gloop/base:profile",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "synchronization_profiling_test_util",
    testonly = 1,
    srcs = ["synchronization_profiling_test_util.cc"],
    hdrs = ["synchronization_profiling_test_util.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:spinlock",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "synchronization_profile",
    hdrs = ["synchronization_profile.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:profile",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "synchronization_tags",
    hdrs = ["synchronization_tags.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "syscall_linux",
    hdrs = [
        "linux_syscall_support_sanitizer.inc",
        "syscall_linux_aarch64.inc",
        "syscall_linux_arm.inc",
        "syscall_linux_riscv.inc",
        "syscall_linux_x86.inc",
        "syscall_linux_x86_64.inc",
    ],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)
