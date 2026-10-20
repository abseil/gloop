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

cc_library(
    name = "parsed_process_stat",
    srcs = ["parsed_process_stat.cc"],
    hdrs = ["parsed_process_stat.h"],
    implementation_deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
    ],
    visibility = [
        "//gloop/base:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
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
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
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
