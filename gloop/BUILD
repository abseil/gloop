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

load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:private"],
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
)

string_flag(
    name = "sanitizer",
    build_setting_default = "none",
)

config_setting(
    name = "asan_enabled",
    flag_values = {":sanitizer": "asan"},
)

config_setting(
    name = "tsan_enabled",
    flag_values = {":sanitizer": "tsan"},
)

config_setting(
    name = "msan_enabled",
    flag_values = {":sanitizer": "msan"},
)

config_setting(
    name = "ubsan_enabled",
    flag_values = {":sanitizer": "ubsan"},
)

# The default malloc to use for tests.
cc_library(
    name = "malloc",
    testonly = True,
    visibility = [
        "//gloop:__subpackages__",
    ],
    deps = select({
        ":asan_enabled": [],
        ":tsan_enabled": [],
        ":msan_enabled": [],
        "//conditions:default": ["@tcmalloc//tcmalloc"],
    }) + ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "gloop_test_main",
    testonly = True,
    srcs = ["gloop_test_main.cc"],
    visibility = [
        "//gloop:__subpackages__",
    ],
    deps = [
        "//gloop/base:init_google",
        "@gloop//gloop:enforce_gloop_support",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)

platform(
    name = "rbe",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = {
        "container-image": "docker://gcr.io/google.com/absl-177019/gloop-linux-latest@sha256:b14a23d65e32c053eaca6bd543bdb70802cb84b34973427f4d5b3cfa567634c3",
        "Pool": "default",
        "dockerRunAsRoot": "true",
        "cache-silo-key": "default-04162026",
    },
)
