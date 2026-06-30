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
load(
    "//gloop/tools/build_rules/text_to_binary:def.bzl",
    "proto_data",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_testonly = 1,
    default_visibility = [
        "//gloop/tools/build_rules/text_to_binary:__subpackages__",
    ],
)

proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    option_deps = ["@protobuf//:java_features_proto"],
    deps = [":test_nested_proto"],
)

cc_proto_library(
    name = "test_cc_proto",
    deps = [":test_proto"],
)

proto_library(
    name = "test_nested_proto",
    srcs = ["test_nested.proto"],
)

cc_proto_library(
    name = "test_nested_cc_proto",
    deps = [":test_nested_proto"],
)

proto_library(
    name = "test_extension_proto",
    srcs = ["test_extension.proto"],
    deps = [":test_proto"],
)

cc_proto_library(
    name = "test_extension_cc_proto",
    deps = [":test_extension_proto"],
)

# proto_data needs the top-level message and extension proto libraries.
# out defaults to test_data.binarypb
proto_data(
    name = "test_data",
    src = "test_data.textproto",
    proto_deps = [
        ":test_extension_proto",
        ":test_proto",
    ],
    proto_name = "tools.build_rules.text_to_binary.testdata.Test",
)

proto_data(
    name = "deterministic_abc",
    src = "deterministic_abc.textproto",
    proto_deps = ["@protobuf//:struct_proto"],
    proto_name = "google.protobuf.Struct",
)

proto_data(
    name = "deterministic_cba",
    src = "deterministic_cba.textproto",
    proto_deps = ["@protobuf//:struct_proto"],
    proto_name = "google.protobuf.Struct",
)
