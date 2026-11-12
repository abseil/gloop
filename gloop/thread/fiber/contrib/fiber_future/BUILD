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

# Description:
#   A lightweight Future which uses Fibers to propagate cancellation.

cc_library(
    name = "fiber_future",
    hdrs = ["fiber_future.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:select",
        "//gloop/thread/fiber:selectables",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
    ],
)

cc_test(
    name = "fiber_future_test",
    size = "small",
    srcs = ["fiber_future_test.cc"],
    deps = [
        ":fiber_future",
        "//gloop:gloop_test_main",
        "//gloop/base/scheduling",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:select",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
