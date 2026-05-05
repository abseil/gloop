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

load(
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "loop_executor",
    srcs = ["loop_executor.cc"],
    hdrs = ["loop_executor.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:callback",
        "//gloop/base:varsetter",
        "//gloop/thread",
        "//gloop/thread:shutdown_gate",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "loop_executor_test",
    size = "medium",
    srcs = ["loop_executor_test.cc"],
    deps = [
        ":loop_executor",
        "//gloop:gloop_test_main",
        "//gloop/base:atomic_sequence_num",
        "//gloop/base:callback",
        "//gloop/thread",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
