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
)

package_group(
    name = "util-tuple",
    packages = [
        "//gloop/util/tuple/...",
    ],
)

cc_library(
    name = "bindings",
    hdrs = ["bindings.h"],
    visibility = [":util-tuple"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/utility",
    ],
)

cc_test(
    name = "bindings_test",
    srcs = ["bindings_test.cc"],
    deps = [
        ":bindings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tuplify",
    hdrs = ["tuplify.h"],
    visibility = [
        "//gloop/util/tuple/bindings:__subpackages__",
    ],
    deps = [
        ":bindings",
        "//gloop:enforce_gloop_support",
        "//gloop/util/tuple:intrinsics",
    ],
)

cc_test(
    name = "tuplify_test",
    srcs = ["tuplify_test.cc"],
    deps = [
        ":tuplify",
        "//gloop/util/tuple:intrinsics",
        "//gloop/util/tuple:streamable",
        "//gloop/util/tuple:struct",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)
