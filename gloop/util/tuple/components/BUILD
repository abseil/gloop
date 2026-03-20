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
    default_visibility = [
        "//gloop/util/tuple:__subpackages__",
    ],
)

cc_library(
    name = "ignore_index",
    hdrs = ["ignore_index.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "intrinsics",
    hdrs = ["intrinsics.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "intrinsics_test",
    size = "small",
    srcs = ["intrinsics_test.cc"],
    deps = [
        ":intrinsics",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "std_tuple",
    hdrs = ["std_tuple.h"],
    deps = [
        ":intrinsics",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "std_tuple_test",
    size = "small",
    srcs = ["std_tuple_test.cc"],
    deps = [
        ":intrinsics",
        ":std_tuple",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "pair",
    hdrs = ["pair.h"],
    deps = [
        ":intrinsics",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "pair_test",
    size = "small",
    srcs = ["pair_test.cc"],
    deps = [
        ":intrinsics",
        ":pair",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "array",
    hdrs = ["array.h"],
    deps = [
        ":intrinsics",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "array_test",
    size = "small",
    srcs = ["array_test.cc"],
    deps = [
        ":array",
        ":intrinsics",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "internal_iterate",
    textual_hdrs = ["internal_iterate.h"],
    deps = [
        ":array",
        ":intrinsics",
        ":pair",
        ":std_tuple",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "iterate",
    hdrs = ["iterate.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ignore_index",
        ":internal_iterate",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "iterate_test",
    size = "small",
    srcs = ["iterate_test.cc"],
    deps = [
        ":iterate",
        "//gloop/util/tuple:test_util",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "for_each",
    hdrs = ["for_each.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ignore_index",
        ":intrinsics",
        ":iterate",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "for_each_test",
    size = "small",
    srcs = ["for_each_test.cc"],
    deps = [
        ":for_each",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "streamable",
    srcs = ["streamable.cc"],
    hdrs = ["streamable.h"],
    defines = select({
        "//conditions:default": [],
    }) + ["UTIL_TUPLE_COMPONENTS_HAVE_NET_PROTO2"],
    visibility = ["//visibility:public"],
    deps = [
        ":for_each",
        ":intrinsics",
        ":rank",
        "//gloop/strings:ostringstream",
        "//gloop/util/gtl:typeid",
        "//gloop/util/symbolize:demangle",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@gloop//gloop:enforce_gloop_support",
        "@protobuf",
        "@protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "streamable_test",
    size = "small",
    srcs = ["streamable_test.cc"],
    deps = [
        ":streamable",
        ":streamable_test_cc_proto",
        "//gloop/util/gtl/extend",
        "//gloop/util/tuple:struct",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

proto_library(
    name = "streamable_test_proto",
    testonly = 1,
    srcs = ["streamable_test.proto"],
    option_deps = ["@protobuf//:cpp_features_proto"],
    visibility = ["//visibility:private"],
)

cc_proto_library(
    name = "streamable_test_cc_proto",
    testonly = 1,
    deps = [":streamable_test_proto"],
)

cc_library(
    name = "rank",
    srcs = ["rank.cc"],
    hdrs = ["rank.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "rank_test",
    size = "small",
    srcs = ["rank_test.cc"],
    deps = [
        ":rank",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "internal_preprocessor",
    hdrs = ["internal_preprocessor.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "internal_preprocessor_test",
    size = "small",
    srcs = ["internal_preprocessor_test.cc"],
    deps = [
        ":internal_preprocessor",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "dump_vars",
    hdrs = ["dump_vars.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/util/tuple/components:internal_preprocessor",
        "//gloop/util/tuple/components:streamable",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:structured",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "dump_vars_test",
    size = "small",
    srcs = ["dump_vars_test.cc"],
    deps = [
        ":dump_vars",
        ":streamable_test_cc_proto",
        "//gloop/util/tuple/components:streamable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
