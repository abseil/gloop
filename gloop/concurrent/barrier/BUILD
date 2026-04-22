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

# Generic Barrier types as described by <link>++-concurrency#barriers

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
)

# --------------------------------------------------------------------
# Libraries
# --------------------------------------------------------------------

cc_library(
    name = "barrier",
    hdrs = [
        "barrier.h",
        "barrier_internal.h",
    ],
    hdrs_check = "strict",
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

# --------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------

cc_test(
    name = "barrier_test",
    size = "small",
    srcs = ["barrier_test.cc"],
    deps = [
        ":barrier",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "incremental_barrier",
    srcs = ["incremental_barrier.cc"],
    hdrs = ["incremental_barrier.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/perftools/tracing",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/util/atomic_danger:refcount",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "incremental_barrier_unittest",
    srcs = ["incremental_barrier_unittest.cc"],
    deps = [
        ":incremental_barrier",
        "@abseil-cpp//absl/functional:any_invocable",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
