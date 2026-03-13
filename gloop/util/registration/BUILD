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

#
# Copyright 2006 Google Inc. All Rights Reserved.
#
# Description:
#   Helpers for registering classes.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

# Static map
cc_library(
    name = "static_map",
    hdrs = ["static_map.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "static_map_test",
    size = "small",
    srcs = ["static_map_test.cc"],
    deps = [
        ":static_map",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
