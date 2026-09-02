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

package_group(
    name = "util-tuple",
    packages = [
        "//gloop/util/tuple/...",
    ],
)

cc_library(
    name = "test_util",
    testonly = 1,
    hdrs = ["test_util.h"],
    visibility = [":util-tuple"],
    deps = [
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "ignore_index",
    hdrs = ["ignore_index.h"],
    visibility = [":util-tuple"],
    deps = ["//gloop/util/tuple/components:ignore_index"],
)

cc_library(
    name = "intrinsics",
    hdrs = ["intrinsics.h"],
    deps = ["//gloop/util/tuple/components:intrinsics"],
)

cc_library(
    name = "std_tuple",
    hdrs = ["std_tuple.h"],
    deps = ["//gloop/util/tuple/components:std_tuple"],
)

cc_library(
    name = "pair",
    hdrs = ["pair.h"],
    deps = ["//gloop/util/tuple/components:pair"],
)

cc_library(
    name = "array",
    hdrs = ["array.h"],
    deps = ["//gloop/util/tuple/components:array"],
)

cc_library(
    name = "internal_accumulate",
    textual_hdrs = ["internal_accumulate.h"],
    visibility = ["//visibility:private"],
    deps = [":intrinsics"],
)

cc_library(
    name = "accumulate",
    hdrs = ["accumulate.h"],
    deps = [
        ":array",
        ":internal_accumulate",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "accumulate_test",
    size = "small",
    srcs = ["accumulate_test.cc"],
    deps = [
        ":accumulate",
        ":int_pack",
        ":push_front",
        ":test_util",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "for_each",
    hdrs = ["for_each.h"],
    deps = ["//gloop/util/tuple/components:for_each"],
)

cc_library(
    name = "int_pack",
    hdrs = ["int_pack.h"],
)

cc_test(
    name = "int_pack_test",
    size = "small",
    srcs = ["int_pack_test.cc"],
    deps = [
        ":int_pack",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "apply",
    hdrs = ["apply.h"],
    deps = [
        ":array",
        ":back",
        ":cat",
        ":int_pack",
        ":intrinsics",
        ":pair",
        ":pop_back",
        ":ref",
        ":std_tuple",
    ],
)

cc_test(
    name = "apply_test",
    size = "small",
    srcs = ["apply_test.cc"],
    deps = [
        ":apply",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "generate",
    hdrs = ["generate.h"],
    deps = [
        ":ignore_index",
        ":int_pack",
        ":intrinsics",
    ],
)

cc_test(
    name = "generate_test",
    size = "small",
    srcs = ["generate_test.cc"],
    deps = [
        ":array",
        ":generate",
        ":pair",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "back",
    hdrs = ["back.h"],
    deps = [
        ":intrinsics",
        ":std_tuple",
    ],
)

cc_test(
    name = "back_test",
    size = "small",
    srcs = ["back_test.cc"],
    deps = [
        ":back",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "slice",
    hdrs = ["slice.h"],
    deps = [
        ":array",
        ":generate",
        ":ignore_index",
        ":int_pack",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "slice_test",
    size = "small",
    srcs = ["slice_test.cc"],
    deps = [
        ":slice",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "filter",
    hdrs = ["filter.h"],
    deps = [
        ":array",
        ":ignore_index",
        ":int_pack",
        ":intrinsics",
        ":pair",
        ":slice",
        ":std_tuple",
    ],
)

cc_test(
    name = "filter_test",
    size = "small",
    srcs = ["filter_test.cc"],
    deps = [
        ":filter",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "erase",
    hdrs = ["erase.h"],
    deps = [
        ":filter",
        ":ignore_index",
        ":int_pack",
        ":intrinsics",
        ":slice",
    ],
)

cc_test(
    name = "erase_test",
    size = "small",
    srcs = ["erase_test.cc"],
    deps = [
        ":erase",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "pop_back",
    hdrs = ["pop_back.h"],
    deps = [
        ":erase",
        ":intrinsics",
    ],
)

cc_test(
    name = "pop_back_test",
    size = "small",
    srcs = ["pop_back_test.cc"],
    deps = [
        ":pop_back",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "cat",
    hdrs = ["cat.h"],
    deps = [
        ":array",
        ":generate",
        ":int_pack",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "cat_test",
    size = "small",
    srcs = ["cat_test.cc"],
    deps = [
        ":cat",
        ":std_tuple",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "insert",
    hdrs = ["insert.h"],
    deps = [
        ":array",
        ":cat",
        ":intrinsics",
        ":pair",
        ":slice",
        ":std_tuple",
    ],
)

cc_test(
    name = "insert_test",
    size = "small",
    srcs = ["insert_test.cc"],
    deps = [
        ":insert",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "streamable",
    hdrs = ["streamable.h"],
    deps = ["//gloop/util/tuple/components:streamable"],
)

cc_library(
    name = "push_front",
    hdrs = ["push_front.h"],
    deps = [":insert"],
)

cc_test(
    name = "push_front_test",
    size = "small",
    srcs = ["push_front_test.cc"],
    deps = [
        ":push_front",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "ref",
    hdrs = ["ref.h"],
    deps = [
        ":array",
        ":generate",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "ref_test",
    size = "small",
    srcs = ["ref_test.cc"],
    deps = [
        ":ref",
        ":std_tuple",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "zip",
    hdrs = ["zip.h"],
    deps = [
        ":generate",
        ":int_pack",
        ":intrinsics",
        ":ref",
        ":std_tuple",
    ],
)

cc_test(
    name = "zip_test",
    size = "small",
    srcs = ["zip_test.cc"],
    deps = [
        ":pair",
        ":std_tuple",
        ":struct",
        ":test_util",
        ":zip",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "compile_string",
    hdrs = ["compile_string.h"],
    deps = ["@abseil-cpp//absl/utility"],
)

cc_library(
    name = "struct",
    hdrs = ["struct.h"],
    deps = [
        ":apply",
        ":compile_string",
        ":intrinsics",
        ":relational",
        ":streamable",
        ":swap",
        "//gloop/util/tuple/bindings",
        "//gloop/util/tuple/components:internal_preprocessor",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "struct_test",
    size = "small",
    srcs = ["struct_test.cc"],
    deps = [
        ":accumulate",
        ":compile_string",
        ":intrinsics",
        ":matchers",
        ":struct",
        ":test_util",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "relational",
    hdrs = ["relational.h"],
    deps = [
        ":accumulate",
        ":intrinsics",
        ":ref",
        ":std_tuple",
        ":zip",
    ],
)

cc_test(
    name = "relational_test",
    size = "small",
    srcs = ["relational_test.cc"],
    deps = [
        ":relational",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "swap",
    hdrs = ["swap.h"],
    deps = [
        ":for_each",
        ":intrinsics",
        ":ref",
        ":std_tuple",
        ":zip",
    ],
)

cc_test(
    name = "swap_test",
    size = "small",
    srcs = ["swap_test.cc"],
    deps = [
        ":swap",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "matchers",
    testonly = 1,
    hdrs = ["matchers.h"],
    deps = [
        ":for_each",
        ":intrinsics",
        ":is_tuple",
        "//gloop/util/tuple/bindings",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "matchers_test",
    size = "small",
    srcs = ["matchers_test.cc"],
    deps = [
        ":intrinsics",
        ":matchers",
        ":struct",
        "@abseil-cpp//absl/base",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "is_tuple",
    hdrs = ["is_tuple.h"],
    deps = [
        ":array",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "is_tuple_test",
    size = "small",
    srcs = ["is_tuple_test.cc"],
    deps = [
        ":is_tuple",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "dump_vars",
    hdrs = ["dump_vars.h"],
    deps = ["//gloop/util/tuple/components:dump_vars"],
)

cc_library(
    name = "all_of",
    hdrs = ["all_of.h"],
    deps = [":accumulate"],
)

cc_test(
    name = "all_of_test",
    size = "small",
    srcs = ["all_of_test.cc"],
    deps = [
        ":all_of",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "any_of",
    hdrs = ["any_of.h"],
    deps = [":accumulate"],
)

cc_test(
    name = "any_of_test",
    size = "small",
    srcs = ["any_of_test.cc"],
    deps = [
        ":any_of",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "compressed_tuple",
    hdrs = ["compressed_tuple.h"],
    deps = ["//gloop/util/tuple/components:compressed_tuple"],
)

cc_library(
    name = "construct",
    hdrs = ["construct.h"],
    deps = [":apply"],
)

cc_test(
    name = "construct_test",
    size = "small",
    srcs = ["construct_test.cc"],
    deps = [
        ":construct",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "find",
    hdrs = ["find.h"],
    deps = [":find_if"],
)

cc_library(
    name = "find_if",
    hdrs = ["find_if.h"],
    deps = [
        ":array",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "find_test",
    size = "small",
    srcs = ["find_test.cc"],
    deps = [
        ":find",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "find_if_test",
    size = "small",
    srcs = ["find_if_test.cc"],
    deps = [
        ":find_if",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "front",
    hdrs = ["front.h"],
    deps = [
        ":array",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "front_test",
    size = "small",
    srcs = ["front_test.cc"],
    deps = [
        ":front",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "fuse",
    hdrs = ["fuse.h"],
    deps = [":apply"],
)

cc_test(
    name = "fuse_test",
    size = "small",
    srcs = ["fuse_test.cc"],
    deps = [
        ":fuse",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "join",
    hdrs = ["join.h"],
    deps = [
        ":array",
        ":cat",
        ":int_pack",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "join_test",
    size = "small",
    srcs = ["join_test.cc"],
    deps = [
        ":join",
        ":pair",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "none_of",
    hdrs = ["none_of.h"],
    deps = [":accumulate"],
)

cc_test(
    name = "none_of_test",
    size = "small",
    srcs = ["none_of_test.cc"],
    deps = [
        ":none_of",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "pop_front",
    hdrs = ["pop_front.h"],
    deps = [":erase"],
)

cc_test(
    name = "pop_front_test",
    size = "small",
    srcs = ["pop_front_test.cc"],
    deps = [
        ":pop_front",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "push_back",
    hdrs = ["push_back.h"],
    deps = [
        ":insert",
        ":intrinsics",
    ],
)

cc_test(
    name = "push_back_test",
    size = "small",
    srcs = ["push_back_test.cc"],
    deps = [
        ":push_back",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "reverse",
    hdrs = ["reverse.h"],
    deps = [
        ":array",
        ":generate",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "reverse_test",
    size = "small",
    srcs = ["reverse_test.cc"],
    deps = [
        ":reverse",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "rotate",
    hdrs = ["rotate.h"],
    deps = [
        ":cat",
        ":intrinsics",
        ":slice",
    ],
)

cc_test(
    name = "rotate_test",
    size = "small",
    srcs = ["rotate_test.cc"],
    deps = [
        ":matchers",
        ":pair",
        ":rotate",
        ":test_util",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "string_hash_map",
    hdrs = ["string_hash_map.h"],
    deps = [
        ":intrinsics",
        "//gloop/util/gtl:string_hash_map",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "string_hash_map_test",
    size = "small",
    srcs = ["string_hash_map_test.cc"],
    deps = [
        ":intrinsics",
        ":string_hash_map",
        "//gloop/util/gtl:string_hash_map",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "transform",
    hdrs = ["transform.h"],
    deps = [
        ":array",
        ":generate",
        ":ignore_index",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "transform_test",
    size = "small",
    srcs = ["transform_test.cc"],
    deps = [
        ":std_tuple",
        ":test_util",
        ":transform",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "unfuse",
    hdrs = ["unfuse.h"],
)

cc_test(
    name = "unfuse_test",
    size = "small",
    srcs = ["unfuse_test.cc"],
    deps = [
        ":unfuse",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "unref",
    hdrs = ["unref.h"],
    deps = [
        ":array",
        ":generate",
        ":intrinsics",
        ":pair",
        ":std_tuple",
    ],
)

cc_test(
    name = "unref_test",
    size = "small",
    srcs = ["unref_test.cc"],
    deps = [
        ":test_util",
        ":unref",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
