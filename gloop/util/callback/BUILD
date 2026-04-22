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

# Libraries in this low-level package may not depend on libraries in
# packages that are not low level.   For more information, including
# how to submit changes to this file, see
#      <link>

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "blocking_callback",
    srcs = ["blocking_callback.cc"],
    hdrs = ["blocking_callback.h"],
    deps = [
        "//gloop/base:callback",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "blocking_callback_unittest",
    size = "small",
    srcs = ["blocking_callback_unittest.cc"],
    deps = [
        ":blocking_callback",
        "//gloop/base:callback",
        "//gloop/thread",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "//gloop/util/gtl:stl_util",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "cancellable_closure",
    srcs = ["cancellable_closure.cc"],
    hdrs = ["cancellable_closure.h"],
    deps = [
        "//gloop/base:callback",
        "//gloop/base:context",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "cancellable_closure_test",
    size = "small",
    srcs = ["cancellable_closure_test.cc"],
    flaky = 1,
    deps = [
        ":cancellable_closure",
        "//gloop/base:callback",
        "//gloop/base:init_google",
        "//gloop/thread",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)
