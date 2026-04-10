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

# Description:
#   Floating-point operations with compiler-team-guaranteed semantics.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "castops",
    hdrs = ["castops.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "castops_test",
    srcs = [
        "castops_linkage_test.cc",
        "castops_test.cc",
    ],
    deps = [
        ":castops",
        "@abseil-cpp//absl/numeric:int128",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "roundedops",
    hdrs = ["roundedops.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:config",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "roundedops_test",
    srcs = ["roundedops_test.cc"],
    deps = [
        ":roundedops",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
