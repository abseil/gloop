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
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//gloop/base:__pkg__"],
)

cc_library(
    name = "effective_user_id",
    srcs = ["effective_user_id.cc"],
    hdrs = ["effective_user_id.h"],
    visibility = [
        "//gloop/base:__pkg__",
        "@gloop//gloop/base:__pkg__",
        "@gloop//gloop/base/internal:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "effective_user_id_test",
    size = "small",
    srcs = ["effective_user_id_test.cc"],
    visibility = ["//gloop/base:__pkg__"],
    deps = [
        ":effective_user_id",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "init_google",
    hdrs = ["init_google.h"],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "logging_directories",
    srcs = ["logging_directories.cc"],
    hdrs = ["logging_directories.h"],
    deps = [
        ":temp_directories",
        "//gloop/base:log_file_flags",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log/internal:config",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "percpu",
    hdrs = ["percpu.inc"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/concurrent/percpu:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:config",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "temp_directories",
    srcs = ["temp_directories.cc"],
    hdrs = ["temp_directories.h"],
    deps = [
        "@abseil-cpp//absl/algorithm:container",
        "@gloop//gloop:enforce_gloop_support",
    ],
)
