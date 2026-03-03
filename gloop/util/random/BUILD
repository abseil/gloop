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

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//gloop:__subpackages__"],
    features = ["header_modules"],
)

cc_library(
    name = "acmrandom",
    srcs = ["acmrandom.cc"],
    hdrs = ["acmrandom.h"],
    deps = [
        ":random_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "acmrandom_unittest",
    size = "small",
    srcs = ["acmrandom_unittest.cc"],
    deps = [
        ":acmrandom",
        "//gloop/util/math:mathutil",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "mt_random",
    srcs = ["mt_random.cc"],
    hdrs = ["mt_random.h"],
    deps = [
        ":random_base",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "mt_random_unittest",
    size = "small",
    srcs = ["mt_random_unittest.cc"],
    deps = [
        ":mt_random",
        ":secure_random_testutils",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "random_base",
    srcs = ["random_base.cc"],
    hdrs = ["random_base.h"],
    deps = [
        "//gloop/base:atomic_sequence_num",
        "//gloop/base:port",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "random_base_unittest",
    size = "small",
    srcs = ["random_base_unittest.cc"],
    deps = [
        ":mt_random",
        ":random_base",
        ":test_random",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "secure_random_testutils",
    testonly = 1,
    srcs = ["random_base_testutils.cc"],
    hdrs = ["random_base_testutils.h"],
    deps = [
        ":random_base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "test_random",
    hdrs = ["test_random-inl.h"],
    deps = [
        ":random_base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)
