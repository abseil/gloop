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

# Descrption:
# Memory utilities

cc_library(
    name = "free_deleter",
    hdrs = ["free_deleter.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "free_deleter_test",
    srcs = ["free_deleter_test.cc"],
    deps = [
        ":free_deleter",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "memory_mapped_io",
    srcs = [
        "memory_mapped_io.cc",
        "scoped_mmap.cc",
    ],
    hdrs = [
        "memory_mapped_io.h",
        "scoped_mmap.h",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "memory_mapped_io_test_data",
    srcs = ["memory_mapped_io_test_data.cc"],
    hdrs = ["memory_mapped_io_test_data.h"],
    visibility = [
        "//gloop/util/memory:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "memory_mapped_io_test",
    size = "small",
    srcs = ["memory_mapped_io_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":memory_mapped_io",
        ":memory_mapped_io_test_data",
        "//gloop/base:init_google",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "memory_mapped_io_test_data_test",
    size = "small",
    srcs = ["memory_mapped_io_test_data_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":memory_mapped_io_test_data",
        "//gloop/base:init_google",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
