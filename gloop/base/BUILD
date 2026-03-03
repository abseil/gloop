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

package_group(
    name = "base_implementation",
    packages = [
        "//gloop/base/...",
    ],
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = [":base_implementation"],
    features = ["header_modules"],
)

cc_library(
    name = "atomic_sequence_num",
    hdrs = ["atomic_sequence_num.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "atomic_sequence_num_test",
    size = "medium",
    srcs = ["atomic_sequence_num_test.cc"],
    deps = [
        ":atomic_sequence_num",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

# Platform specific library that currently contains implementations of snprintf
# and vsnprintf for Windows.
cc_library(
    name = "port",
    hdrs = ["port.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "port_test",
    size = "small",
    srcs = ["port_test.cc"] + [
        "port_byteswap_test.cc",
    ],
    deps = [
        ":port",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "strtoint",
    srcs = ["strtoint.cc"],
    hdrs = ["strtoint.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "strtoint_test",
    size = "small",
    srcs = ["strtoint_test.cc"],
    deps = [
        ":strtoint",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "uword",
    hdrs = ["uword.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)
