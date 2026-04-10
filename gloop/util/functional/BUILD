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
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

# For only util::functional::FromCallback and friends
cc_library(
    name = "from_callback",
    srcs = [
        "from_callback_internal.h",
        "to_callback_internal.h",
    ],
    hdrs = ["from_callback.h"],
    visibility = ["//gloop:__subpackages__"],
    deps = [
        ":to_callback",
        ":to_shared_function",
        "//gloop/base:callback-types",
        "//gloop/base:context",
        "//gloop/base:tracecontext",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/util/refcount:compact_reference_counted",
        "//gloop/util/refcount:reffed_ptr",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "from_callback_test",
    srcs = ["from_callback_test.cc"],
    deps = [
        ":from_callback",
        ":to_callback",
        "//gloop/base:callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/functional:bind_front",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# For only util::functional::ToCallback
cc_library(
    name = "to_callback",
    srcs = ["to_callback_internal.h"],
    hdrs = ["to_callback.h"],
    visibility = ["//gloop:__subpackages__"],
    deps = [
        "//gloop/base:callback-types",
        "//gloop/base:context",
        "//gloop/base:tracecontext",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "to_callback_benchmark",
    srcs = ["to_callback_benchmark.cc"],
    deps = [
        ":to_callback",
        "//gloop/base:callback",
        "//gloop/base:context",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "to_callback_test",
    srcs = ["to_callback_test.cc"],
    deps = [
        ":from_callback",
        ":to_callback",
        "//gloop/base:callback",
        "//gloop/base:context",
        "//gloop/base:tracecontext",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "to_shared_function",
    hdrs = ["to_shared_function.h"],
    visibility = ["//gloop:__subpackages__"],
    deps = [
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/memory",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "to_shared_function_test",
    size = "small",
    srcs = ["to_shared_function_test.cc"],
    deps = [
        ":to_shared_function",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# For only util::functional::WithContext
cc_library(
    name = "with_context",
    srcs = ["with_context_internal.h"],
    hdrs = ["with_context.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:context",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "callable_once",
    srcs = ["callable_once_internal.h"],
    hdrs = ["callable_once.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "with_context_test",
    srcs = ["with_context_test.cc"],
    deps = [
        ":with_context",
        "//gloop/base:context",
        "//gloop/base:tracecontext",
        "//gloop/perftools/tracing:mock_trace_event_listener",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/perftools/tracing:with_trace_event_listener",
        "@abseil-cpp//absl/functional:any_invocable",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "with_context_benchmark",
    srcs = ["with_context_benchmark.cc"],
    deps = [
        ":with_context",
        "//gloop/base:context",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "callable_once_test",
    srcs = ["callable_once_test.cc"],
    deps = [
        ":callable_once",
        ":to_callback",
        "//gloop/base:callback",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
