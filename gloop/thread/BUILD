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

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

POSIX_PORT_DEPS = [
    ":run_in_thread",
    ":thread_control",
    "@abseil-cpp//absl/base",
    "@abseil-cpp//absl/container:flat_hash_map",
    "//gloop/base/scheduling",
    "//gloop/base:per-thread-sem",
    "//gloop/base:process_state",
    "//gloop/base:signal-handler",
    "//gloop/util/callback:cancellable_closure",
    "//gloop/util/gtl:intrusive_list",
    "@abseil-cpp//absl/algorithm",
    "@abseil-cpp//absl/types:source_location",
    "@abseil-cpp//absl/memory",
]

POSIX_PORT_SOURCES = [
    "add_after_helper.cc",
    "cpu_subcontainer.h",  # Stubbed out, private.
    "executor.cc",
    "logger.h",
    "pcqueue.h",  # private.
    "port/cpu_subcontainer.cc",
    "python_stack_size.h",
    "timedcall.cc",
    "thread.cc",
    "thread_options.cc",
    "threadlocal.cc",
    "threadlocal-internal.h",
    "threadpool.cc",
    "watchdog.h",  # private.
    "watchdog.cc",
]

# Headers of portable implementation of //thread for non-prod posix platforms.
POSIX_PORT_HEADERS = [
    "add_after_helper.h",
    "executor.h",
    "ordered-producer-consumer-queue.h",
    "thread.h",
    "threadlocal.h",
    "threadpool.h",
    "thread_options.h",
    "timedcall.h",
    "wait_queue_thread.h",
]

# Sources of portable implementation of //thread for non-posix platforms.
PORT_SOURCES = [
    "port/threadpool.cc",
    "port/threadlocal.h",
    "port/threadpool.h",
]

# Headers of portable implementation of //thread for non-posix platforms.
PORT_HEADERS = [
    "threadlocal.h",
    "threadpool.h",
]

