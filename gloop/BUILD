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

# Description:
#   Utility functions for sharing code originally written in Google's codebase.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

license(
    name = "license",
    package_name = "gloop",
)

licenses(["notice"])

exports_files(["LICENSE"])

cc_library(
    name = "enforce_gloop_support",
    hdrs = ["enforce_gloop_support.h"],
    visibility = [
        "@gloop//gloop:__subpackages__",
    ],
    deps = [
        "@host_distro//:gloop_distro",
    ],
)

# <link>
cc_test(
    name = "basic_test",
    srcs = ["basic_test.cc"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
