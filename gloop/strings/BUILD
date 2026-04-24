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
    features = [
        "parse_headers",
        "header_modules",
    ],
)

cc_library(
    name = "arena-string",
    hdrs = ["arena-string.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:arena",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "arena-string_test",
    size = "small",
    srcs = ["arena-string_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":arena-string",
        "//gloop/base:arena",
        "//gloop/base:init_google",
        "//gloop/util/random:distributions",
        "//gloop/util/random:mt_random",
        "//gloop/util/random:random_base",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "bytestream",
    hdrs = ["bytestream.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "case",
    srcs = ["case.cc"],
    hdrs = ["case.h"],
    deps = [
        ":memutil",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "case_test",
    size = "small",
    srcs = ["case_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":case",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "case_benchmark",
    size = "small",
    srcs = [
        "case_benchmark.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        ":case",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "cord_bytestream",
    hdrs = ["cord_bytestream.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:cord",
    ],
)

cc_library(
    name = "escaping",
    srcs = ["escaping.cc"],
    hdrs = ["escaping.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:internal",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "escaping_test",
    size = "small",
    srcs = ["escaping_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":escaping",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "hex_ascii_dump",
    srcs = ["hex_ascii_dump.cc"],
    hdrs = ["hex_ascii_dump.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "hex_ascii_dump_test",
    size = "small",
    srcs = ["hex_ascii_dump_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":hex_ascii_dump",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "host_port",
    srcs = ["host_port.cc"],
    hdrs = ["host_port.h"],
    deps = [
        ":numbers",
        ":util",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "host_port_test",
    size = "small",
    srcs = ["host_port_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":host_port",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "human_readable",
    srcs = ["human_readable.cc"],
    hdrs = ["human_readable.h"],
    deps = [
        ":si_prefix",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "human_readable_test",
    size = "small",
    srcs = ["human_readable_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":human_readable",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "interntable_benchmark",
    testonly = 1,
    srcs = ["interntable_benchmark.cc"],
    hdrs = ["interntable_benchmark.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "keyed_intern_table",
    hdrs = ["keyed_intern_table.h"],
    deps = [
        ":arena-string",
        "//gloop:enforce_gloop_support",
        "//gloop/base:arena",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "keyed_intern_table_test",
    size = "small",
    srcs = ["keyed_intern_table_test.cc"],
    deps = [
        ":interntable_benchmark",
        ":keyed_intern_table",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:stl_util",
        "//gloop/util/intops",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "memblock",
    srcs = ["memblock.cc"],
    hdrs = ["memblock.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:config",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:container_memory",
        "@abseil-cpp//absl/container:layout",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "memblock_unittest",
    size = "small",
    srcs = ["memblock_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":memblock",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:cord_test_helpers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "memutil",
    srcs = ["memutil.cc"],
    hdrs = ["memutil.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "memutil_unittest",
    size = "small",
    srcs = ["memutil_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":memutil",
        "//gloop/base:init_google",
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
    name = "numberformat",
    srcs = ["numberformat.cc"],
    hdrs = ["numberformat.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/math:fastmath",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "numberformat_unittest",
    size = "small",
    srcs = ["numberformat_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":numberformat",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log:check",
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
        "//gloop:enforce_gloop_support",
        "//gloop/base:fprint",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "ostringstream",
    srcs = ["ostringstream.cc"],
    hdrs = ["ostringstream.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "ostringstream_test",
    size = "small",
    srcs = ["ostringstream_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":ostringstream",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "serialize",
    srcs = ["serialize.cc"],
    hdrs = ["serialize.h"],
    deps = [
        ":numbers",
        "//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/strings:string_view",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "si_prefix",
    srcs = ["si_prefix.cc"],
    hdrs = ["si_prefix.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/math:mathutil",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:optional",
    ],
)

cc_test(
    name = "si_prefix_test",
    size = "small",
    srcs = ["si_prefix_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":si_prefix",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:optional",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "split",
    srcs = ["split.cc"],
    hdrs = ["split.h"],
    deps = [
        ":util",
        "//gloop:enforce_gloop_support",
        "//gloop/base:strtoint",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "split_benchmark",
    size = "small",
    srcs = ["split_benchmark.cc"],
    deps = [
        ":split",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "split_test",
    srcs = ["split_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":numbers",
        ":split",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "strip",
    srcs = ["strip.cc"],
    hdrs = ["strip.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:charset",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "strip_test",
    size = "small",
    srcs = ["strip_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":strip",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "string_view_utils",
    srcs = ["string_view_utils.cc"],
    hdrs = ["string_view_utils.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "string_view_utils_test",
    size = "medium",
    srcs = ["string_view_utils_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":string_view_utils",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@re2",
    ],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        ":string_view_utils",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
