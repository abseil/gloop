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

# Libraries in this low-level package may not depend on libraries in packages
# that are not low level. For more information, including how to submit
# changes to this file, see <link>howto/build-monitors.html

load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

# TODO Remove this once we can use the real thing to avoid ODR violations.
cc_proto_library(
    name = "message_set_cc_proto",
    deps = ["@protobuf//src/google/protobuf/bridge:message_set_proto"],
)

cc_test(
    name = "canonical_errors_test",
    size = "small",
    srcs = ["canonical_errors_test.cc"],
    deps = [
        ":status",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "canonical_error_space_find_test",
    srcs = ["canonical_error_space_find_test.cc"],
    deps = [
        ":status",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "errno_mapping",
    srcs = ["errno_mapping.cc"],
    hdrs = ["errno_mapping.h"],
    deps = [
        ":status",
        ":status_builder",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "errno_mapping_test",
    srcs = ["errno_mapping_test.cc"],
    deps = [
        ":errno_mapping",
        ":status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "posixerrorspace",
    srcs = ["posixerrorspace.cc"],
    hdrs = ["posixerrorspace.h"],
    deps = [
        ":errno_mapping",
        ":status",
        "//gloop/base:strerror",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "posixerrorspace_test",
    srcs = ["posixerrorspace_test.cc"],
    deps = [
        ":posixerrorspace",
        ":status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/types:source_location",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ret_check",
    srcs = ["ret_check.cc"],
    hdrs = ["ret_check.h"],
    deps = [
        ":status",
        ":status_builder",
        ":status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:has_ostream_operator",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "ret_check_test",
    size = "small",
    srcs = ["ret_check_test.cc"],
    deps = [
        ":ret_check",
        ":status_macros",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "status",
    srcs = [
        "error_space.cc",
        "status.cc",
        "status_to_from_proto.cc",
    ],
    hdrs = [
        "error_space.h",
        "status.h",
    ],
    textual_hdrs = [
        # This header is not intended to be public and should not be referenced
        # by other targets. It is listed in textual_hdrs instead of srcs to
        # work around b/175618066.
        "status_internal.h",
        "status_oss_shim.inc",
    ],
    deps = [
        "//gloop/util/coding",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:source_location",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
        "@protobuf",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "status_test",
    size = "small",
    srcs = [
        "error_space_test.cc",
        "status_test.cc",
    ],
    deps = [
        ":status",
        ":test_payload_cc_proto",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord_test_helpers",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@protobuf",
        "@protobuf//:any_cc_proto",
        "@protobuf//:wrappers_cc_proto",
    ],
)

cc_library(
    name = "status_builder",
    srcs = ["status_builder.cc"],
    hdrs = ["status_builder.h"],
    deps = [
        ":status",
        "//gloop/base:examine_stack",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log/internal:vlog_config",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:internal",
        "@abseil-cpp//absl/strings:stringify_stream",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "status_builder_test",
    srcs = ["status_builder_test.cc"],
    deps = [
        ":posixerrorspace",
        ":status",
        ":status_builder",
        ":status_macros",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "status_macros",
    srcs = ["status_macros.cc"],
    hdrs = ["status_macros.h"],
    deps = [
        ":status",
        ":status_builder",
        "//gloop/util/status:message_set_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "status_macros_test",
    size = "small",
    srcs = ["status_macros_test.cc"],
    deps = [
        ":posixerrorspace",
        ":status",
        ":status_builder",
        ":status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

proto_library(
    name = "test_payload_proto",
    testonly = 1,
    srcs = ["test_payload.proto"],
    option_deps = ["@protobuf//:cpp_features_proto"],
    visibility = ["//visibility:private"],
    deps = ["@protobuf//src/google/protobuf/bridge:message_set_proto"],
)

cc_proto_library(
    name = "test_payload_cc_proto",
    testonly = 1,
    deps = [":test_payload_proto"],
)
