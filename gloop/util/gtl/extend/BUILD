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

# This package defines the gtl::Extend templates and several commonly used
# extensions.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "extend",
    hdrs = ["extend.h"],
    deps = [
        # TODO: Remove once all extensions are migrated to new target.
        ":extension",
        "@gloop//gloop:enforce_gloop_support",
        "//gloop/util/gtl/extend/internal:extend",
        "//gloop/util/gtl/internal:dependencies",
    ],
)

cc_test(
    name = "extend_test",
    srcs = ["extend_test.cc"],
    deps = [
        ":extend",
        "//gloop/util/gtl:requires",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "extension",
    hdrs = ["extension.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)
