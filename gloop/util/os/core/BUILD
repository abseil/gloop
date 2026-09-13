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

load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

# POSIX-related wrappers and utilities.
cc_library(
    name = "syscalls",
    srcs = ["syscalls.cc"],
    hdrs = ["syscalls.h"],
    deps = [
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "syscalls_test",
    srcs = ["syscalls_test.cc"],
    deps = [
        ":syscalls",
        "//gloop/util/status:errno_mapping",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "cpu_set_test_util",
    testonly = 1,
    srcs = ["cpu_set_test_util.cc"],
    hdrs = ["cpu_set_test_util.h"],
    deps = [
        ":cpu_set",
    ],
)

cc_library(
    name = "cpu_set",
    srcs = ["cpu_set.cc"],
    hdrs = ["cpu_set.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "cpu_set_test",
    srcs = ["cpu_set_test.cc"],
    deps = [
        ":cpu_set",
        ":cpu_set_test_util",
        ":test_cc_proto",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

proto_library(
    name = "test_proto",
    testonly = 1,
    srcs = ["test_proto.proto"],
)

cc_proto_library(
    name = "test_cc_proto",
    testonly = 1,
    deps = [":test_proto"],
)
