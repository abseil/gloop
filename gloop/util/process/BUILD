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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

# Libraries
# =========================================================

cc_library(
    name = "nul_terminated_buf_appender",
    srcs = ["nul_terminated_buf_appender.cc"],
    hdrs = ["nul_terminated_buf_appender.h"],
    visibility = [
        "//gloop/util/process:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "process",
    srcs = ["subprocess.cc"],
    hdrs = ["subprocess.h"],
    copts = select({
                "//gloop:ubsan_enabled": ["-DCONFIG_UBSAN"],
                "//conditions:default": [],
            }) +
            # Disable profile and xray (b/7924015) instrumentation for this code.
            # These are negating the values passed by --config=clang-coverage
            select({
                "@rules_cc//cc/compiler:clang": [
                    "-fno-coverage-mapping",
                    "-fno-profile-instr-generate",
                ],
                "@rules_cc//cc/compiler:clang-cl": [
                    "-fno-coverage-mapping",
                    "-fno-profile-instr-generate",
                ],
                "//conditions:default": [],
            }),
    deps = [
        ":nul_terminated_buf_appender",
        "//gloop:enforce_gloop_support",
        "//gloop/base:callback",
        "//gloop/base:commandlineflags",
        "//gloop/base:config",
        "//gloop/base:file_toc",
        "//gloop/base:googleinit",
        "//gloop/base:linux_syscall_support",
        "//gloop/base:logging_extensions",
        "//gloop/base:process_state",
        "//gloop/base:strerror",
        "//gloop/base:sysinfo",
        "//gloop/base:walltime",
        "//gloop/base/scheduling",
        "//gloop/strings:bytestream",
        "//gloop/strings:util",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "//gloop/util/gtl:unique_array",
        "//gloop/util/hash",
        "//gloop/util/status",
        "//gloop/util/status:status_builder",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:optional",
    ],
)

# Unit Tests
# =========================================================

cc_test(
    name = "nul_terminated_buf_appender_test",
    srcs = ["nul_terminated_buf_appender_test.cc"],
    deps = [
        ":nul_terminated_buf_appender",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "subprocess_unittest_helper",
    testonly = 1,
    srcs = ["subprocess_unittest_helper.cc"],
    deps = [
        ":process",
        "//gloop:enforce_gloop_support",
        "//gloop/base:init_google",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
    ],
)

# Use list comprehension to create multiple cc_test targets.
[cc_test(
    name = "subprocess_unittest_" + subprocess_fork_method,
    size = "large",
    srcs = ["subprocess_unittest.cc"],
    args = ["--subprocess_fork_method=" + subprocess_fork_method],
    data = [
        ":subprocess_unittest_helper",
    ],
    tags = ["notsan"],
    deps = [
        ":process",
        "//gloop/base:callback",
        "//gloop/base:file_toc",
        "//gloop/base:linux_syscall_support",
        "//gloop/base:logging_extensions",
        "//gloop/base:strerror",
        "//gloop/base:sysinfo",
        "//gloop/base:time_support",
        "//gloop/base:walltime",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:fifolifo-schedulers",
        "//gloop/thread/fiber:select",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
) for subprocess_fork_method in [
    "fork",
    "clone",
]]

# Use list comprehension to create multiple cc_test targets.
[cc_test(
    name = "subprocess_unittest_" + subprocess_fork_method + "_static",
    size = "large",
    srcs = ["subprocess_unittest.cc"],
    args = ["--subprocess_fork_method=" + subprocess_fork_method],
    data = [
        ":subprocess_unittest_helper",
    ],
    features = ["fully_static_link"],
    linkstatic = 1,
    tags = ["nosan"],
    deps = [
        ":process",
        "//gloop/base:callback",
        "//gloop/base:file_toc",
        "//gloop/base:linux_syscall_support",
        "//gloop/base:logging_extensions",
        "//gloop/base:strerror",
        "//gloop/base:sysinfo",
        "//gloop/base:time_support",
        "//gloop/base:walltime",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:fifolifo-schedulers",
        "//gloop/thread/fiber:select",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
) for subprocess_fork_method in [
    "fork",
    "clone",
]]

cc_test(
    name = "subprocess_signal_test",
    size = "small",
    srcs = ["subprocess_signal_test.cc"],
    deps = [
        ":process",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)
