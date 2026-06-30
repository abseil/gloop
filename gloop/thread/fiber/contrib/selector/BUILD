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
    features = [
        "parse_headers",
    ],
)

cc_library(
    name = "selector",
    hdrs = [
        "handlers.h",
        "selector.h",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:channel",
        "//gloop/thread/fiber:select",
        "//gloop/thread/fiber:selectables",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
        "@abseil-cpp//absl/utility",
    ],
)

cc_test(
    name = "selector_test",
    srcs = ["selector_test.cc"],
    deps = [
        ":selector",
        "//gloop:gloop_test_main",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:channel",
        "//gloop/thread/fiber:select",
        "//gloop/thread/fiber:selectables",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:simulated_clock",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
