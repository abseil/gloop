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
    default_visibility = ["//visibility:private"],
    features = [
        "parse_headers",
        "header_modules",
    ],
)

cc_library(
    name = "fingerprint2011",
    srcs = ["fingerprint2011.cc"],
    hdrs = ["fingerprint2011.h"],
    copts = ["-O3"],
    visibility = ["//visibility:public"],
    deps = [
        ":murmur",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "fingerprint2011_test_lib",
    testonly = True,
    srcs = ["fingerprint2011_test.cc"],
    deps = [
        ":fingerprint2011",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
    alwayslink = True,
)

cc_test(
    name = "fingerprint2011_test",
    size = "small",
    deps = [
        ":fingerprint2011_test_lib",
    ],
)

cc_library(
    name = "murmur",
    srcs = ["murmur.cc"],
    hdrs = ["murmur.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

# Simple tests for murmur hashing, with some benchmarks.
cc_test(
    name = "murmur_test",
    size = "large",
    srcs = ["murmur_test.cc"],
    deps = [
        ":murmur",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

# Even more tests for murmur hashing.
cc_test(
    name = "murmur_large_test",
    size = "large",
    srcs = ["murmur_large_test.cc"],
    shard_count = 4,
    deps = [
        ":murmur",
        "//gloop/util/endian",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "crc",
    srcs = [
        "crc.cc",
        "crc_internal.h",
        "crc_poly.cc",
        "crc_powerpc.h",
        "crc_x86_arm_combined.cc",
    ],
    hdrs = ["crc.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/crc:crc32c",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "crc_concat_test",
    size = "small",
    srcs = ["crc_concat_test.cc"],
    deps = [
        ":crc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "crc_unittest",
    size = "medium",
    srcs = ["crc_unittest.cc"],
    deps = [
        ":crc",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "crc32c_x86_arm_combined_test",
    size = "small",
    srcs = [
        "crc32c_x86_arm_combined_test.cc",
        "crc_internal.h",
    ],
    deps = [
        ":crc",
        "//gloop/util/bits",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
