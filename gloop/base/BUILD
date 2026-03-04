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

load(
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package_group(
    name = "base_implementation",
    packages = [
        "//gloop/base/...",
    ],
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = [":base_implementation"],
    features = ["header_modules"],
)

cc_library(
    name = "atomic_sequence_num",
    hdrs = ["atomic_sequence_num.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "atomic_sequence_num_test",
    size = "medium",
    srcs = ["atomic_sequence_num_test.cc"],
    deps = [
        ":atomic_sequence_num",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "config",
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:config",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

# Unique Global Symbol Compliance.
# The below targets allow shared library owners to annotate their shared library
# targets as 'not global symbol compliant', and common library code to detect
# this. Common library code such as `percpu` can then avoid undefined behavior
# and failures from symbol duplication resulting from dlopen.
# See <link> for more details and background.
cc_library(
    name = "global_symbol_compliance",
    srcs = ["global_symbol_compliance.cc"],
    hdrs = ["global_symbol_compliance.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "global_symbol_compliant_test",
    srcs = ["global_symbol_compliant_test.cc"],
    deps = [
        ":global_symbol_compliance",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

PERCPU_LINUX_DEPS = [
    "@tcmalloc//tcmalloc/internal:sysinfo",
]

cc_library(
    name = "percpu",
    srcs = [
        "percpu.cc",
        "percpu_handle.cc",
        "percpu_rseq_unsupported.cc",
        "percpu_types.cc",
    ] + select({
        "@platforms//os:linux": ["percpu_rseq_asm.S"],
        "//conditions:default": [],
    }),
    hdrs = [
        "percpu.h",
        "percpu_macros.h",
        "percpu_types.h",
    ],
    textual_hdrs = select({
        "@platforms//os:linux": [
            "percpu_rseq_aarch64.S",
            "percpu_rseq_x86_64.S",
        ],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        ":global_symbol_compliance",
        ":scoped_sigmask",
        ":spinlock",
        "//gloop/base/internal:percpu",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/util/atomic_danger",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:base_internal",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/base:spinlock_wait",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
        "@tcmalloc//tcmalloc/internal:linux_syscall_support",
    ] + select({
        "@platforms//os:linux": PERCPU_LINUX_DEPS,
        "//conditions:default": [],
    }),
)

cc_test(
    name = "percpu_macros_test",
    srcs = ["percpu_macros_test.cc"],
    deps = [
        ":percpu",
        "@abseil-cpp//absl/base",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@tcmalloc//tcmalloc/internal:linux_syscall_support",
    ],
)

cc_test(
    name = "percpu_early_test",
    srcs = ["percpu_early_test.cc"],
    linkstatic = 1,
    deps = [
        ":percpu",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "percpu_handle_early_test",
    srcs = ["percpu_handle_early_test.cc"],
    linkstatic = 1,
    deps = [
        ":percpu",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "percpu_compat_test",
    srcs = ["percpu_compat_test.cc"],
    deps = [
        ":percpu",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

# Platform specific library that currently contains implementations of snprintf
# and vsnprintf for Windows.
cc_library(
    name = "port",
    hdrs = ["port.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "port_test",
    size = "small",
    srcs = ["port_test.cc"] + [
        "port_byteswap_test.cc",
    ],
    deps = [
        ":port",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "scoped_sigmask",
    hdrs = ["scoped_sigmask.h"],
    visibility = [
        "//gloop/base:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "spinlock",
    hdrs = ["spinlock.h"],
    visibility = [
        "//:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "strtoint",
    srcs = ["strtoint.cc"],
    hdrs = ["strtoint.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "strtoint_test",
    size = "small",
    srcs = ["strtoint_test.cc"],
    deps = [
        ":strtoint",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "timer",
    srcs = ["timer.cc"],
    hdrs = ["timer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":port",
        ":walltime",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "timer_unittest",
    size = "medium",
    srcs = ["timer_unittest.cc"],
    flaky = 1,
    deps = [
        ":timer",
        "//gloop/util/gtl:unique_array",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "uword",
    hdrs = ["uword.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "walltime",
    srcs = ["walltime.cc"],
    hdrs = ["walltime.h"],
    aspect_hints = [
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "walltime_unittest",
    size = "medium",
    srcs = ["walltime_unittest.cc"],
    deps = [
        ":walltime",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
