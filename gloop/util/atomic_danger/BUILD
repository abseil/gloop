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
    features = ["header_modules"],
)

cc_library(
    name = "atomic_danger",
    hdrs = ["atomic_danger.h"],
    visibility = [
        "//:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/meta:type_traits",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "atomic_danger_test",
    srcs = ["atomic_danger_test.cc"],
    deps = [
        ":atomic_danger",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "refcount",
    hdrs = ["refcount.h"],
    # TODO: Consider restricting this once <link> is done.
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "refcount_test",
    srcs = ["refcount_test.cc"],
    deps = [
        ":refcount",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
