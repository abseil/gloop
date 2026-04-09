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

load(
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "fiber-options",
    srcs = ["fiber-options.cc"],
    hdrs = ["fiber-options.h"],
    local_defines = ["FIBER_OPTIONS_COUNTING_MUTEX"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:arena",
        "//gloop/base:context",
        "//gloop/concurrent/percpu:counting_mutex",
        "//gloop/strings:arena-string",
        "//gloop/strings:keyed_intern_table",
        "//gloop/thread",
        "//gloop/thread/fiber/internal:fiber_thread_options",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "fiber-options_test",
    srcs = ["fiber-options_test.cc"],
    deps = [
        ":fiber-options",
        "//gloop/thread/fiber/internal:fiber_thread_options",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "per-domain-counters",
    hdrs = ["per-domain-counters.h"],
    visibility = [
        "//gloop/base/scheduling:__subpackages__",
    ],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)
