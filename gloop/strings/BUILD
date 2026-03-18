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

load(
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "escaping",
    srcs = ["escaping.cc"],
    hdrs = ["escaping.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:internal",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "escaping_test",
    size = "small",
    srcs = ["escaping_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":escaping",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "host_port",
    srcs = ["host_port.cc"],
    hdrs = ["host_port.h"],
    deps = [
        ":numbers",
        ":util",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "host_port_test",
    size = "small",
    srcs = ["host_port_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":host_port",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "human_readable",
    srcs = ["human_readable.cc"],
    hdrs = ["human_readable.h"],
    deps = [
        ":si_prefix",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "human_readable_test",
    size = "small",
    srcs = ["human_readable_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":human_readable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "memutil",
    srcs = ["memutil.cc"],
    hdrs = ["memutil.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "memutil_unittest",
    size = "small",
    srcs = ["memutil_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":memutil",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "numbers",
    srcs = ["numbers.cc"],
    hdrs = ["numbers.h"],
    deps = [
        ":string_view_utils",
        ":util",
        "//gloop/base:fprint",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "numbers_test",
    size = "small",
    srcs = [
        "numbers_test.cc",
        "numbers_test_common.h",
    ],
    visibility = ["//visibility:private"],
    deps = [
        ":numbers",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "numbers_benchmark",
    size = "small",
    srcs = [
        "numbers_benchmark.cc",
        "numbers_test_common.h",
    ],
    visibility = ["//visibility:private"],
    deps = [
        ":numbers",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ostringstream",
    srcs = ["ostringstream.cc"],
    hdrs = ["ostringstream.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "ostringstream_test",
    size = "small",
    srcs = ["ostringstream_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":ostringstream",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "serialize",
    srcs = ["serialize.cc"],
    hdrs = ["serialize.h"],
    deps = [
        ":numbers",
        "//gloop/util/endian",
        "//gloop/util/gtl:stl_util",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "serialize_test",
    size = "small",
    srcs = ["serialize_test.cc"],
    shard_count = 10,
    visibility = ["//visibility:private"],
    deps = [
        ":serialize",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    # Separated from serialize_test for non-ASCL features.
    name = "serialize_hash_test",
    size = "small",
    srcs = ["serialize_hash_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":serialize",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "si_prefix",
    srcs = ["si_prefix.cc"],
    hdrs = ["si_prefix.h"],
    deps = [
        "//gloop/util/math:mathutil",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:optional",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "si_prefix_test",
    size = "small",
    srcs = ["si_prefix_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":si_prefix",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:optional",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "split",
    srcs = ["split.cc"],
    hdrs = ["split.h"],
    deps = [
        ":util",
        "//gloop/base:strtoint",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "split_benchmark",
    size = "small",
    srcs = ["split_benchmark.cc"],
    deps = [
        ":split",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "split_test",
    srcs = ["split_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":numbers",
        ":split",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "strip",
    srcs = ["strip.cc"],
    hdrs = ["strip.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "strip_test",
    size = "small",
    srcs = ["strip_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":strip",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "string_view_utils",
    srcs = ["string_view_utils.cc"],
    hdrs = ["string_view_utils.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "string_view_utils_test",
    size = "medium",
    srcs = ["string_view_utils_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":string_view_utils",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@re2",
    ],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        ":string_view_utils",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "util_test",
    size = "small",
    srcs = ["util_test.cc"],
    data = [
        "testdata/getline-1.txt",
        "testdata/getline-2.txt",
    ],
    deps = [
        ":util",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
