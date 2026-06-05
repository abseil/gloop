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

# This package is part of Components, and generally cannot be modified in the
# same changelist as other packages. See <link> for details.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# This package is part of Components, and generally cannot be modified in the
# same changelist as other packages. See <link> for details.

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
    features = [
        "parse_headers",
        "header_modules",
    ],
)

cc_library(
    name = "hash",
    srcs = [
        "hash.cc",
        "hasher.cc",
    ],
    hdrs = [
        "hash.h",
        "hasher.h",
    ],
    textual_hdrs = [
        "builtin_type_hash.h",  # depend on :builtin_type_hash instead
        "hash128to64.h",  # depend on :hash128to64 instead
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":builtin_type_hash",
        ":city",
        ":hash128to64",
        ":jenkins",
        ":string_hash",
        "//gloop:enforce_gloop_support",
        "//gloop/base:port",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

# Define select clauses which should be consistent across every hasher.
OPTIMIZATION_SELECTOR = {
    "//conditions:default": ["-O3"],
}

cc_library(
    name = "seeds",
    srcs = ["seeds.cc"],
    hdrs = ["seeds.h"],
    visibility = ["//visibility:private"],
    deps = [
        "//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "farmhash",
    srcs = ["farmhashte.cc"],
    hdrs = [
        "farmhash.h",
        "farmhashte.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/endian",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/numeric:int128",
    ],
)

cc_test(
    name = "farmhashte_test",
    size = "small",
    timeout = "long",
    srcs = ["farmhashte_test.cc"],
    deps = [
        ":farmhash",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "builtin_type_hash",
    hdrs = ["builtin_type_hash.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":jenkins",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_library(
    name = "string_hash",
    hdrs = ["string_hash.h"],
    visibility = [
        "//gloop/util/hash:__subpackages__",
    ],
    deps = [
        ":city",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "legacy_hash",
    hdrs = ["legacy_hash.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":builtin_type_hash",
        ":hash",
        ":jenkins",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "legacy_hash_cord",
    srcs = ["legacy_hash_cord.cc"],
    hdrs = ["legacy_hash_cord.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":hash",
        ":jenkins",
        ":seeds",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "jenkins",
    srcs = ["jenkins.cc"],
    hdrs = [
        "jenkins.h",
        "jenkins_lookup2.h",
    ],
    textual_hdrs = [
        "builtin_type_hash.h",  # depend on :builtin_type_hash instead
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:port",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "hash_unittest",
    size = "small",
    srcs = ["hash_unittest.cc"],
    copts = [
        "$(STACK_FRAME_UNLIMITED)",  # hash_unittest.cc
    ] + select({
        # Include the benchmarks only for prod builds.
        "//conditions:default": [],
    }),
    linkstatic = 1,
    deps = [
        ":builtin_type_hash",
        ":farmhash",
        ":hash",
        ":jenkins",
        ":legacy_hash",
        ":string_hash",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:cord_test_helpers",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "legacy_hash_test",
    size = "small",
    srcs = ["legacy_hash_test.cc"],
    deps = [
        ":builtin_type_hash",
        ":legacy_hash",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "legacy_hash_cord_test",
    size = "small",
    srcs = ["legacy_hash_cord_test.cc"],
    deps = [
        ":hash",
        ":legacy_hash_cord",
        ":murmur",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:cord_test_helpers",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "hash128to64",
    textual_hdrs = ["hash128to64.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/numeric:int128",
    ],
)

cc_test(
    name = "cityhash128_test",
    size = "small",
    srcs = ["cityhash128_test.cc"],
    deps = [
        ":city",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "city",
    srcs = ["city.cc"],
    hdrs = ["city.h"],
    copts = select(OPTIMIZATION_SELECTOR),
    textual_hdrs = ["hash128to64.h"],  # depend on :hash128to64 instead
    visibility = ["//visibility:public"],
    deps = [
        ":builtin_type_hash",
        ":farmhash",
        ":hash128to64",
        "//gloop:enforce_gloop_support",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "fingerprint2011",
    srcs = ["fingerprint2011.cc"],
    hdrs = ["fingerprint2011.h"],
    copts = ["-O3"],
    visibility = ["//visibility:public"],
    deps = [
        ":murmur",
        "//gloop:enforce_gloop_support",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "fingerprint2011_test_lib",
    testonly = True,
    srcs = ["fingerprint2011_test.cc"],
    deps = [
        ":fingerprint2011",
        ":hash",
        "//gloop:enforce_gloop_support",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
    alwayslink = True,
)

cc_test(
    name = "fingerprint2011_test",
    size = "small",
    deps = [
        ":fingerprint2011_test_lib",
    ],
)

cc_library(
    name = "murmur",
    srcs = ["murmur.cc"],
    hdrs = ["murmur.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
)

# Simple tests for murmur hashing, with some benchmarks.
cc_test(
    name = "murmur_test",
    size = "large",
    srcs = ["murmur_test.cc"],
    deps = [
        ":murmur",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Even more tests for murmur hashing.
cc_test(
    name = "murmur_large_test",
    size = "large",
    srcs = ["murmur_large_test.cc"],
    shard_count = 4,
    deps = [
        ":murmur",
        "//gloop:gloop_test_main",
        "//gloop/util/endian",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "crc",
    srcs = [
        "crc.cc",
        "crc_internal.h",
        "crc_poly.cc",
        "crc_poly_arm.inc",
        "crc_poly_x86.inc",
        "crc_powerpc.h",
        "crc_x86_arm_combined.cc",
    ],
    hdrs = ["crc.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/crc:crc32c",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "crc_concat_test",
    size = "small",
    srcs = ["crc_concat_test.cc"],
    deps = [
        ":crc",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "crc_unittest",
    size = "medium",
    srcs = ["crc_unittest.cc"],
    deps = [
        ":crc",
        "//gloop/base:init_google",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "crc32c_x86_arm_combined_test",
    size = "small",
    srcs = [
        "crc32c_x86_arm_combined_test.cc",
        "crc_internal.h",
    ],
    deps = [
        ":crc",
        "//gloop:gloop_test_main",
        "//gloop/util/bits",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "transparent_hash",
    hdrs = ["transparent_hash.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":string_hash",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "transparent_hash_test",
    srcs = ["transparent_hash_test.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":transparent_hash",
        "//gloop:gloop_test_main",
        "//gloop/base:arena",
        "//gloop/strings:arena-string",
        "//gloop/util/gtl:stl_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
