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

# Description:
#   Semi-experimental helper libraries for Futures. See
#   @gloop//gloop/thread/future for Future's definition.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "futures",
    hdrs = [
        "creation.h",
        "join.h",
    ],
    deps = [
        "//gloop/concurrent/barrier",
        "//gloop/thread",
        "//gloop/thread/future:transform_future",
        "//gloop/util/tuple/components:for_each",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "creation_test",
    size = "small",
    srcs = ["creation_test.cc"],
    deps = [
        ":futures",
        "//gloop/thread",
        "//gloop/thread/future:make_future_pair",
        "//gloop/thread/future:transform_future",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:gloop_test_main",
        "@gloop//gloop/thread/future",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "join_test",
    size = "small",
    srcs = ["join_test.cc"],
    deps = [
        ":futures",
        "//gloop/base:walltime",
        "//gloop/thread",
        "//gloop/thread/future:make_future_pair",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
