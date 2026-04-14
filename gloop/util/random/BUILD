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
    default_hdrs_check = "strict",
    default_visibility = ["//gloop:__subpackages__"],
    features = ["header_modules"],
)

cc_library(
    name = "acmrandom",
    srcs = ["acmrandom.cc"],
    hdrs = ["acmrandom.h"],
    # This library is highly deprecated and should not be marked public for wider use.
    visibility = ["//gloop:__subpackages__"],
    deps = [
        ":random_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "acmrandom_unittest",
    size = "small",
    srcs = ["acmrandom_unittest.cc"],
    deps = [
        ":acmrandom",
        "//gloop/util/math:mathutil",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "distributions",
    hdrs = ["distributions.h"],
    # This library is only needed by Gloop externally.
    visibility = ["//gloop:__subpackages__"],
    deps = [
        "//gloop/util/random/internal:skewed_low_distribution",
        "//gloop/util/random/internal:yule_simon_distribution",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/random/internal:distribution_caller",
        "@abseil-cpp//absl/random/internal:traits",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "distributions_test",
    srcs = ["distributions_test.cc"],
    deps = [
        ":distributions",
        ":mock_distributions",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:mocking_bit_gen",
        "@abseil-cpp//absl/random/internal:distribution_test_util",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "global_id",
    srcs = ["global_id.cc"],
    hdrs = ["global_id.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "global_id_test",
    size = "large",
    srcs = ["global_id_test.cc"],
    shard_count = 2,
    deps = [
        ":global_id",
        "//gloop/thread",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "mock_distributions",
    testonly = 1,
    hdrs = ["mock_distributions.h"],
    # This library is only needed by Gloop externally.
    visibility = ["//gloop:__subpackages__"],
    deps = [
        "//gloop/util/random/internal:skewed_low_distribution",
        "//gloop/util/random/internal:yule_simon_distribution",
        "@abseil-cpp//absl/random:mocking_bit_gen",
        "@abseil-cpp//absl/random/internal:mock_overload_set",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "mt_random",
    srcs = ["mt_random.cc"],
    hdrs = ["mt_random.h"],
    # This library is highly deprecated and should not be marked public for wider use.
    visibility = ["//gloop:__subpackages__"],
    deps = [
        ":random_base",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "mt_random_unittest",
    size = "small",
    srcs = ["mt_random_unittest.cc"],
    deps = [
        ":mt_random",
        ":secure_random_testutils",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "random_base",
    srcs = ["random_base.cc"],
    hdrs = ["random_base.h"],
    # This library is highly deprecated and should not be marked public for wider use.
    visibility = ["//gloop:__subpackages__"],
    deps = [
        "//gloop/base:atomic_sequence_num",
        "//gloop/base:port",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "random_base_unittest",
    size = "small",
    srcs = ["random_base_unittest.cc"],
    deps = [
        ":mt_random",
        ":random_base",
        ":test_random",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "secure_random_testutils",
    testonly = 1,
    srcs = ["random_base_testutils.cc"],
    hdrs = ["random_base_testutils.h"],
    deps = [
        ":random_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "test_random",
    hdrs = ["test_random-inl.h"],
    deps = [
        ":random_base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "shared_bit_gen",
    hdrs = ["shared_bit_gen.h"],
    deps = [
        "//gloop/concurrent/percpu:object",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/random",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "shared_bit_gen_test",
    srcs = ["shared_bit_gen_test.cc"],
    deps = [
        ":shared_bit_gen",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
