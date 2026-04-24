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

# Libraries in this low-level package may not depend on libraries in packages
# that are not low level.   For more information, including how to submit
# changes to this file, see    <link>

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "best-fit-allocator",
    srcs = ["best-fit-allocator.cc"],
    hdrs = ["best-fit-allocator.h"],
    deps = [
        ":block-allocator",
        ":size-based-allocator",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
    ],
    alwayslink = 1,
)

cc_test(
    name = "best-fit-allocator_test",
    size = "small",
    srcs = ["best-fit-allocator_test.cc"],
    deps = [
        ":best-fit-allocator",
        ":block-allocator",
        ":tested-allocator",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "block-allocator",
    hdrs = ["block-allocator.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/registration",
    ],
)

cc_library(
    name = "size-based-allocator",
    srcs = ["size-based-allocator.cc"],
    hdrs = ["size-based-allocator.h"],
    deps = [
        ":block-allocator",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "size-based-allocator_test",
    size = "small",
    srcs = ["size-based-allocator_test.cc"],
    deps = [
        ":block-allocator",
        ":size-based-allocator",
        ":tested-allocator",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "tested-allocator",
    testonly = 1,
    srcs = ["tested-allocator.cc"],
    hdrs = ["tested-allocator.h"],
    deps = [
        ":block-allocator",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)
