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
    features = ["header_modules"],
)

cc_library(
    name = "requires",
    hdrs = ["requires.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "stl_util",
    hdrs = ["stl_util.h"],
    deps = [
        ":requires",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "stl_util_unittest",
    srcs = ["stl_util_unittest.cc"],
    tags = [
        "no_test_ios_arm64",
        "no_test_ios_x86_64",
        "no_test_wasm",
    ],
    deps = [
        ":stl_util",
        ":unique_array",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:chunked_queue",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "unique_array",
    srcs = ["unique_array.cc"],
    hdrs = ["unique_array.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_test(
    name = "unique_array_test",
    srcs = ["unique_array_test.cc"],
    malloc = "//gloop:malloc",
    deps = [
        ":unique_array",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
