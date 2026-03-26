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
# Libraries in this low-level package may not depend on libraries in
# packages that are not low level.   For more information, including
# how to submit changes to this file, see
#      <link>howto/build-monitors.html

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:public"],
    features = [
        "parse_headers",
        "header_modules",
    ],
)

cc_library(
    name = "blocking_refcount",
    srcs = ["blocking_refcount.cc"],
    hdrs = ["blocking_refcount.h"],
    hdrs_check = "strict",
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "blocking_refcount_test",
    size = "medium",
    srcs = ["blocking_refcount_test.cc"],
    deps = [
        ":blocking_refcount",
        "//gloop/base:atomic_stats_counter",
        "//gloop/base:walltime",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "reffed_ptr",
    hdrs = ["reffed_ptr.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/meta:type_traits",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "reffed_ptr_test",
    size = "small",
    srcs = ["reffed_ptr_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":reffed_ptr",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
