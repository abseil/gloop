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
)

cc_library(
    name = "adjustable-heap",
    hdrs = ["adjustable-heap.h"],
    deps = [],
)

cc_library(
    name = "adjustable_priority_queue",
    hdrs = [
        "adjustable_priority_queue.h",
        "adjustable_priority_queue-inl.h",
    ],
    deps = [
        ":adjustable-heap",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "adjustable_priority_queue_unittest",
    srcs = ["adjustable_priority_queue_unittest.cc"],
    deps = [
        ":adjustable_priority_queue",
        "//gloop/base:init_google",
        "//gloop/util/gtl:stl_util",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "adjustable-heap_test",
    srcs = ["adjustable-heap_test.cc"],
    deps = [
        ":adjustable-heap",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
