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
# changes to this file, see    <link>howto/build-monitors.html

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

cc_library(
    name = "task",
    srcs = ["task.cc"],
    hdrs = ["task.h"],
    copts = select({
        "//conditions:default": [],
    }),
    deps = [
        "//gloop/base:callback",
        "//gloop/base:context",
        "//gloop/base:spinlock",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/thread",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:transitional",
        "//gloop/util/functional:with_context",
        "//gloop/util/random:shared_bit_gen",
        "//gloop/util/refcount:reference_counted",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
        "@protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "task_test",
    size = "small",
    srcs = ["task_test.cc"],
    deps = [
        ":task",
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
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:protobuf_lite",
    ],
)
