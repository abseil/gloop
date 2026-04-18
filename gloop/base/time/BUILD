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
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "time_unix_nanos",
    hdrs = ["time_unix_nanos.h"],
    # This library is intended to be used only in performance critical
    # low-level libraries. See time_unix_nanos.h for more information.
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/base/time:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "time_unix_nanos_test",
    srcs = ["time_unix_nanos_test.cc"],
    deps = [
        ":time_unix_nanos",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
