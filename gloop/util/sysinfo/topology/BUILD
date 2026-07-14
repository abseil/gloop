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

load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "affinity",
    srcs = select({
        "//mobile/flutter/build_defs/internal:targeting_ios": [],
        "//conditions:default": ["affinity.cc"],
    }),
    hdrs = select({
        "//mobile/flutter/build_defs/internal:targeting_ios": [],
        "//conditions:default": ["affinity.h"],
    }),
    deps = select({
        "//mobile/flutter/build_defs/internal:targeting_ios": [],
        "//conditions:default": [
            "//gloop/util/os/core:cpu_set",
            "@abseil-cpp//absl/log",
        ],
    }) + ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "topology",
    srcs = select({
        "//mobile/flutter/build_defs/internal:targeting_ios": [],
        "//conditions:default": [
            "topology.cc",
            "topology_converter.cc",
        ],
    }),
    hdrs = select({
        "//mobile/flutter/build_defs/internal:targeting_ios": [],
        "//conditions:default": [
            "topology.h",
            "topology_converter.h",
        ],
    }),
    deps = select({
        "//mobile/flutter/build_defs/internal:targeting_ios": [],
        "//conditions:default": [
            ":affinity",
            ":topology_cc_proto",
            "//gloop/base:commandlineflags",
            "//gloop/base:sysinfo",
            "//gloop/strings:split",
            "//gloop/testing/production_stub:testvalue",
            "//gloop/util/gtl:container_logging",
            "//gloop/util/os/core:cpu_set",
            "@abseil-cpp//absl/algorithm:container",
            "@abseil-cpp//absl/base:core_headers",
            "@abseil-cpp//absl/base:cpu_detect",
            "@abseil-cpp//absl/container:flat_hash_map",
            "@abseil-cpp//absl/flags:flag",
            "@abseil-cpp//absl/log",
            "@abseil-cpp//absl/log:check",
            "@abseil-cpp//absl/status",
            "@abseil-cpp//absl/status:statusor",
            "@abseil-cpp//absl/strings",
            "@abseil-cpp//absl/types:span",
            "@re2",
        ],
    }) + ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "topology_unittest",
    timeout = "long",
    srcs = ["topology_unittest.cc"],
    data = [
    ],
    linkstatic = 1,
    deps = [
        ":topology",
        "//gloop/base:init_google",
        "//gloop/base:log_file_flags",
        "//gloop/base:sysinfo",
        "//gloop/testing/production_stub:testvalue",
        "//gloop/util/os/core:cpu_set",
        "//gloop/util/os/core:cpu_set_test_util",
        "@abseil-cpp//absl/base:cpu_detect",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@re2",
    ],
)

cc_proto_library(
    name = "topology_cc_proto",
    deps = [":topology_proto"],
)

cc_library(
    name = "topology_mock",
    testonly = 1,
    hdrs = ["topology_mock.h"],
    copts = ["-Wthread-safety"],
    deps = [
        ":topology",
        "//gloop:enforce_gloop_support",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/base:cpu_detect",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)

proto_library(
    name = "topology_proto",
    srcs = ["topology.proto"],
    option_deps = ["@protobuf//:cpp_features_proto"],
)
