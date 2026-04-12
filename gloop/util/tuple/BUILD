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
        "@gloop//gloop:enforce_gloop_support",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "ignore_index",
    hdrs = ["ignore_index.h"],
    visibility = [":util-tuple"],
    deps = [
        "//gloop/util/tuple/components:ignore_index",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "intrinsics",
    hdrs = ["intrinsics.h"],
    deps = [
        "//gloop/util/tuple/components:intrinsics",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "std_tuple",
    hdrs = ["std_tuple.h"],
    deps = [
        "//gloop/util/tuple/components:std_tuple",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "pair",
    hdrs = ["pair.h"],
    deps = [
        "//gloop/util/tuple/components:pair",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "array",
    hdrs = ["array.h"],
    deps = [
        "//gloop/util/tuple/components:array",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "internal_accumulate",
    textual_hdrs = ["internal_accumulate.h"],
    visibility = [":util-tuple"],
    deps = [
        ":intrinsics",
        "@gloop//gloop:enforce_gloop_support",
    ],
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
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "for_each",
    hdrs = ["for_each.h"],
    deps = [
        "//gloop/util/tuple/components:for_each",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "int_pack",
    hdrs = ["int_pack.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "int_pack_test",
    size = "small",
    srcs = ["int_pack_test.cc"],
    deps = [
        ":int_pack",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "apply_test",
    size = "small",
    srcs = ["apply_test.cc"],
    deps = [
        ":apply",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "generate",
    hdrs = ["generate.h"],
    deps = [
        ":ignore_index",
        ":int_pack",
        ":intrinsics",
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "back",
    hdrs = ["back.h"],
    deps = [
        ":intrinsics",
        ":std_tuple",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "back_test",
    size = "small",
    srcs = ["back_test.cc"],
    deps = [
        ":back",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "slice_test",
    size = "small",
    srcs = ["slice_test.cc"],
    deps = [
        ":slice",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "filter",
    hdrs = ["filter.h"],
    deps = [
        ":array",
        ":int_pack",
        ":intrinsics",
        ":pair",
        ":slice",
        ":std_tuple",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "filter_test",
    size = "small",
    srcs = ["filter_test.cc"],
    deps = [
        ":filter",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "erase_test",
    size = "small",
    srcs = ["erase_test.cc"],
    deps = [
        ":erase",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "pop_back",
    hdrs = ["pop_back.h"],
    deps = [
        ":erase",
        ":intrinsics",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "pop_back_test",
    size = "small",
    srcs = ["pop_back_test.cc"],
    deps = [
        ":pop_back",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "insert_test",
    size = "small",
    srcs = ["insert_test.cc"],
    deps = [
        ":insert",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "streamable",
    hdrs = ["streamable.h"],
    deps = [
        "//gloop/util/tuple/components:streamable",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "push_front",
    hdrs = ["push_front.h"],
    deps = [
        ":insert",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "push_front_test",
    size = "small",
    srcs = ["push_front_test.cc"],
    deps = [
        ":push_front",
        ":test_util",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "compile_string",
    hdrs = ["compile_string.h"],
    deps = [
        "@abseil-cpp//absl/utility",
        "@gloop//gloop:enforce_gloop_support",
    ],
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
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "relational_test",
    size = "small",
    srcs = ["relational_test.cc"],
    deps = [
        ":relational",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "swap_test",
    size = "small",
    srcs = ["swap_test.cc"],
    deps = [
        ":swap",
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
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
        "@gloop//gloop:gloop_test_main",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "is_tuple_test",
    size = "small",
    srcs = ["is_tuple_test.cc"],
    deps = [
        ":is_tuple",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_library(
    name = "dump_vars",
    hdrs = ["dump_vars.h"],
    deps = [
        "//gloop/util/tuple/components:dump_vars",
        "@gloop//gloop:enforce_gloop_support",
    ],
)
