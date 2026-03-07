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

# Copyright 2012, Google Inc.  All rights reserved.
#

# Libraries in this low-level package may not depend on libraries in packages
# that are not low level.   For more information, including how to submit
# changes to this file, see    <link>howto/build-monitors.html

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "intops",
    hdrs = [
        "lossless_convert.h",
        "safe_int.h",
        "safe_int_ops.h",
        "saturated.h",
        "saturated_cast.h",
        "strong_int.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/util/floatops:castops",
        "//gloop/util/symbolize:demangle",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "lossless_convert_test",
    size = "small",
    srcs = ["lossless_convert_test.cc"],
    deps = [
        ":intops",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "saturated_test",
    srcs = ["saturated_test.cc"],
    deps = [
        ":intops",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "saturated_cast_test",
    size = "small",
    srcs = ["saturated_cast_test.cc"],
    deps = [
        ":intops",
        "//gloop/util/symbolize:demangle",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

# Broken out to avoid circular dependency.
cc_library(
    name = "strong_containers",
    srcs = [],
    hdrs = [
        "strong_array.h",
        "strong_fixedarray.h",
        "strong_vector.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":intops",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

# Build this in opt mode and disassemble it to demonstrate that the compiler
# optimizes out the null validator. You can run this command:
#  bazel build --save_temps -c opt strong_int_disassemble_me
cc_binary(
    name = "strong_int_disassemble_me",
    testonly = 1,
    srcs = ["strong_int_disassemble_me.cc"],
    deps = [
        ":intops",
        ":strong_containers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "strong_container_test",
    size = "small",
    timeout = "long",
    srcs = ["strong_container_test.cc"],
    deps = [
        ":intops",
        ":strong_containers",
        "//gloop/base:uword",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/hash:hash_testing",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "strong_int_test",
    size = "small",
    timeout = "long",
    srcs = ["strong_int_test.cc"],
    shard_count = 15,
    deps = [
        ":intops",
        "//gloop/base:uword",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/flags:marshalling",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "safe_int_test",
    size = "small",
    timeout = "long",
    srcs = ["safe_int_test.cc"],
    deps = [
        ":intops",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "safe_int_ops_test",
    size = "medium",
    srcs = ["safe_int_ops_test.cc"],
    shard_count = 20,
    deps = [
        ":intops",
        "//gloop/util/random:mt_random",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
