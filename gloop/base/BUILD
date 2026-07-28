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
    "@rules_cc//cc:cc_binary.bzl",
    "cc_binary",
)
load(
    "@rules_cc//cc:cc_library.bzl",
    "cc_library",
)
load(
    "@rules_cc//cc:cc_test.bzl",
    "cc_test",
)
load("@rules_shell//shell:sh_test.bzl", "sh_test")

# Dependencies of //gloop/base that rely on POSIX support.
POSIX_DEPS = [
    ":mutex_yield",
    ":nsscache",
    ":per-thread-sem",
    ":process_state",
    ":signal-handler",
    ":sleep",
    ":spinlock_wait",
    "//gloop/base/scheduling",
]

package_group(
    name = "base_implementation",
    packages = [
        "//gloop/base/...",
        "//gloop/concurrent/...",
    ],
)

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = [":base_implementation"],
    features = ["header_modules"],
)

cc_library(
    name = "address_is_readable",
    hdrs = ["address_is_readable.h"],
    visibility = [
        "//gloop/thread:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/debugging:debugging_internal",
    ],
)

cc_test(
    name = "address_is_readable_test",
    size = "small",
    srcs = [
        "address_is_readable_test.cc",
    ],
    deps = [
        ":address_is_readable",
        ":strerror",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "atomic_sequence_num",
    hdrs = ["atomic_sequence_num.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "atomic_sequence_num_test",
    size = "medium",
    srcs = ["atomic_sequence_num_test.cc"],
    deps = [
        ":atomic_sequence_num",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "callback-types",
    srcs = ["callback.cc"],
    hdrs = ["callback-types.h"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/util/functional:__subpackages__",
        "@protobuf//compiler/cpp:__pkg__",
    ],
    deps = [
        ":context",
        ":tracecontext",
        "//gloop:enforce_gloop_support",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "callback",
    hdrs = ["callback.h"],
    visibility = ["//gloop:__subpackages__"],
    deps = [
        ":callback-types",
        "//gloop:enforce_gloop_support",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
    ],
)

cc_test(
    name = "callback_unittest",
    size = "small",
    srcs = ["callback_unittest.cc"],
    deps = [
        ":callback",
        "//gloop:gloop_test_main",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "cancellation_coloring",
    srcs = ["cancellation_coloring.cc"],
    hdrs = ["cancellation_coloring.h"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        ":sysinfo",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "cancellation_coloring_test",
    size = "small",
    srcs = ["cancellation_coloring_test.cc"],
    deps = [
        ":cancellation_coloring",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "censushandle",
    hdrs = ["censushandle.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "censushandle_test",
    srcs = ["censushandle_test.cc"],
    deps = [
        ":censushandle",
        ":censushandle_test_entry",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:no_destructor",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "censushandle_test_entry",
    testonly = 1,
    hdrs = ["censushandle_test_entry.h"],
    deps = [
        ":censushandle",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_library(
    name = "config",
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:config",
    ],
)

cc_library(
    name = "context",
    srcs = ["context.cc"],
    hdrs = ["context.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":censushandle",
        ":context_access",
        ":static_threadlocal",
        ":tracecontext",
        "//gloop:enforce_gloop_support",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/util/refcount:reffed_ptr",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "context_access",
    hdrs = ["context_access.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "context_test",
    size = "medium",
    srcs = ["context_test.cc"],
    deps = [
        ":callback",
        ":censushandle",
        ":context",
        ":context_access",
        ":sysinfo",
        ":tracecontext",
        "//gloop:gloop_test_main",
        "//gloop/perftools/tracing:mock_trace_event_listener",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/thread",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "//gloop/util/refcount:reffed_ptr",
        "//gloop/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ] + select({
        "//conditions:default": [],
    }),
)

cc_library(
    name = "context_origin",
    hdrs = ["context_origin.h"],
    visibility = [
        "//gloop/base:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/atomic_danger:refcount",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "context_origin_test",
    srcs = ["context_origin_test.cc"],
    deps = [
        ":context_origin",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "coredump_flags",
    srcs = ["coredump_flags.cc"],
    hdrs = ["coredump_flags.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "crash",
    srcs = ["crash.cc"],
    hdrs = ["crash.h"],
    visibility = [
        ":base_implementation",
        "//gloop/thread:__pkg__",
    ],
    deps = [
        ":config",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "cycleclock",
    hdrs = ["cycleclock.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_test(
    name = "cycleclock_test",
    size = "small",
    srcs = ["cycleclock_test.cc"],
    flaky = 1,
    deps = [
        ":config",
        ":cycleclock",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:cycleclock_internal",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "getpc",
    hdrs = ["getpc.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:raw_logging_internal",
    ],
)

cc_test(
    name = "getpc_test",
    size = "small",
    srcs = ["getpc_test.cc"],
    tags = [
        "notsan",  # TODO: Investigate.
    ],
    deps = [":getpc"],
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
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "global_symbol_compliant_test",
    srcs = ["global_symbol_compliant_test.cc"],
    deps = [
        ":global_symbol_compliance",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "hostname",
    srcs = ["hostname.cc"],
    hdrs = ["hostname.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "hostname_test",
    size = "small",
    srcs = ["hostname_test.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":hostname",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "init_google_flags",
    srcs = ["init_google_flags.cc"],
    hdrs = ["init_google_flags.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings:string_view",
    ],
    alwayslink = 1,
)

cc_library(
    name = "linux_syscall_support",
    hdrs = ["linux_syscall_support.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base/auxiliary:syscall_linux",
    ],
)

cc_test(
    name = "linux_syscall_support_unittest",
    size = "small",
    srcs = ["linux_syscall_support_unittest.cc"],
    features = [
        "-leak_sanitize",
    ],
    linkstatic = 1,
    # Can't use lsan, therefore bypass lsan malloc (see <link>).
    malloc = "//gloop:malloc",
    tags = [
        "notsan",  # TODO: Investigate.
    ],
    deps = [
        ":linux_syscall_support",
        ":linux_syscall_support_unittest_helper",
        ":log_file_flags",
        "//gloop/thread:thread_control",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "linux_syscall_support_unittest_helper",
    srcs = ["linux_syscall_support_unittest_helper.cc"],
    hdrs = ["linux_syscall_support_unittest_helper.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

# Test for sanitizer call backs by linux_syscall_support.h functions.
cc_test(
    name = "linux_syscall_support_hooks_unittest",
    size = "small",
    srcs = [
        # We redefine stuff before including this.
        "linux_syscall_support.h",
        "linux_syscall_support_hooks_unittest.cc",
    ],
    tags = [
        "noasan",
        "nomsan",
        "notsan",
    ],
    deps = [
        "//gloop:gloop_test_main",
        "//gloop/base/auxiliary:syscall_linux",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "mutex_yield",
    srcs = ["mutex_yield.cc"],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base:core_headers",
    ],
    alwayslink = 1,
)

cc_library(
    name = "on_fatal_log_message",
    srcs = ["on_fatal_log_message.cc"],
    hdrs = ["on_fatal_log_message.h"],
    visibility = [
        "//gloop:__subpackages__",
    ],
    deps = [
        ":config",
        ":context",
        ":crash",
        ":examine_stack",
        ":signal-handler",
        ":tracecontext",
        ":tracer",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
    alwayslink = True,
)

PERCPU_LINUX_DEPS = [
    "@tcmalloc//tcmalloc/internal:sysinfo",
]

cc_library(
    name = "per-thread-sem",
    srcs = ["per-thread-sem.cc"],
    hdrs = ["per-thread-sem.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":spinlock",
        "//gloop:enforce_gloop_support",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
    alwayslink = 1,
)

# This version of per-thread-sem_test links in //per-thread-sem, therefore
# picking up the symbol overrides.
cc_test(
    name = "per-thread-sem_test",
    size = "medium",
    malloc = ":system_malloc",
    tags = [
        "noubsan",  # TODO Times out on UBSAN+ARM
    ],
    deps = [
        ":per-thread-sem",
        "@abseil-cpp//absl/synchronization:per_thread_sem_test_common",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

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
        "//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "percpu_test",
    size = "large",
    srcs = ["percpu_test.cc"],
    linkstatic = 1,
    shard_count = 7,  # Run long-running tests in parallel.
    tags = [
        # numactl causes sanitizer false-positives from the numa_init constructor.
        "nosan",
    ],
    visibility = [
        ":base_implementation",
    ],
    deps = [
        ":callback",
        ":init_google",
        ":log_file_flags",
        ":percpu",
        ":proc_maps",
        ":signal_util_subtle",
        ":static_threadlocal",
        ":sysinfo",
        "//gloop/base/scheduling",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:fifolifo-schedulers",
        "//gloop/thread/fiber:select",
        "//gloop/util/functional:to_callback",
        "//gloop/util/gtl:unique_array",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@numactl//:numa",
        "@tcmalloc//tcmalloc/internal:linux_syscall_support",
    ],
)

cc_test(
    name = "percpu_early_test",
    srcs = ["percpu_early_test.cc"],
    linkstatic = 1,
    deps = [
        ":percpu",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "percpu_fork_test",
    size = "large",
    srcs = ["percpu_fork_test.cc"],
    linkstatic = 1,
    deps = [
        ":percpu",
        ":raw_logging",
        "//gloop/thread:thread_control",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@tcmalloc//tcmalloc/internal:linux_syscall_support",
    ],
)

cc_test(
    name = "percpu_handle_early_test",
    srcs = ["percpu_handle_early_test.cc"],
    linkstatic = 1,
    deps = [
        ":percpu",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "percpu_compat_test",
    srcs = ["percpu_compat_test.cc"],
    deps = [
        ":percpu",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "percpu_dynamic_init_test_none",
    srcs = ["percpu_dynamic_init_test.cc"],
    args = ["--expected_vcpu_mode=0"],
    env = {"PERCPU_VCPU_MODE": "none"},
    linkstatic = 1,
    deps = [
        ":percpu",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "percpu_dynamic_init_test_default",
    srcs = ["percpu_dynamic_init_test.cc"],
    args = ["--expected_vcpu_mode=1"],
    env = {"BORG_EXPERIMENTS": ""},
    linkstatic = 1,
    deps = [
        ":percpu",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "percpu_dynamic_init_test_flat",
    srcs = ["percpu_dynamic_init_test.cc"],
    args = ["--expected_vcpu_mode=1"],
    env = {
        "BORG_EXPERIMENTS": "",
        "PERCPU_VCPU_MODE": "flat",
    },
    linkstatic = 1,
    deps = [
        ":percpu",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Platform specific library that currently contains implementations of snprintf
# and vsnprintf for Windows.
cc_library(
    name = "port",
    hdrs = ["port.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
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
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "proc_maps",
    srcs = ["proc_maps.cc"],
    hdrs = ["proc_maps.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":port",
        ":spinlock",
        "//gloop:enforce_gloop_support",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "proc_maps_test",
    srcs = ["proc_maps_test.cc"],
    deps = [
        ":proc_maps",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# As with heap-checker, this is always optimized.
cc_library(
    name = "raw_printer",
    srcs = ["raw_printer.cc"],
    hdrs = ["raw_printer.h"],
    copts = ["-O3"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/thread:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "raw_printer_test",
    srcs = ["raw_printer_test.cc"],
    deps = [
        ":raw_printer",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "raw_logging",
    srcs = ["raw_logging.cc"],
    hdrs = ["raw_logging.h"],
    visibility = ["//gloop/thread:__subpackages__"],
    deps = [
        ":config",
        ":crash",
        ":examine_stack",
        ":sysinfo",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log/internal:format",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "raw_logging_test",
    size = "small",
    srcs = ["raw_logging_test.cc"],
    deps = [
        ":raw_logging",
        "@abseil-cpp//absl/base:config",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "scoped_sigmask",
    hdrs = ["scoped_sigmask.h"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:raw_logging_internal",
    ],
)

cc_library(
    name = "signal-handler",
    srcs = ["signal-handler.cc"],
    hdrs = ["signal-handler.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        ":sysinfo",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "sleep",
    srcs = ["sleep.cc"],
    deps = [
        ":per-thread-sem",
        "//gloop:enforce_gloop_support",
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/time",
    ],
    alwayslink = 1,
)

cc_library(
    name = "spinlock",
    hdrs = ["spinlock.h"],
    visibility = [
        "//:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_test(
    name = "spinlock_unittest",
    size = "medium",
    srcs = ["spinlock_unittest.cc"],
    deps = [
        ":config",
        ":init_google",
        ":per-thread-sem",
        ":spinlock",
        ":sysinfo",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:low-level-support",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:fifolifo-schedulers",
        "//gloop/util/hash:legacy_hash",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# This version of spinlock_test links in //gloop/spinlock_wait, therefore
# picking up the Gloop-specific symbol overrides for AbslInternalSpinLockDelay
# and AbslInternalSpinLockWake.
cc_test(
    name = "spinlock_test",
    size = "medium",
    deps = [
        ":spinlock",
        ":spinlock_wait",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:spinlock_test_common",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "spinlock_wait",
    srcs = ["spinlock_wait.cc"],
    hdrs = ["spinlock_wait.h"],
    visibility = [
        "//gloop/base:__pkg__",
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/base:base_internal",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/base:spinlock_wait",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
    alwayslink = 1,
)

cc_binary(
    name = "libthreadidentitytest_shared_lib.so",
    testonly = 1,
    srcs = [
        "thread-identity_test_shared.cc",
    ],
    linkshared = 1,
    linkstatic = 1,
    deps = [
        ":per-thread-sem",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "thread-identity_test_shared",
    srcs = ["thread-identity_test_shared.cc"],
    copts = ["-DABSL_THREADIDENTITYTEST_SHARED_MAIN"],
    data = [":libthreadidentitytest_shared_lib.so"],
    linkstatic = 1,
    deps = [
        ":per-thread-sem",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "strerror",
    hdrs = ["strerror.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:strerror",
    ],
)

cc_library(
    name = "strtoint",
    srcs = ["strtoint.cc"],
    hdrs = ["strtoint.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "strtoint_test",
    size = "small",
    srcs = ["strtoint_test.cc"],
    deps = [
        ":strtoint",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "sysinfo",
    srcs = ["sysinfo.cc"],
    hdrs = ["sysinfo.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":port",
        ":proc_maps",
        ":strerror",
        "//gloop:enforce_gloop_support",
        "//gloop/base/auxiliary:parsed_process_stat",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

sh_test(
    name = "sysinfo_errorlog_unittest",
    size = "small",
    srcs = ["sysinfo_errorlog_unittest.sh"],
    data = [
        ":sysinfo_unittest",
    ],
)

cc_test(
    name = "sysinfo_portable_test",
    size = "small",
    srcs = ["sysinfo_portable_test.cc"],
    deps = [
        ":sysinfo",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "sysinfo_unittest",
    size = "small",
    srcs = ["sysinfo_unittest.cc"],
    args = [
        # Some of the tests fork() so we don't want to launch threads in InitGoogle.
        "--threaded_logging=false",
        "--logtostderr=true",
    ],
    malloc = "//gloop:malloc",
    deps = [
        ":commandlineflags",
        ":googleinit",
        ":proc_maps",
        ":sysinfo",
        "//gloop:gloop_test_main",
        "//gloop/base/auxiliary:parsed_process_stat",
        "//gloop/thread",
        "//gloop/util/process",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:reflection",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

# Empty rule to force the use of the system malloc library.
# When used in the 'malloc' attribute of a cc_binary rule, it prevents the
# binary from being linked into any custom malloc library, and thus ensures the
# system malloc is used.
cc_library(
    name = "system_malloc",
    linkstatic = 1,
    visibility = ["//gloop:__subpackages__"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "timer",
    srcs = ["timer.cc"],
    hdrs = ["timer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":port",
        ":walltime",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "timer_unittest",
    size = "medium",
    srcs = ["timer_unittest.cc"],
    flaky = 1,
    deps = [
        ":config",
        ":timer",
        "//gloop:gloop_test_main",
        "//gloop/util/gtl:unique_array",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "time_support",
    srcs = ["time_support.cc"],
    hdrs = ["time_support.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "time_support_unittest",
    size = "medium",
    srcs = ["time_support_unittest.cc"],
    deps = [
        ":time_support",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "tracecontext",
    srcs = ["tracecontext.cc"],
    hdrs = ["tracecontext.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":censushandle",
        ":context_access",
        ":context_origin",
        ":port",
        ":sysinfo",
        ":tracer",
        ":xray_tracing_annotations",
        "//gloop:enforce_gloop_support",
        "//gloop/perftools/tracing:sync_context",
        "//gloop/perftools/tracing:trace_event_listener",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "uword",
    hdrs = ["uword.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "walltime",
    srcs = ["walltime.cc"],
    hdrs = ["walltime.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "walltime_unittest",
    size = "medium",
    srcs = ["walltime_unittest.cc"],
    deps = [
        ":walltime",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "examine_stack",
    srcs = ["examine_stack.cc"],
    hdrs = ["examine_stack.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":proc_maps",
        "//gloop:enforce_gloop_support",
        "//gloop/base:signal-handler",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/debugging:examine_stack",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "static_threadlocal",
    srcs = ["static_threadlocal.cc"],
    hdrs = ["static_threadlocal.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":per_thread",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "thread_identity",
    hdrs = ["thread-identity.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_test(
    name = "examine_stack_unittest",
    srcs = ["examine_stack_unittest.cc"],
    deps = [
        ":examine_stack",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@re2",
    ],
)

cc_test(
    name = "examine_stack_fuzz_test",
    srcs = ["examine_stack_fuzz_test.cc"],
    deps = [
        ":examine_stack",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings",
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "per_thread",
    srcs = ["per_thread.cc"],
    hdrs = ["per_thread.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":spinlock",
        "//gloop:enforce_gloop_support",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/util/atomic_danger",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:malloc_internal",
        "@abseil-cpp//absl/base:raw_logging_internal",
    ],
)

cc_library(
    name = "signal_util_subtle",
    srcs = ["signal_util_subtle.cc"],
    hdrs = ["signal_util_subtle.h"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "static_threadlocal_test",
    size = "medium",
    srcs = [
        "static_threadlocal_test.cc",
        "static_threadlocal_test_static_order.cc",
    ],
    linkstatic = 1,
    deps = [
        ":static_threadlocal",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "//gloop/util/random:shared_bit_gen",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "per_thread_test",
    size = "medium",
    srcs = ["per_thread_test.cc"],
    deps = [
        ":per_thread",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "signal_util_subtle_test",
    size = "medium",
    srcs = ["signal_util_subtle_test.cc"],
    features = ["-use_header_modules"],
    deps = [
        ":percpu",
        ":signal_util_subtle",
        ":time_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# For tests that cannot coexist with fast-mode percpu,
# prevent its use by depending on this library.
# Do not use this unless you're actively working with
# restartable sequences somehow.
cc_library(
    name = "percpu_disable_rseq",
    testonly = 1,
    srcs = ["percpu_disable_rseq.cc"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
    alwayslink = 1,
)

cc_library(
    name = "futex",
    srcs = ["futex.cc"],
    hdrs = ["futex.h"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/concurrent/percpu:__pkg__",
        "//gloop/thread/fiber:__pkg__",
    ],
    deps = [
        ":inlined_syscall",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
    ],
)

cc_library(
    name = "inlined_syscall",
    hdrs = ["inlined_syscall.h"],
    visibility = [
        "//gloop/base:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "fprint",
    hdrs = ["fprint.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "futex_test",
    size = "small",
    srcs = ["futex_test.cc"],
    linkstatic = 1,
    deps = [
        ":futex",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "atomic_stats_counter",
    hdrs = ["atomic_stats_counter.h"],
    visibility = ["//visibility:public"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "atomic_stats_counter_test",
    size = "medium",
    srcs = ["atomic_stats_counter_test.cc"],
    deps = [
        ":atomic_stats_counter",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "process_state",
    srcs = ["process_state.cc"],
    hdrs = ["process_state.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":address_is_readable",
        ":config",
        ":coredump_flags",
        ":examine_stack",
        ":init_google_flags",
        ":logging_extensions",
        ":on_fatal_log_message",
        ":port",
        ":raw_logging",
        ":signal-handler",
        ":sysinfo",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:base_internal",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/debugging:examine_stack",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_test(
    name = "process_state_test",
    size = "small",
    srcs = ["process_state_test.cc"],
    features = [
        # This test uses alloca to push SP past stack limits.
        # Auto-initialization of resulting buffer causes a crash.
        # Turn that feature off for this test.
        "-stack_auto_init_pattern",
    ],
    deps = [
        ":config",
        ":process_state",
        ":signal-handler",
        "//gloop:gloop_test_main",
        "//gloop/base/internal:logging_directories",
        "//gloop/thread",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "limit_physical_memory_test",
    size = "small",
    srcs = ["limit_physical_memory_test.cc"],
    flaky = 1,  # TODO
    malloc = "//gloop:malloc",
    tags = [
        "noasan",
        "nomsan",
        "notsan",
    ],
    deps = [
        ":process_state",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "arena",
    srcs = ["arena.cc"],
    hdrs = ["arena.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_library(
    name = "arena_allocator",
    srcs = ["arena_allocator.cc"],
    hdrs = ["arena_allocator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":arena",
        ":port",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "arena_unittest",
    size = "medium",
    srcs = ["arena_unittest.cc"],
    deps = [
        ":arena",
        ":arena_allocator",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# test for leakage
cc_test(
    name = "arena_leakage_unittest",
    size = "small",
    srcs = ["arena_leakage_unittest.cc"],
    deps = [
        ":arena",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# =========================================================
# Some modules reference base header files directly, which does not
# work with default private visibility. We allowlist such uses by
# exporting these files explicitly to the existing users.
#
# The export rules intentionally have limited visibility so we can
# keep track of such external dependencies and ensure that future
# users work with base OWNERS on appropriately scoped rules that can
# be exported by base/BUILD.
exports_files(
    ["file_toc.h"],
    visibility = [
        "//gloop/tools:__pkg__",
    ],
)

# For other projects that just need the file_toc.h header
cc_library(
    name = "file_toc",
    hdrs = ["file_toc.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "googleinit",
    srcs = ["googleinit.cc"],
    hdrs = ["googleinit.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":init_google_flags",
        ":port",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "googleinit_unittest",
    size = "small",
    srcs = ["googleinit_unittest.cc"],
    args = ["--g_init_check_2=2"],
    deps = [
        ":googleinit",
        ":init_google",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "init_google",
    srcs = ["init_google.cc"],
    hdrs = ["init_google.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":commandlineflags",
        ":config",
        ":examine_stack",
        ":googleinit",
        ":init_google_flags",
        ":logging",
        ":raw_logging",
        ":spinlock",
        ":sysinfo",
        ":user_name",
        "//gloop:enforce_gloop_support",
        "//gloop/base/internal:init_google",
        "//gloop/base/internal:logging_globals",
        "//gloop/concurrent/rcu",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:base_internal",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/debugging:demangle_internal",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:program_name",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@tcmalloc//tcmalloc:malloc_extension",
    ] + select({
        "@platforms//os:linux": POSIX_DEPS,
        "//conditions:default": [],
    }),
)

cc_test(
    name = "init_google_test",
    size = "small",
    srcs = ["init_google_test.cc"],
    deps = [
        ":config",
        ":init_google",
        "//gloop:gloop_test_main",
        "//gloop/base/internal:init_google",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ] + select({
        "//conditions:default": [":process_state"],
    }),
)

cc_library(
    name = "nsscache",
    srcs = ["nsscache.cc"],
    hdrs = ["nsscache.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "nsscache_unittest",
    size = "medium",
    srcs = [
        "nsscache_unittest.cc",
    ],
    deps = [
        ":nsscache",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "nsscache_getgrnam_unittest",
    size = "medium",
    srcs = [
        "nsscache_getgrnam_unittest.cc",
    ],
    deps = [
        ":nsscache",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "logging",
    srcs = ["logging.cc"],
    hdrs = ["logging.h"],
    visibility = [
        ":base_implementation",
    ],
    deps = [
        ":commandlineflags",
        ":config",
        ":logger",
        ":logging_extensions",
        ":sysinfo",
        "//gloop:enforce_gloop_support",
        "//gloop/base/internal:logging",
        "//gloop/base/internal:logging_directories",
        "//gloop/base/internal:logging_globals",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/base:strerror",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/log/internal:conditions",
        "@abseil-cpp//absl/log/internal:config",
        "@abseil-cpp//absl/log/internal:flags",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:internal",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
    ],
)

cc_test(
    name = "logging_unittest",
    size = "small",
    timeout = "moderate",
    srcs = ["logging_unittest.cc"],
    data = ["//gloop/base/testdata:logging_test_golden_files"],
    deps = [
        ":config",
        ":init_google",
        ":init_google_flags",
        ":log_file_flags",
        ":logger",
        ":logging",
        ":logging_extensions",
        ":port",
        "//gloop/base/internal:gcapturedstream",
        "//gloop/base/internal:logging_directories",
        "//gloop/base/internal:munge_output",
        "//gloop/thread:config",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:malloc_internal",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:reflection",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/log/internal:flags",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ] + [
        "//gloop/thread/fiber",
    ],
)

cc_library(
    name = "logging_extensions",
    srcs = ["logging_extensions.cc"],
    hdrs = ["logging_extensions.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        ":logger",
        ":sysinfo",
        "//gloop:enforce_gloop_support",
        "//gloop/base/internal:logging_directories",
        "//gloop/base/internal:temp_directories",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/log/internal:config",
        "@abseil-cpp//absl/log/internal:log_message",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "logger",
    srcs = [
        "log_file.cc",
        "log_file_object.cc",
        "logger.cc",
    ],
    hdrs = [
        "log_file.h",
        "log_file_object.h",
        "log_severity.h",
        "logger.h",
    ],
    # Please use `absl::LogSink` instead of adding new uses of `base::Logger`.
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/thread:__pkg__",
        "//gloop/util/status:__pkg__",
    ],
    deps = [
        ":config",
        ":log_file_flags",
        ":port",
        ":user_name",
        "//gloop:enforce_gloop_support",
        "//gloop/base/internal:logging_directories",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:strerror",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:program_name",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "logging_extensions_test",
    size = "small",
    srcs = ["logging_extensions_test.cc"],
    args = [
        # --install_signal_handlers is needed to test FATAL/QFATAL interaction with RunOnFailure on some non-prod platforms.
        # Other platforms lack the flag altogether; rather than try to list them in a select() here we simply --undefok it.
        "--undefok=install_signal_handlers",
        "--install_signal_handlers",
    ],
    deps = [
        ":config",
        ":crash",
        ":init_google",
        ":logging_extensions",
        ":raw_logging",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/log:structured",
        "@abseil-cpp//absl/log/internal:test_helpers",
        "@abseil-cpp//absl/log/internal:test_matchers",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ] + select({
        "//conditions:default": [":process_state"],
    }),
)

cc_library(
    name = "log_file_flags",
    srcs = ["log_file_flags.cc"],
    hdrs = ["log_file_flags.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:marshalling",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "user_name",
    srcs = ["user_name.cc"],
    hdrs = ["user_name.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":nsscache",
        ":port",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
    ],
)

# TODO: run this in TAP projects with:
# 1) -DABSL_MIN_LOG_LEVEL={kInfo,kWarning,kError,kFatal,kLogDebugFatal,kInfinity}
# 2) --minloglevel={kInfo,kWarning,kError,kFatal,kLogDebugFatal,kInfinity}
# 3) both 1 and 2
# 4) -D{INFO,WARNING,ERROR,FATAL,DFATAL}
cc_test(
    name = "logger_test",
    size = "small",
    srcs = ["logger_test.cc"],
    deps = [
        ":config",
        ":log_file_flags",
        ":logger",
        ":logging_extensions",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log/internal:test_actions",
        "@abseil-cpp//absl/log/internal:test_helpers",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "user_name_test",
    srcs = ["user_name_test.cc"],
    deps = [
        ":user_name",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "varsetter",
    hdrs = ["varsetter.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/flags:flag",
    ],
)

cc_test(
    name = "varsetter_test",
    size = "small",
    srcs = ["varsetter_test.cc"],
    deps = [
        ":varsetter",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "commandlineflags",
    srcs = ["commandlineflags.cc"],
    hdrs = ["commandlineflags_declare.h"],
    # Textual include configurable by defining STRIP_FLAG_HELP.
    # Can't be in hdrs without breaking header_modules.
    # TODO: Fix this.
    textual_hdrs = ["commandlineflags.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        ":logging_extensions",
        ":strerror",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:commandlineflag",
        "@abseil-cpp//absl/flags:commandlineflag_internal",
        "@abseil-cpp//absl/flags:config",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:marshalling",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:path_util",
        "@abseil-cpp//absl/flags:private_handle_accessor",
        "@abseil-cpp//absl/flags:program_name",
        "@abseil-cpp//absl/flags:reflection",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/flags:usage_internal",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:source_location",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "commandlineflags_unittest",
    size = "medium",
    srcs = ["commandlineflags_unittest.cc"],
    data = ["commandlineflags_unittest_flagfile"],
    deps = [
        ":commandlineflags",
        ":config",
        ":init_google",
        ":log_file_flags",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:config",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:marshalling",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:reflection",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "mock_tracer",
    testonly = True,
    hdrs = ["mock_tracer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":tracer",
        ":tracing_types",
        "//gloop:enforce_gloop_support",
        "//gloop/perftools/tracing:trace_source_location",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)

cc_test(
    name = "mock_tracer_test",
    srcs = ["mock_tracer_test.cc"],
    deps = [
        ":mock_tracer",
        ":tracecontext",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "reference_tracker",
    srcs = ["reference_tracker.cc"],
    hdrs = ["reference_tracker.h"],
    deps = [
        ":spinlock",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/debugging:stacktrace",
    ],
)

cc_test(
    name = "reference_tracker_test",
    srcs = ["reference_tracker_test.cc"],
    deps = [
        ":reference_tracker",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/log",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "tracer",
    srcs = [
        # TODO: Break the Tracer<->TraceContext circular dependency.
        "tracecontext.h",
        "tracer.cc",
    ],
    hdrs = ["tracer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":censushandle",
        ":context_access",
        ":context_origin",
        ":port",
        ":reference_tracker",
        ":sysinfo",
        ":tracing_types",
        ":walltime",
        ":xray_tracing_annotations",
        "//gloop:enforce_gloop_support",
        "//gloop/base/internal:effective_user_id",
        "//gloop/base/time:time_unix_nanos",
        "//gloop/perftools/tracing",
        "//gloop/perftools/tracing:format_to_buffer_sink",
        "//gloop/perftools/tracing:sync_context",
        "//gloop/perftools/tracing:trace_event_listener",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
    ],
)

cc_library(
    name = "tracer_ffi",
    hdrs = ["tracer_ffi.h"],
    deps = [
        ":tracecontext",
        ":tracer",
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "tracer_test",
    size = "medium",
    timeout = "long",
    srcs = ["tracer_test.cc"],
    deps = [
        ":callback",
        ":context",
        ":context_access",
        ":mock_tracer",
        ":tracecontext",
        ":tracer",
        ":tracing_types",
        ":walltime",
        "//gloop:gloop_test_main",
        "//gloop/perftools/tracing:test_only_access",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/util/functional:from_callback",
        "//gloop/util/functional:to_callback",
        "//gloop/util/refcount:blocking_refcount",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "tracing_types",
    hdrs = ["tracing_types.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/functional:function_ref",
    ],
)

# Library for XRay RPC tracing
cc_library(
    name = "xray_tracing_annotations",
    hdrs = ["xray/tracing_annotations.h"],
    deps = ["//gloop:enforce_gloop_support"],
)
