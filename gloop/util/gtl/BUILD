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
    name = "comparator",
    hdrs = ["comparator.h"],
    deps = [
        ":compressed_tuple",
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "comparator_test",
    size = "small",
    srcs = ["comparator_test.cc"],
    deps = [
        ":comparator",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "compressed_tuple",
    hdrs = ["compressed_tuple.h"],
    deps = [
        "@abseil-cpp//absl/container:compressed_tuple",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "compressed_tuple_test",
    size = "small",
    srcs = ["compressed_tuple_test.cc"],
    deps = [
        ":compressed_tuple",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "flat_internal",
    hdrs = ["flat_internal.h"],
    visibility = [
        "//gloop/util/gtl:__pkg__",
    ],
    deps = [
        ":unchecked_tags",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/utility",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "flat_internal_test",
    srcs = ["flat_internal_test.cc"],
    deps = [
        ":flat_internal",
        ":requires",
        "@abseil-cpp//absl/strings:cord",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "flat_set",
    hdrs = ["flat_set.h"],
    deps = [
        ":flat_internal",
        ":heterogeneous_lookup",
        ":unchecked_tags",
        "@abseil-cpp//absl/meta:type_traits",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "flat_set_test",
    srcs = ["flat_set_test.cc"],
    deps = [
        ":comparator",
        ":flat_set",
        ":stl_util",
        ":switch",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "heterogeneous_lookup",
    hdrs = ["heterogeneous_lookup.h"],
    visibility = [
        "//gloop/util/gtl:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/container:common",
        "@gloop//gloop:enforce_gloop_support",
    ],
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
    name = "switch",
    hdrs = ["switch.h"],
    deps = [
        "@abseil-cpp//absl/utility",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "switch_test",
    size = "small",
    srcs = ["switch_test.cc"],
    deps = [
        ":switch",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "unchecked_tags",
    hdrs = ["unchecked_tags.h"],
    visibility = [
        "//gloop/util/gtl:__pkg__",
    ],
    deps = ["@gloop//gloop:enforce_gloop_support"],
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
