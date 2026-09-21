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
# that are not low level.   For more information, including how to submit
# changes to this file, see    <link>

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

config_setting(
    name = "cs_xbinaryfdo",
    values = {"features": "cs_xbinaryfdo"},
)

# Complete a parent task when all its child tasks complete.
cc_library(
    name = "child_task_barrier",
    srcs = ["child_task_barrier.cc"],
    hdrs = ["child_task_barrier.h"],
    implementation_deps = [
        "//gloop/base:callback",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/synchronization",
    ],
    deps = [
        ":task",
        "//gloop:enforce_gloop_support",
        "//gloop/concurrent/barrier:incremental_barrier",
        "//gloop/thread",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
    ],
)

cc_test(
    name = "child_task_barrier_test",
    size = "small",
    srcs = ["child_task_barrier_test.cc"],
    deps = [
        ":child_task_barrier",
        ":sync_task",
        ":task",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "//gloop/util/status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "sleep",
    srcs = ["sleep.cc"],
    hdrs = ["sleep.h"],
    copts = select({
        "//conditions:default": [],
    }),
    implementation_deps = [
        ":task",
        "//gloop/base:walltime",
        "//gloop/thread",
        "//gloop/thread:thread_manager",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/status",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "sleep_test",
    size = "medium",
    srcs = ["sleep_test.cc"],
    deps = [
        ":sleep",
        ":sync_task",
        ":task",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "sync_task",
    srcs = ["sync_task.cc"],
    hdrs = ["sync_task.h"],
    copts = select({
        "//conditions:default": [],
    }),
    implementation_deps = [
        "//gloop/base:context",
        "//gloop/thread/fiber",
    ],
    deps = [
        ":task",
        "//gloop:enforce_gloop_support",
        "//gloop/thread",
        "//gloop/thread:config",
        "//gloop/thread/fiber:select",
        "//gloop/thread/fiber:selectables",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "sync_task_test",
    size = "small",
    srcs = ["sync_task_test.cc"],
    deps = [
        ":sleep",
        ":sync_task",
        ":task",
        "//gloop:gloop_test_main",
        "//gloop/base:context",
        "//gloop/base:timer",
        "//gloop/thread",
        "//gloop/thread:config",
        "//gloop/thread/fiber",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "task",
    srcs = ["task.cc"],
    hdrs = ["task.h"],
    copts = select({
        "//conditions:default": [],
    }),
    implementation_deps = [
        "//gloop/thread",
        "//gloop/util/functional:with_context",
        "//gloop/util/random:shared_bit_gen",
        "//gloop/util/refcount:reference_counted",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/strings:str_format",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:context",
        "//gloop/base:spinlock",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:transitional",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "task_test",
    size = "small",
    srcs = ["task_test.cc"],
    deps = [
        ":task",
        "//gloop:gloop_test_main",
        "//gloop/base:context",
        "//gloop/base:tracecontext",
        "//gloop/perftools/tracing:mock_trace_event_listener",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/perftools/tracing:with_trace_event_listener",
        "//gloop/thread",
        "//gloop/util/functional:with_context",
        "//gloop/util/gtl:unique_array",
        "//gloop/util/refcount:reference_counted",
        "//gloop/util/status",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "status_matchers",
    testonly = True,
    hdrs = ["status_matchers.h"],
    deps = [
        ":status_matchers_internal",
        "//gloop:enforce_gloop_support",
        "//gloop/util/status:status_macros",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)

cc_test(
    name = "status_matchers_test",
    size = "small",
    srcs = ["status_matchers_test.cc"],
    deps = [
        ":status_matchers",
        "//gloop:gloop_test_main",
        "//gloop/util/status",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop/util/status:status_cc_proto",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "status_matchers_internal",
    testonly = True,
    srcs = ["status_matchers_internal.cc"],
    hdrs = ["status_matchers_internal.h"],
    textual_hdrs = ["status_matchers.h"],
    visibility = [
        "//visibility:private",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/status",
        "//gloop/util/status:status_builder",
        "//gloop/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop/util/status:codes_cc_proto",
        "@gloop//gloop/util/status:status_cc_proto",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)
