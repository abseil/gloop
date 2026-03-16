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
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "human_readable",
    srcs = ["human_readable.cc"],
    hdrs = ["human_readable.h"],
    deps = [
        ":si_prefix",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "human_readable_test",
    size = "small",
    srcs = ["human_readable_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":human_readable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ostringstream",
    srcs = ["ostringstream.cc"],
    hdrs = ["ostringstream.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "ostringstream_test",
    size = "small",
    srcs = ["ostringstream_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":ostringstream",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "si_prefix",
    srcs = ["si_prefix.cc"],
    hdrs = ["si_prefix.h"],
    deps = [
        "//gloop/util/math:mathutil",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:optional",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "si_prefix_test",
    size = "small",
    srcs = ["si_prefix_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":si_prefix",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:optional",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "strip",
    srcs = ["strip.cc"],
    hdrs = ["strip.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "strip_test",
    size = "small",
    srcs = ["strip_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":strip",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "string_view_utils",
    srcs = ["string_view_utils.cc"],
    hdrs = ["string_view_utils.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "string_view_utils_test",
    size = "medium",
    srcs = ["string_view_utils_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":string_view_utils",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@re2",
    ],
)
