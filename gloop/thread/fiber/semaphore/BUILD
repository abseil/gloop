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

# Description: A fiber-friendly FIFO semaphore implementation.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "ordered_semaphore",
    srcs = ["ordered_semaphore.cc"],
    hdrs = ["ordered_semaphore.h"],
    implementation_deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/log:vlog_is_on",
    ],
    visibility = [
        "//thread/fiber/semaphore:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/thread/fiber:select",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "fifo_semaphore",
    srcs = ["fifo_semaphore.cc"],
    hdrs = ["fifo_semaphore.h"],
    implementation_deps = [
        "//gloop/thread/fiber",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ordered_semaphore",
        "//gloop:enforce_gloop_support",
        "//gloop/thread/fiber:select",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
    ],
)

cc_test(
    name = "fifo_semaphore_test",
    size = "large",
    srcs = ["fifo_semaphore_test.cc"],
    linkstatic = 1,
    visibility = ["//visibility:private"],
    deps = [
        ":fifo_semaphore",
        ":ordered_semaphore",
        "//gloop:gloop_test_main",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:select",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
