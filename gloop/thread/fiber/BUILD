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
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
)

cc_library(
    name = "arrival-order-scheduler",
    srcs = ["arrival-order-scheduler.cc"],
    hdrs = ["arrival-order-scheduler.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scheduler_types",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:inlined_vector",
    ],
)

cc_test(
    name = "arrival-order-scheduler_test",
    srcs = ["arrival-order-scheduler_test.cc"],
    linkstatic = 1,
    deps = [
        ":arrival-order-scheduler",
        ":fiber",
        ":fiber-options",
        ":fifolifo-schedulers",
        ":select",
        "//gloop/base:init_google",
        "//gloop/base:log_file_flags",
        "//gloop/base/scheduling",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "channel",
    srcs = [
        "channel.cc",
        # TODO: Break this circular dep.
        "fiber.h",
    ],
    hdrs = ["channel.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":channel-internal",
        ":fiber-internal",
        ":fiber-options",
        ":fiber_cc_proto",
        ":per-domain-counters",
        ":select",
        ":selectables",
        "//gloop/base:context",
        "//gloop/base/scheduling",
        "//gloop/util/gtl:intrusive_list",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
    ],
)

cc_library(
    name = "channel-internal",
    hdrs = ["channel-internal.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        ":select",
        ":select-internal",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:chunked_queue",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "channel_test",
    srcs = ["channel_test.cc"],
    linkstatic = 1,
    deps = [
        ":channel",
        ":fiber",
        ":fiber-internal",
        ":fiber-options",
        ":fifolifo-schedulers",
        ":probabilistic_test_util",
        ":select",
        ":selectables",
        "//gloop/base:config",
        "//gloop/base:context",
        "//gloop/base:walltime",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:simulated_clock",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "fiber",
    srcs = [
        "bundle.cc",
        "fiber.cc",
        "init-domain.cc",
        "pthread-domain.cc",
        "sleep.cc",
    ],
    hdrs = [
        "bundle.h",
        "fiber.h",
        "init-domain.h",
        "pthread-domain.h",
        "sleep.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":arrival-order-scheduler",
        ":channel",
        ":fiber-internal",
        ":fiber-options",
        ":fiber_cc_proto",
        ":fifolifo-schedulers",
        ":futex-domain",
        ":per-domain-counters",
        ":scheduler_types",
        ":select",
        ":selectables",
        "//gloop/base:cancellation_coloring",
        "//gloop/base:config",
        "//gloop/base:context",
        "//gloop/base:googleinit",
        "//gloop/base:static_threadlocal",
        "//gloop/base:sysinfo",
        "//gloop/base:thread_identity",
        "//gloop/base:walltime",
        "//gloop/base/scheduling",
        "//gloop/thread",
        "//gloop/thread:config",
        "//gloop/thread:os_semaphore",
        "//gloop/thread/fiber/internal:fiber_thread_pool",
        "//gloop/util/functional:with_context",
        "//gloop/util/gtl:intrusive_list",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
    ] + select({
        "//conditions:default": [],
    }),
)

cc_library(
    name = "fiber-internal",
    srcs = ["fiber-internal.cc"],
    hdrs = ["fiber-internal.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
        "//gloop/thread/fiber/internal:__subpackages__",
        "//thread/fiber/internal:__pkg__",
    ],
    deps = [
        ":fiber-options",
        ":fiber_cc_proto",
        "//gloop/base:thread_identity",
        "//gloop/base:walltime",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:low-level-support",
        "//gloop/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "fiber-once_test",
    size = "small",
    srcs = ["fiber-once_test.cc"],
    deps = [
        ":fiber",
        ":fiber-options",
        ":fifolifo-schedulers",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:low-level-support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "fiber_test",
    srcs = ["fiber_test.cc"],
    linkstatic = 1,
    visibility = [
        "//devtools/crosstool/perflab/benchmarks:__pkg__",
        "//waymo/os/bfc:__subpackages__",
    ],
    deps = [
        ":channel",
        ":fiber",
        ":fiber-internal",
        ":fiber-options",
        ":fifolifo-schedulers",
        ":select",
        ":selectables",
        "//gloop/base:callback",
        "//gloop/base:cancellation_coloring",
        "//gloop/base:context",
        "//gloop/base:sysinfo",
        "//gloop/base:thread_identity",
        "//gloop/base:time_support",
        "//gloop/base:tracecontext",
        "//gloop/base/scheduling",
        "//gloop/thread",
        "//gloop/thread:config",
        "//gloop/thread:thread_manager",
        "//gloop/thread/fiber/internal:fiber_thread_options",
        "//gloop/thread/fiber/internal:fiber_thread_pool",
        "//gloop/util/functional:callable_once",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "//gloop/util/math:mathutil",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ] + select({
        "//conditions:default": [],
    }),
)

cc_test(
    name = "bundle_test",
    srcs = ["bundle_test.cc"],
    linkstatic = 1,
    visibility = [
        "//devtools/crosstool/perflab/benchmarks:__pkg__",
        "//waymo/os/bfc:__subpackages__",
    ],
    deps = [
        ":fiber",
        ":select",
        "//gloop/base:context",
        "//gloop/thread",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

[
    cc_test(
        name = "pthread-domain" + suffix + "_test",
        srcs = ["pthread-domain_test.cc"],
        deps = [
            ":fiber",
            "//gloop/base/scheduling:domain-test",
            "@google_benchmark//:benchmark",
            "@googletest//:gtest",
        ] + add_deps,
    )
    for suffix, add_deps in [
        ("", []),
    ]
]

cc_test(
    name = "sleep_test",
    srcs = ["sleep_test.cc"],
    linkstatic = 1,
    deps = [
        ":fiber",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:simulated_clock",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "fifolifo-schedulers",
    srcs = ["fifolifo-schedulers.cc"],
    hdrs = ["fifolifo-schedulers.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scheduler_types",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:inlined_vector",
    ],
)

cc_library(
    name = "fiber-options",
    srcs = ["fiber-options.cc"],
    hdrs = ["fiber-options.h"],
    local_defines = ["FIBER_OPTIONS_COUNTING_MUTEX"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:arena",
        "//gloop/base:context",
        "//gloop/strings:arena-string",
        "//gloop/strings:keyed_intern_table",
        "//gloop/thread",
        "//gloop/thread/fiber/internal:fiber_thread_options",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
    ] + [
        "//gloop/concurrent/percpu:counting_mutex",
    ],
)

cc_test(
    name = "fiber-options_test",
    srcs = ["fiber-options_test.cc"],
    deps = [
        ":fiber",
        ":fiber-options",
        "//gloop/thread/fiber/internal:fiber_thread_options",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

proto_library(
    name = "fiber_proto",
    srcs = ["fiber.proto"],
    option_deps = ["@protobuf//:cpp_features_proto"],
    visibility = ["//thread/fiber:__subpackages__"],
)

cc_proto_library(
    name = "fiber_cc_proto",
    visibility = ["//thread/fiber:__subpackages__"],
    deps = [":fiber_proto"],
)

cc_library(
    name = "futex-domain",
    srcs = ["futex-domain.cc"],
    hdrs = ["futex-domain.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
        "//gloop/thread/fiber/internal:__subpackages__",
        "//thread/fiber:__subpackages__",
    ],
    deps = [
        "//gloop/base/scheduling",
        "//gloop/thread",
        "//gloop/thread/fiber/internal:fiber-domain-support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@abseil-cpp//absl/time",
    ] + select({
        "//conditions:default": [
            "//gloop/base:futex",
            "//gloop/thread:stack_reclaimer",
        ],
    }),
)

cc_test(
    name = "futex-domain_test",
    srcs = ["futex-domain_test.cc"],
    deps = [
        ":futex-domain",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:domain-test",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "per-domain-counters",
    hdrs = ["per-domain-counters.h"],
    visibility = [
        "//gloop/base/scheduling:__subpackages__",
    ],
)

cc_library(
    name = "probabilistic_test_util",
    testonly = 1,
    hdrs = ["probabilistic_test_util.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "probabilistic_test_util_test",
    srcs = ["probabilistic_test_util_test.cc"],
    deps = [
        ":probabilistic_test_util",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "scheduler_types",
    srcs = ["scheduler-types.cc"],
    hdrs = ["scheduler-types.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base/scheduling",
        "//gloop/util/atomic_danger",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/base:spinlock_wait",
        "@abseil-cpp//absl/container:fixed_array",
    ],
)

cc_test(
    name = "scheduler-types_test",
    srcs = ["scheduler-types_test.cc"],
    deps = [
        ":scheduler_types",
        "//gloop/base:callback",
        "//gloop/thread",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "priority_admission_scheduler",
    srcs = ["priority_admission_scheduler.cc"],
    hdrs = ["priority_admission_scheduler.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scheduler_types",
        "//gloop/base:raw_logging",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:low-level-support",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:vlog_is_on",
    ],
)

cc_test(
    name = "priority_admission_scheduler_test",
    srcs = ["priority_admission_scheduler_test.cc"],
    deps = [
        ":fiber",
        ":fiber-options",
        ":fifolifo-schedulers",
        ":priority_admission_scheduler",
        ":select",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "select",
    srcs = ["select.cc"],
    hdrs = ["select.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":select-internal",
        "//gloop/base/scheduling",
        "//gloop/concurrent/percpu:object",
        "//gloop/perftools/tracing",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
        "@abseil-cpp//absl/types:source_location",
    ],
)

cc_library(
    name = "select-internal",
    srcs = ["select-internal.cc"],
    hdrs = ["select-internal.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "selectables",
    srcs = ["selectables.cc"],
    hdrs = ["selectables.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":select",
        ":select-internal",
        "//gloop/base:cancellation_coloring",
        "//gloop/base:spinlock",
        "//gloop/perftools/tracing",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "selectables_test_lib",
    testonly = 1,
    srcs = ["selectables_test.cc"],
    visibility = ["//thread/fiber:__pkg__"],
    deps = [
        ":fiber",
        ":fiber-internal",
        ":fiber-options",
        ":probabilistic_test_util",
        ":select",
        ":selectables",
        "//gloop/base:cancellation_coloring",
        "//gloop/base:context",
        "//gloop/base:walltime",
        "//gloop/perftools/tracing:mock_trace_event_listener",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/perftools/tracing:with_trace_event_listener",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
        "@abseil-cpp//absl/time:simulated_clock",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
    alwayslink = 1,
)

cc_test(
    name = "selectables_test",
    linkstatic = 1,
    deps = [
        ":selectables_test_lib",
    ],
)
