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
    name = "coding",
    srcs = ["coding.cc"],
    hdrs = ["coding.h"],
    deps = [
        ":nthbit",
        ":options",
        "//gloop:enforce_gloop_support",
        "//gloop/util/coding",
        "//gloop/util/endian",
        "//gloop/util/gtl:unaligned",
        "//gloop/util/intops",
        "//gloop/util/status:ret_check",
        "//gloop/util/status:status_builder",
        "//gloop/util/status:status_macros",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/container:layout",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:source_location",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "coding_test",
    srcs = ["coding_test.cc"],
    deps = [
        ":coding",
        ":options",
        "//gloop:gloop_test_main",
        "//gloop/base:commandlineflags",
        "//gloop/base:googleinit",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "nthbit",
    srcs = ["nthbit.cc"],
    hdrs = ["nthbit.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:cpu_detect",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
    ],
)

cc_test(
    name = "nthbit_test",
    srcs = ["nthbit_test.cc"],
    deps = [
        ":nthbit",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "options",
    srcs = ["options.cc"],
    hdrs = ["options.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/coding",
        "//gloop/util/status:ret_check",
        "//gloop/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "options_test",
    srcs = ["options_test.cc"],
    deps = [
        ":options",
        "//gloop:gloop_test_main",
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
