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
#
# Description:
# Bitmap implementations

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# default_hdrs_check not set yet
package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "bitmap",
    hdrs = [
        "bitmap.h",
        "inlined_bitvector.h",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "bitmap_large_test",
    size = "large",
    srcs = ["bitmap_large_test.cc"],
    shard_count = 6,
    visibility = ["//visibility:private"],
    deps = [
        ":bitmap",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/flags:flag",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "bitmap_test",
    size = "small",
    srcs = ["bitmap_test.cc"],
    shard_count = 8,
    visibility = ["//visibility:private"],
    deps = [
        ":bitmap",
        "//gloop:gloop_test_main",
        "//gloop/util/coding",
        "//gloop/util/random:acmrandom",
        "//gloop/util/random:mt_random",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "inlined_bitvector_test",
    srcs = ["inlined_bitvector_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":bitmap",
        "//gloop:gloop_test_main",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