cc_library(
    name = "config",
    hdrs = ["config.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

# Defines the exit_timeout_seconds flag.
cc_library(
    name = "exit_timeout_seconds",
    srcs = ["exit_timeout_seconds.cc"],
    hdrs = ["exit_timeout_seconds.h"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "thread-internal",
    hdrs = ["thread-internal.h"],
    visibility = [
        "//gloop/thread:__subpackages__",
    ],
    deps = [
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/util/gtl:intrusive_list",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "thread",
    srcs = select({
        "//conditions:default": [
            "add_after_helper.cc",
            "executor.cc",
            "logger.cc",
            "port/cpu_subcontainer.cc",
            "stack_reclaimer.cc",
            "stack_reclaimer.h",
            "thread.cc",
            "thread_options.cc",
            "threadlocal.cc",
            "threadlocal-internal.h",
            "threadpool.cc",
            "timedcall.cc",
            "watchdog.cc",
        ],
    }),
    hdrs = select({
        "//conditions:default": POSIX_PORT_HEADERS + [
            "cpu_subcontainer.h",
            "logger.h",
            "pcqueue.h",
            "watchdog.h",
        ],
    }),
    linkopts = select({
        "//conditions:default": ["-ldl"],
    }),
    deps = [
        ":config",
        ":exit_timeout_seconds",
        ":os_semaphore",
        ":prioritized_wait_queue",
        ":python_stack_size",
        ":sync_queue",
        ":thread-internal",
        ":wait_queue",
        ":wait_state",
        "//gloop/base:atomic_sequence_num",
        "//gloop/base:callback",
        "//gloop/base:config",
        "//gloop/base:context",
        "//gloop/base:examine_stack",
        "//gloop/base:getpc",
        "//gloop/base:googleinit",
        "//gloop/base:init_google_flags",
        "//gloop/base:logger",
        "//gloop/base:port",
        "//gloop/base:raw_printer",
        "//gloop/base:spinlock",
        "//gloop/base:static_threadlocal",
        "//gloop/base:sysinfo",
        "//gloop/base:thread_identity",
        "//gloop/base:tracecontext",
        "//gloop/base:walltime",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/strings:util",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "//gloop/util/functional:with_context",
        "//gloop/util/gtl:intrusive_heap",
        "//gloop/util/gtl:iterator_adaptors",
        "//gloop/util/intops",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:malloc_internal",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:chunked_queue",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/functional:overload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/time:clock_interface",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
        "@gloop//gloop/base:timer",
        "@googletest//:gtest_prod",
        "@tcmalloc//tcmalloc:malloc_extension",
    ] + select({
        "//conditions:default": POSIX_PORT_DEPS + [
            "//gloop/util/priority:io-priority",
        ],
    }),
)

cc_library(
    name = "os_semaphore",
    hdrs = ["os_semaphore.h"],
    visibility = [
        "//gloop/thread:__subpackages__",
        "@gloop//gloop/thread:__subpackages__",
        "@gloop//gloop/thread/fiber:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "os_semaphore_test",
    size = "small",
    srcs = ["os_semaphore_test.cc"],
    deps = [
        ":os_semaphore",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "prioritized_wait_queue",
    hdrs = ["prioritized_wait_queue.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

# Only for direct use by core C++ libraries that spawn threads. -python-team
cc_library(
    name = "python_stack_size",
    srcs = ["python_stack_size.cc"],
    hdrs = ["python_stack_size.h"],
    visibility = [
        "//gloop/thread:__subpackages__",
        "@gloop//gloop/thread:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "run_in_thread",
    srcs = ["run_in_thread.cc"],
    hdrs = ["run_in_thread.h"],
    visibility = [
    ],
    deps = [
        ":os_semaphore",
        ":thread-internal",
        "//gloop/base:port",
        "//gloop/base:signal-handler",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "stack_reclaimer",
    hdrs = ["stack_reclaimer.h"],
    visibility = [
        "//gloop/thread:__subpackages__",
        "@gloop//gloop/thread:__subpackages__",
    ],
    deps = [
        ":thread",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "sync_queue",
    hdrs = ["sync_queue.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "wait_queue",
    hdrs = ["wait_queue.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "wait_state",
    srcs = ["wait_state.cc"],
    hdrs = ["wait_state.h"],
    deps = [
        "//gloop/base:thread_identity",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "thread_benchmark",
    srcs = ["thread_benchmark.cc"],
    deps = [
        ":thread",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "synchronization_benchmark",
    srcs = ["synchronization_benchmark.cc"],
    deps = [
        ":thread",
        "//gloop/base:thread_identity",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "cpuusage_test",
    size = "medium",
    srcs = ["cpuusage_test.cc"],
    deps = [
        ":thread",
        "//gloop/base:init_google",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "pcqueue_unittest",
    srcs = ["pcqueue_unittest.cc"],
    deps = [
        ":thread",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "thread_unittest",
    srcs = [
        "thread-internal.h",
        "thread_unittest.cc",
    ],
    deps = [
        ":config",
        ":thread",
        ":thread-internal",
        ":thread_control",
        "//gloop/base:callback",
        "//gloop/base:context",
        "//gloop/base:init_google",
        "//gloop/base:port",
        "//gloop/base:signal-handler",
        "//gloop/base:spinlock",
        "//gloop/base:sysinfo",
        "//gloop/base:timer",
        "//gloop/base:tracecontext",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/util/functional:to_callback",
        "//gloop/util/gtl:container_logging",
        "//gloop/util/gtl:intrusive_list",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@re2",
    ],
)

cc_library(
    name = "thread_control",
    srcs = ["thread_control.cc"],
    hdrs = ["thread_control.h"],
    # Visibility is intentionally restricted and new uses of the single
    # threaded controller will not be accepted.
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/thread:__subpackages__",
    ],
    deps = [
        ":config",
        "@abseil-cpp//absl/log",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "threadpool_unittest",
    srcs = ["threadpool_unittest.cc"],
    deps = [
        ":thread",
        "//gloop/base:atomic_stats_counter",
        "//gloop/concurrent/barrier",
        "//gloop/util/refcount:blocking_refcount",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_test(
    name = "threadpool_setup_unittest",
    srcs = ["threadpool_setup_unittest.cc"],
    deps = [
        ":thread",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_test(
    name = "threadpool_setup_unittest_static",
    srcs = ["threadpool_setup_unittest.cc"],
    linkstatic = 1,
    deps = [
        ":thread",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_test(
    name = "guard_unittest",
    size = "medium",
    srcs = ["guard_unittest.cc"],
    deps = [
        ":thread",
        "//gloop/base:address_is_readable",
        "//gloop/base:init_google",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_test(
    name = "threadlocal_unittest",
    srcs = ["threadlocal_unittest.cc"],
    deps = [
        ":thread",
        "//gloop/concurrent/barrier",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "inline_executor_test",
    size = "small",
    srcs = ["inline_executor_test.cc"],
    deps = [
        ":thread",
        "@abseil-cpp//absl/functional:bind_front",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "timedcall_unittest",
    srcs = ["timedcall_unittest.cc"],
    deps = [
        ":thread",
        "//gloop/base:walltime",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "thread_default_stack_unittest",
    srcs = ["thread_default_stack_unittest.cc"],
    copts = ["$(STACK_FRAME_UNLIMITED)"],  # thread_default_stack_unittest.cc
    deps = [
        ":thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:initialize",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "logger_test",
    srcs = ["logger_test.cc"],
    deps = [
        ":thread",
        "//gloop/base:init_google",
        "//gloop/base:logger",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "port_threadlocal_test",
    size = "small",
    srcs = ["port/threadlocal_test.cc"],
    copts = ["-Wno-deprecated-volatile"],
    deps = [
        ":thread",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "port_threadpool_test",
    size = "medium",
    srcs = ["port/threadpool_test.cc"],
    deps = [
        ":thread",
        "//gloop/base:timer",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "named_vma_test",
    srcs = ["named_vma_test.cc"],
    linkstatic = 1,
    deps = [
        ":thread",
        "//gloop/base:proc_maps",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "prioritized_wait_queue_test",
    srcs = ["prioritized_wait_queue_test.cc"],
    deps = [
        ":prioritized_wait_queue",
        ":thread",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "sync_queue_test",
    srcs = ["sync_queue_test.cc"],
    deps = [
        ":sync_queue",
        ":thread",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "wait_queue_test",
    srcs = ["wait_queue_test.cc"],
    deps = [
        ":thread",
        ":wait_queue",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "wait_state_test",
    size = "small",
    srcs = ["wait_state_test.cc"],
    tags = ["notsan"],
    deps = [
        ":thread",
        ":thread-internal",
        ":wait_state",
        "//gloop/base:thread_identity",
        "//gloop/base:walltime",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
