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

# Package:
#   concurrent/percpu
#
# Description:
#   Collection of cpu-local objects and utlities.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "object",
    hdrs = ["object.h"],
    textual_hdrs = [
        "object_emulated.inc",
        "object_native.inc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:percpu",
        "//gloop/base:spinlock",
        "//gloop/base:sysinfo",
        "//gloop/base/internal:percpu",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "object_test",
    size = "small",
    srcs = ["object_test.cc"],
    linkstatic = 1,
    visibility = ["//visibility:private"],
    deps = [
        ":object",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
