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
    default_visibility = ["//visibility:private"],
)

########################################################################
# Libraries
########################################################################

cc_library(
    name = "make_future_pair",
    hdrs = ["make_future_pair.h"],
    # This library is deprecated, and not open to new users. Please see
    # README.md
    #
    # NOTE: We intentionally use __pkg__ to restrict usage. If you need
    # to add a new package for some reason (e.g. moving files), do not use
    # __subpackages__.
    visibility = [
        "//gloop/concurrent/futures:__pkg__",
    ],
    deps = [
        "//gloop/thread",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "transform_future",
    hdrs = ["transform_future.h"],
    visibility = [
        "//gloop/concurrent/futures:__pkg__",
    ],
    deps = [
        ":make_future_pair",
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

########################################################################
# Tests
########################################################################

cc_test(
    name = "transform_future_test",
    size = "small",
    srcs = ["transform_future_test.cc"],
    deps = [
        ":make_future_pair",
        ":transform_future",
        "//gloop/concurrent/futures",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
