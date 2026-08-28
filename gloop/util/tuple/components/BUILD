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
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "ignore_index_test",
    size = "small",
    srcs = ["ignore_index_test.cc"],
    deps = [
        ":ignore_index",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "intrinsics",
    hdrs = ["intrinsics.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "intrinsics_test",
    size = "small",
    srcs = ["intrinsics_test.cc"],
    deps = [
        ":intrinsics",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "std_tuple",
    hdrs = ["std_tuple.h"],
    deps = [
        ":intrinsics",
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "std_tuple_test",
    size = "small",
    srcs = ["std_tuple_test.cc"],
    deps = [
        ":intrinsics",
        ":std_tuple",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "pair",
    hdrs = ["pair.h"],
    deps = [
        ":intrinsics",
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "pair_test",
    size = "small",
    srcs = ["pair_test.cc"],
    deps = [
        ":intrinsics",
        ":pair",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "array",
    hdrs = ["array.h"],
    deps = [
        ":intrinsics",
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "array_test",
    size = "small",
    srcs = ["array_test.cc"],
    deps = [
        ":array",
        ":intrinsics",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "compressed_tuple",
    hdrs = ["compressed_tuple.h"],
    deps = [
        ":intrinsics",
        "//gloop:enforce_gloop_support",
        "//gloop/util/gtl:compressed_tuple",
    ],
)

cc_test(
    name = "compressed_tuple_test",
    srcs = ["compressed_tuple_test.cc"],
    deps = [
        ":compressed_tuple",
        ":intrinsics",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:compressed_tuple",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
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
        "//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "iterate",
    hdrs = ["iterate.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ignore_index",
        ":internal_iterate",
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "iterate_test",
    size = "small",
    srcs = ["iterate_test.cc"],
    deps = [
        ":iterate",
        "//gloop:gloop_test_main",
        "//gloop/util/tuple:test_util",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
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
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "for_each_test",
    size = "small",
    srcs = ["for_each_test.cc"],
    deps = [
        ":for_each",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "streamable",
    srcs = ["streamable.cc"],
    hdrs = ["streamable.h"],
    defines = select({
        "//conditions:default": [
            "UTIL_TUPLE_COMPONENTS_HAVE_UTIL_SYMBOLIZE",
        ],
    }) + ["UTIL_TUPLE_COMPONENTS_HAVE_NET_PROTO2"],
    implementation_deps = ["@abseil-cpp//absl/log:check"],
    visibility = ["//visibility:public"],
    deps = [
        ":for_each",
        ":intrinsics",
        ":rank",
        "//gloop:enforce_gloop_support",
        "//gloop/strings:ostringstream",
        "//gloop/util/gtl:typeid",
        "//gloop/util/status",
        "//gloop/util/symbolize:demangle",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:source_location",
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
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:iterator_range",
        "//gloop/util/gtl/extend",
        "//gloop/util/gtl/extend:debug_printing",
        "//gloop/util/tuple:struct",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop/util/status:codes_cc_proto",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
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
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "rank_test",
    size = "small",
    srcs = ["rank_test.cc"],
    deps = [
        ":rank",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "internal_preprocessor",
    hdrs = ["internal_preprocessor.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "internal_preprocessor_test",
    size = "small",
    srcs = ["internal_preprocessor_test.cc"],
    deps = [
        ":internal_preprocessor",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "dump_vars",
    hdrs = ["dump_vars.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":internal_preprocessor",
        ":streamable",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:structured",
    ],
)

cc_test(
    name = "dump_vars_test",
    size = "small",
    srcs = ["dump_vars_test.cc"],
    deps = [
        ":dump_vars",
        ":streamable",
        ":streamable_test_cc_proto",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
