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
# changes to this file, see    <link>howto/build-monitors.html

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "mathlimits",
    hdrs = ["mathlimits.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "mathlimits_unittest",
    srcs = ["mathlimits_unittest.cc"],
    deps = [
        ":mathlimits",
        "@abseil-cpp//absl/log",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "mathutil",
    srcs = ["mathutil.cc"],
    hdrs = ["mathutil.h"],
    deps = [
        ":mathlimits",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/numeric:bits",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "mathutil_unittest",
    srcs = ["mathutil_unittest.cc"],
    shard_count = 16,
    deps = [
        ":mathlimits",
        ":mathutil",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "fastmath",
    srcs = ["fastmath.cc"],
    hdrs = ["fastmath.h"],
    deps = [
        "@abseil-cpp//absl/base",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "fastmath_unittest",
    srcs = ["fastmath_unittest.cc"],
    deps = [
        ":fastmath",
        ":mathutil",
        "//gloop/base:timer",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "constant_divisor",
    srcs = ["constant_divisor.cc"],
    hdrs = ["constant_divisor.h"],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "constant_divisor_test",
    size = "small",
    srcs = ["constant_divisor_test.cc"],
    deps = [
        ":constant_divisor",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:bit_gen_ref",
        "@abseil-cpp//absl/random:distributions",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "round",
    srcs = ["round.cc"],
    hdrs = ["round.h"],
    deps = [
        ":mathutil",
        "//gloop/util/bits",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "round_test",
    srcs = ["round_test.cc"],
    deps = [
        ":round",
        "//gloop/strings:numbers",
        "//gloop/util/bits",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
