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
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
