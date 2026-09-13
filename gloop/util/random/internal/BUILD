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
    default_visibility = [
        "//gloop/util/random:__subpackages__",
    ],
)

cc_library(
    name = "skewed_low_distribution",
    hdrs = ["skewed_low_distribution.h"],
    deps = [
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/random/internal:iostream_state_saver",
        "@abseil-cpp//absl/random/internal:traits",
    ],
)

cc_test(
    name = "skewed_low_distribution_test",
    srcs = ["skewed_low_distribution_test.cc"],
    deps = [
        ":skewed_low_distribution",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random/internal:distribution_test_util",
        "@abseil-cpp//absl/random/internal:pcg_engine",
        "@abseil-cpp//absl/random/internal:sequence_urbg",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "yule_simon_distribution",
    hdrs = ["yule_simon_distribution.h"],
    deps = [
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/random/internal:iostream_state_saver",
        "@abseil-cpp//absl/random/internal:traits",
    ],
)

cc_test(
    name = "yule_simon_distribution_test",
    srcs = ["yule_simon_distribution_test.cc"],
    deps = [
        ":yule_simon_distribution",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random/internal:distribution_test_util",
        "@abseil-cpp//absl/random/internal:pcg_engine",
        "@abseil-cpp//absl/random/internal:sequence_urbg",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "small_prime_distribution",
    hdrs = ["small_prime_distribution.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/random/internal:iostream_state_saver",
    ],
)

cc_test(
    name = "small_prime_distribution_test",
    srcs = ["small_prime_distribution_test.cc"],
    deps = [
        ":small_prime_distribution",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
