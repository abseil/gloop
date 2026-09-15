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

# Libraries in this low-level package may not depend on libraries in packages
# that are not low level.   For more information, including how to submit
# changes to this file, see    <link>

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

# Description:
#
# This package implements various compression algorithms.
# The "coding" library includes various low-level compression routines.
# The "vcdiff" library is an implementation of the Bentley-McIlroy
#   compression technique for long strings.
# The "bmdiff" library is a simpler implementation of the Bentley-McIlroy
#   compression technique for long strings that omits zlib based compression
#   and just computes a plain diff.
# The "compressed-array" library provides an interface for building
#   and dereferencing compressed uint32 arrays.
# The "shrunk-array" library provides an interface for shrinking and
#   then reading arrays of uint8, uint16, uint32, or uint64.  Whereas
#   compressed-array is best suited for compressing a few large arrays,
#   shrunk-array is designed to accommodate many small independent
#   arrays, and offers faster compression and faster random-access
#   reads, even when jumping between different arrays.

cc_library(
    name = "coding",
    srcs = [
        "bitcoding.cc",
        "coder.cc",
        "huffcoding.cc",
        "two-values-varint.cc",
        "varint.cc",
    ],
    hdrs = [
        "bitcoding.h",
        "coder.h",
        "huffcoding.h",
        "tablecoding.h",
        "two-values-varint.h",
        "varint.h",
    ],
    implementation_deps = [
        "//gloop/util/gtl:stl_util",
        "//gloop/util/hash",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:uword",
        "//gloop/util/bits",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "coder_unittest",
    srcs = ["coder_unittest.cc"],
    deps = [
        ":coding",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:unique_array",
        "//gloop/util/random:acmrandom",
        "//gloop/util/random:distributions",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "bitcoding_unittest",
    size = "small",
    timeout = "moderate",
    srcs = ["bitcoding_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":coding",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:unique_array",
        "//gloop/util/random:acmrandom",
        "//gloop/util/random:distributions",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "huffcoding_unittest",
    srcs = ["huffcoding_unittest.cc"],
    deps = [
        ":coding",
        "//gloop:gloop_test_main",
        "//gloop/util/endian",
        "//gloop/util/gtl:unique_array",
        "//gloop/util/hash",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Linked statically to make it easier to copy to production machines
# for timing tests and for more realistic measurement of benchmarks
cc_test(
    name = "varint_unittest",
    srcs = ["varint_unittest.cc"],
    copts = ["$(STACK_FRAME_UNLIMITED)"],
    linkstatic = 1,
    deps = [
        ":coding",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:unique_array",
        "//gloop/util/random:acmrandom",
        "//gloop/util/random:distributions",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Linked statically to make it easier to copy to production machines
# for timing tests and for more realistic measurement of benchmarks
cc_test(
    name = "two-values-varint_test",
    srcs = ["two-values-varint_test.cc"],
    copts = ["$(STACK_FRAME_UNLIMITED)"],
    linkstatic = 1,
    # Guarantees changelist stability for the random number generators.
    # See <link>.
    stamp = 1,
    deps = [
        ":coding",
        "//gloop:gloop_test_main",
        "//gloop/util/random:distributions",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "shrunk-array",
    srcs = ["shrunk-array.cc"],
    hdrs = ["shrunk-array.h"],
    # b/119699200 -- this file is a dependency of almost all binaries (via
    # util/sybmolize), and the speed matters. Build it with optimization
    # even in non-opt modes.
    # Note: if you need to debug this file and would like to disable
    # optimization, use --per_file_copt=util/coding@-O0.
    copts = ["-O3"],
    implementation_deps = [
        "//gloop/base:port",
        "//gloop/util/bits",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "shrunk-array_unittest",
    size = "small",
    srcs = ["shrunk-array_unittest.cc"],
    malloc = "//gloop:malloc",
    deps = [
        ":shrunk-array",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "//gloop/util/endian",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
