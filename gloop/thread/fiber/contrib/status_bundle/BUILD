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

cc_library(
    name = "status_bundle",
    srcs = ["status_bundle.cc"],
    hdrs = ["status_bundle.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:select",
        "//gloop/util/functional:from_callback",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
    ],
)

########################################################################
# Tests
########################################################################

cc_test(
    name = "status_bundle_test",
    size = "small",
    srcs = ["status_bundle_test.cc"],
    deps = [
        ":status_bundle",
        "//gloop:gloop_test_main",
        "//gloop/base:context",
        "//gloop/base:tracecontext",
        "//gloop/base:tracer",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:channel",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:select",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
