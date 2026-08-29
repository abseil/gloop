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

# Package:
#   concurrent/percpu
#
# Description:
#   Collection of cpu-local objects and utlities.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "counter",
    hdrs = ["counter.h"],
    textual_hdrs = [
        "counter_emulated.inc",
        "counter_native.inc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:percpu",
        "//gloop/base:sysinfo",
    ],
)

cc_test(
    name = "counter_test",
    size = "small",
    srcs = ["counter_test.cc"],
    linkstatic = 1,
    deps = [
        ":counter",
        "//gloop/base:sysinfo",
        "//gloop/thread",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:no_destructor",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "counting_mutex",
    srcs = ["counting_mutex.cc"],
    hdrs = ["counting_mutex.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ] + select({
        "//tools/cc_target_os:apple": [],
        "//conditions:default": [
            "//gloop/base:futex",
            "//gloop/base:percpu",
            "//gloop/base/scheduling",
            "@abseil-cpp//absl/base:dynamic_annotations",
            "@abseil-cpp//absl/base:no_destructor",
            "@abseil-cpp//absl/log:check",
            "@abseil-cpp//absl/random",
        ],
    }),
)

cc_test(
    name = "counting_mutex_test",
    srcs = ["counting_mutex_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":counting_mutex",
        "//gloop/base:sysinfo",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/util/tuple/components:dump_vars",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "counting_mutex_no_rseq_test",
    srcs = ["counting_mutex_test.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":counting_mutex",
        "//gloop/base:percpu_disable_rseq",
        "//gloop/base:sysinfo",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/util/tuple/components:dump_vars",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "object",
    hdrs = ["object.h"],
    textual_hdrs = [
        "object_emulated.inc",
        "object_native.inc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:percpu",
        "//gloop/base:spinlock",
        "//gloop/base:sysinfo",
        "//gloop/base/internal:percpu",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
    ],
)

cc_test(
    name = "object_test",
    size = "small",
    srcs = ["object_test.cc"],
    linkstatic = 1,
    visibility = ["//visibility:private"],
    deps = [
        ":object",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "rseq_clobber_test",
    srcs = ["rseq_clobber_test.cc"],
    linkopts = ["-Wl,--script=$(location rseq_clobber_test.lds)"],
    linkstatic = True,
    visibility = ["//visibility:private"],
    deps = [
        ":counting_mutex",
        ":rseq_clobber_test.lds",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
