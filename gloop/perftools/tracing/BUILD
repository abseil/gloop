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
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

# Allow list including perftools / dapper depot paths
package_group(
    name = "internal_visibility",
    packages = [
        "//gloop/perftools/tracing/...",
    ],
)

# Allow list including gloop base paths
package_group(
    name = "base_visibility",
    packages = [
        "//gloop/base/...",
    ],
)

# Allow list including paths with explicit access to trace_event_listener
package_group(
    name = "trace_event_listener_visibility",
    packages = [
    ],
)

cc_library(
    name = "test_only_access",
    testonly = True,
    hdrs = ["test_only_access.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "trace_source_location",
    hdrs = ["trace_source_location.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "trace_source_location_test",
    srcs = ["trace_source_location_test.cc"],
    deps = [
        ":test_only_access",
        ":trace_source_location",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tracing_base",
    srcs = [
        "string_base.cc",
        "string_label.cc",
        "string_registry.cc",
        "tracing_base.cc",
    ],
    hdrs = [
        "string_base.h",
        "string_label.h",
        "string_registry.h",
        "tracing_base.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":trace_source_location",
        "//gloop/util/gtl:lockfree_hashmap",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "tracing_base_test",
    srcs = ["tracing_base_test.cc"],
    deps = [
        ":mock_trace_event_listener",
        ":tracing_base",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/flags:flag",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_event_listener",
    srcs = ["trace_event_listener.cc"],
    hdrs = ["trace_event_listener.h"],
    visibility = [
        ":base_visibility",
        ":internal_visibility",
        ":trace_event_listener_visibility",
    ],
    deps = [
        ":tracing_base",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "trace_event_listener_test",
    srcs = ["trace_event_listener_test.cc"],
    deps = [
        ":mock_trace_event_listener",
        ":trace_event_listener",
        ":tracing_base",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "multiplex_trace_event_listener",
    srcs = ["multiplex_trace_event_listener.cc"],
    hdrs = ["multiplex_trace_event_listener.h"],
    visibility = [
        ":base_visibility",
        ":internal_visibility",
    ],
    deps = [
        ":trace_event_listener",
        ":tracing_base",
        "//gloop/base:examine_stack",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "multiplex_trace_event_listener_test",
    srcs = ["multiplex_trace_event_listener_test.cc"],
    deps = [
        ":mock_trace_event_listener",
        ":multiplex_trace_event_listener",
        ":trace_event_listener",
        ":tracing_base",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "mock_trace_event_listener",
    testonly = True,
    hdrs = ["mock_trace_event_listener.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":trace_event_listener",
        ":tracing_base",
        "@gloop//gloop:enforce_gloop_support",
        "@googletest//:gtest_for_library",
    ],
)

cc_library(
    name = "tracing_core",
    srcs = ["tracing_core.cc"],
    hdrs = ["tracing_core.h"],
    visibility = [":internal_visibility"],
    deps = [
        ":trace_event_listener",
        ":tracing_base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:tracing_internal",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "tracing_core_test",
    srcs = ["tracing_core_test.cc"],
    deps = [
        ":mock_trace_event_listener",
        ":tracing_base",
        ":tracing_core",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:tracing_internal",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "perftools_verify",
    hdrs = ["perftools_verify.h"],
    visibility = [
        ":internal_visibility",
    ],
    deps = [
        "//gloop/base:examine_stack",
        "@abseil-cpp//absl/log",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "perftools_verify_test",
    srcs = ["perftools_verify_test.cc"],
    deps = [
        ":perftools_verify",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sync_context",
    srcs = ["sync_context.cc"],
    hdrs = ["sync_context.h"],
    visibility = [
        ":base_visibility",
        ":internal_visibility",
    ],
    deps = [
        ":multiplex_trace_event_listener",
        ":perftools_verify",
        ":trace_event_listener",
        ":trace_source_location",
        ":tracing_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "format_to_buffer_sink",
    hdrs = ["format_to_buffer_sink.h"],
    visibility = [
        ":base_visibility",
        ":internal_visibility",
    ],
    deps = [
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "format_to_buffer_sink_test",
    srcs = ["format_to_buffer_sink_test.cc"],
    deps = [
        ":format_to_buffer_sink",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "with_trace_event_listener",
    hdrs = ["with_trace_event_listener.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":trace_event_listener",
        ":tracing_base",
        "//gloop/base:tracecontext",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "tracing",
    srcs = ["tracing.cc"],
    hdrs = ["tracing.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":trace_event_listener",
        ":trace_source_location",
        ":tracing_base",
        ":tracing_core",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "tracing_test",
    srcs = ["tracing_test.cc"],
    deps = [
        ":mock_trace_event_listener",
        ":trace_source_location",
        ":tracing",
        ":tracing_base",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
