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

# Libraries in this low-level package may not depend on libraries in packages
# that are not low level.   For more information, including how to submit
# changes to this file, see    <link>

# Description:
#   A set of utilities for manipulating time.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

# Libraries:

# Implements randomized exponential backoff.
cc_library(
    name = "backoff",
    srcs = ["backoff.cc"],
    hdrs = ["backoff.h"],
    deps = [
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "backoff_test",
    size = "medium",
    srcs = ["backoff_test.cc"],
    deps = [
        ":backoff",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "monotonic_clock",
    srcs = [
        "monotonic_clock.cc",
    ],
    hdrs = [
        "monotonic_clock.h",
        "monotonic_clock-internal.h",
    ],
    deps = [
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
    ],
)

cc_library(
    name = "protoutil",
    srcs = ["protoutil.cc"],
    hdrs = ["protoutil.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@protobuf//:duration_cc_proto",
        "@protobuf//:timestamp_cc_proto",
    ],
)

# Tests:

cc_test(
    name = "monotonic_clock_test",
    srcs = ["monotonic_clock_test.cc"],
    deps = [
        ":monotonic_clock",
        "//gloop/base:commandlineflags",
        "//gloop/thread",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
        "@abseil-cpp//absl/time:simulated_clock",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "protoutil_test",
    srcs = ["protoutil_test.cc"],
    deps = [
        ":protoutil",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:duration_cc_proto",
        "@protobuf//:timestamp_cc_proto",
    ],
)
