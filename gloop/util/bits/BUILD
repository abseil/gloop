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
# changes to this file, see    http://www/eng/howto/build-monitors.html

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "bits",
    hdrs = ["bits.h"],
    deps = [
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/numeric:int128",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "bits_unittest",
    timeout = "long",  # The test can be slow under MSAN.
    srcs = ["bits_unittest.cc"],
    deps = [
        ":bits",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "bit-copy",
    srcs = ["bit-copy.cc"],
    hdrs = ["bit-copy.h"],
    deps = [
        "@abseil-cpp//absl/base:nullability",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "bit-copy_unittest",
    srcs = ["bit-copy_unittest.cc"],
    copts = ["$(STACK_FRAME_UNLIMITED)"],  # bit-copy_unittest.cc
    deps = [
        ":bit-copy",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
