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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

# Description:
# The anticodex tool and unit test.
#

# anticodex-light does not include the dependencies needed for reading the
# global proto DB.
#
# Only proto types compiled into the binary or that are provided via
# --protofiles and --proto_paths will work.
cc_binary(
    name = "anticodex-light",
    deps = [
        ":anticodex-main",
        "//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "anticodex-main",
    srcs = ["anticodex.cc"],
    copts = ["$(STACK_FRAME_UNLIMITED)"],  # anticodex.cc
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:init_google",
        "//gloop/strings:escaping",
        "//gloop/strings:serialize",
        "//gloop/util/gtl:sparse_hash_map",
        "//gloop/util/gtl:sparse_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@protobuf",
        "@protobuf//:json_util",
        "@protobuf//src/google/protobuf/io",
    ],
    alwayslink = 1,
)
