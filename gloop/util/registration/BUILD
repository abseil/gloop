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
#   Helpers for registering classes.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "function_registry",
    hdrs = ["function_registry.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:source_location",
    ],
)

cc_test(
    name = "function_registry_test",
    srcs = ["function_registry_test.cc"],
    deps = [
        ":function_registry",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Static map
cc_library(
    name = "static_map",
    hdrs = ["static_map.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:source_location",
    ],
)

alias(
    name = "registerer",
    actual = "//gloop/util/registration",
)

cc_library(
    name = "registration",
    srcs = ["registerer.cc"],
    hdrs = ["registerer.h"],
    implementation_deps = ["@abseil-cpp//absl/log:check"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/gtl:lockfree_hashmap",
        "//gloop/util/hash:transparent_hash",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "registerer_tracker",
    srcs = ["registerer_tracker.cc"],
    hdrs = ["registerer_tracker.h"],
    implementation_deps = [
        ":registration",
        "//gloop/base:googleinit",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
    ],
    alwayslink = 1,
)

cc_test(
    name = "static_map_test",
    size = "small",
    srcs = ["static_map_test.cc"],
    deps = [
        ":static_map",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "registerer_unittest",
    srcs = ["registerer_unittest.cc"],
    deps = [
        ":registration",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
