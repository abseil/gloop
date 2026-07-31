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

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

bzl_library(
    name = "def",
    srcs = ["def.bzl"],
    visibility = ["//visibility:public"],
    deps = [
        "@bazel_skylib//lib:new_sets",
        "@bazel_skylib//rules:build_test",
        "@protobuf//bazel/common:proto_info_bzl",
    ],
)

# Compares proto_data output to manually-constructed expectation
cc_test(
    name = "proto_data_test",
    size = "small",
    srcs = ["proto_data_test.cc"],
    data = [
        "//gloop/tools/build_rules/text_to_binary/testdata:deterministic_abc",
        "//gloop/tools/build_rules/text_to_binary/testdata:deterministic_cba",
        "//gloop/tools/build_rules/text_to_binary/testdata:test_data.binarypb",
    ],
    deps = [
        "//gloop/tools/build_rules/text_to_binary/testdata:test_cc_proto",
        "//gloop/tools/build_rules/text_to_binary/testdata:test_extension_cc_proto",
        "//gloop/tools/build_rules/text_to_binary/testdata:test_nested_cc_proto",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
        "@protobuf//:json_util",
    ],
)
