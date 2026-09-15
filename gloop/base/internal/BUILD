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
    implementation_deps = ["@abseil-cpp//absl/strings"],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "effective_user_id_test",
    size = "small",
    srcs = ["effective_user_id_test.cc"],
    visibility = ["//gloop/base:__pkg__"],
    deps = [
        ":effective_user_id",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "gcapturedstream",
    testonly = 1,
    srcs = ["gcapturedstream.cc"],
    hdrs = ["gcapturedstream.h"],
    implementation_deps = [
        "//gloop/base:port",
        "@abseil-cpp//absl/log:check",
    ],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "init_google",
    hdrs = ["init_google.h"],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "logging",
    hdrs = ["logging.h"],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:nullability",
    ],
)

cc_library(
    name = "logging_directories",
    srcs = ["logging_directories.cc"],
    hdrs = ["logging_directories.h"],
    implementation_deps = [
        ":temp_directories",
        "//gloop/base:log_file_flags",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/synchronization",
    ],
    visibility = [
        "//gloop/base:__pkg__",
        "//gloop/base/internal:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:config",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "logging_globals",
    srcs = ["logging_globals.cc"],
    hdrs = ["logging_globals.h"],
    implementation_deps = ["@abseil-cpp//absl/base:core_headers"],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "munge_output",
    testonly = 1,
    srcs = ["munge_output.cc"],
    hdrs = ["munge_output.h"],
    implementation_deps = [
        "//gloop/util/status:errno_mapping",
        "@re2",
    ],
    visibility = [
        "//gloop/base:__pkg__",
        "//gloop/base/internal:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "munge_output_test",
    srcs = ["munge_output_test.cc"],
    deps = [
        ":munge_output",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "percpu",
    hdrs = ["percpu.inc"],
    visibility = [
        "//gloop/base:__pkg__",
        "//gloop/concurrent/percpu:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:config",
    ],
)

cc_library(
    name = "temp_directories",
    srcs = ["temp_directories.cc"],
    hdrs = ["temp_directories.h"],
    implementation_deps = ["@abseil-cpp//absl/algorithm:container"],
    visibility = [
        "//gloop/base:__pkg__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)
