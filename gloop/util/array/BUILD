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

# No default_hdrs_check yet
package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "array2d",
    hdrs = ["array2d.h"],
    deps = [
        ":array_common",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "array2d_unittest",
    size = "small",
    srcs = ["array2d_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":array2d",
        "//gloop/util/gtl:unique_array",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "array3d",
    hdrs = ["array3d.h"],
    deps = [
        ":array_common",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "array3d_unittest",
    size = "small",
    srcs = ["array3d_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":array3d",
        "//gloop/util/gtl:unique_array",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "array_common",
    hdrs = ["array_common.h"],
    deps = ["//gloop:enforce_gloop_support"],
)
