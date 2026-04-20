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
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
)

cc_library(
    name = "example_lib",
    srcs = ["example.cc"],
    hdrs = ["example.h"],
    deps = [
        ":example_cc_proto",
        "@abseil-cpp//absl/status",
        "@gloop//gloop:enforce_gloop_support",
        "@gloop//gloop/net/base:ipaddress",
        "@gloop//gloop/util/math:mathutil",
        "@gloop//gloop/util/status",
        "@gloop//gloop/util/status:codes_cc_proto",
        "@gloop//gloop/util/status:ret_check",
        "@gloop//gloop/util/status:status_builder",
        "@gloop//gloop/util/status:status_cc_proto",
        "@gloop//gloop/util/status:status_macros",
    ],
)

cc_test(
    name = "example_test",
    srcs = ["example_test.cc"],
    deps = [
        ":example_lib",
        "//gloop/base:config",
        "@googletest//:gtest_main",
    ],
)

proto_library(
    name = "example_proto",
    srcs = ["example.proto"],
    deps = [
        "@gloop//gloop/util/status:codes_proto",
        "@gloop//gloop/util/status:status_proto",
    ],
)

cc_proto_library(
    name = "example_cc_proto",
    deps = [":example_proto"],
)
