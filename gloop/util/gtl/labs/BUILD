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

# Description:
#   Shorter-term Libraries being considered for //gloop/util/gtl.
#   See README in this directory.

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "sorted_range",
    srcs = ["sorted_range_internal.h"],
    hdrs = ["sorted_range.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/gtl:comparator",
        "//gloop/util/gtl:iterator_adaptors",
        "@abseil-cpp//absl/container:fixed_array",
    ],
)

cc_test(
    name = "sorted_range_test",
    srcs = ["sorted_range_test.cc"],
    deps = [
        ":sorted_range",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:comparator",
        "//gloop/util/gtl:iterator_adaptors",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
