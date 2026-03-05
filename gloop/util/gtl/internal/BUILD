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
)

cc_library(
    name = "aggregate",
    hdrs = ["aggregate.h"],
    visibility = [
        "//gloop/util/gtl/extend/internal:__subpackages__",
        "//gloop/util/gtl/internal:__subpackages__",
    ],
    deps = [
        ":num_bases",
        ":num_initializers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "aggregate_test",
    srcs = ["aggregate_test.cc"],
    deps = [
        ":aggregate",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "dependencies",
    hdrs = ["dependencies.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
        "//gloop/util/gtl/internal:__subpackages__",
    ],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "dependencies_test",
    srcs = ["dependencies_test.cc"],
    deps = [
        ":dependencies",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "num_bases",
    hdrs = ["num_bases.h"],
    visibility = [
        "//gloop/util/gtl/internal:__subpackages__",
    ],
    deps = [
        ":num_initializers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "num_bases_test",
    srcs = ["num_bases_test.cc"],
    deps = [
        ":num_bases",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "num_initializers",
    hdrs = ["num_initializers.h"],
    visibility = [
        "//gloop/util/gtl/internal:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "num_initializers_test",
    srcs = ["num_initializers_test.cc"],
    deps = [
        ":num_initializers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
