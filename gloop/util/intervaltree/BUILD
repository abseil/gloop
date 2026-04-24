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
    default_visibility = ["//visibility:public"],
)

# Description: Utilities for interval tree

cc_library(
    name = "intervaltree",
    hdrs = [
        "intervaltree.h",
        "intervaltree-inl.h",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:arena",
        "//gloop/base:arena_allocator",
        "//gloop/base:init_google",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
    ],
)

cc_test(
    name = "intervaltree_unittest",
    srcs = ["intervaltree_unittest.cc"],
    malloc = "//gloop:malloc",
    deps = [
        ":intervaltree",
        "//gloop/base:arena",
        "//gloop/base:init_google",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
