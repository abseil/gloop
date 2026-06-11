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
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "csv",
    srcs = [
        "parser.cc",
        "writer.cc",
    ],
    hdrs = [
        "parser.h",
        "writer.h",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/strings:bytestream",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "parser_test",
    size = "small",
    srcs = ["parser_test.cc"],
    data = glob(["testdata/*.csv"]),
    deps = [
        ":csv",
        "//gloop:gloop_test_main",
        "//gloop/strings:bytestream",
        "//gloop/strings:numbers",
        "//gloop/strings:split",
        "//gloop/util/functional:to_callback",
        "//gloop/util/gtl:unique_array",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "writer_test",
    size = "small",
    srcs = ["writer_test.cc"],
    deps = [
        ":csv",
        "//gloop:gloop_test_main",
        "//gloop/strings:bytestream",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
