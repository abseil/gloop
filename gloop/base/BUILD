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
    ],
    deps = [
        "@abseil-cpp//absl/debugging:debugging_internal",
        "@gloop//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/log:check",
    ],
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
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "censushandle",
    hdrs = ["censushandle.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "censushandle_test",
    srcs = ["censushandle_test.cc"],
    deps = [
        ":censushandle",
        ":censushandle_test_entry",
        "@abseil-cpp//absl/base:no_destructor",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "censushandle_test_entry",
    testonly = 1,
    hdrs = ["censushandle_test_entry.h"],
    deps = [
        ":censushandle",
        "@abseil-cpp//absl/base",
        "@gloop//gloop:enforce_gloop_support",
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

cc_library(
    name = "context",
    srcs = ["context.cc"],
    hdrs = ["context.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":context_access",
        ":tracecontext",
        "//gloop/base:censushandle",
        "//gloop/base:static_threadlocal",
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
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "context_access",
    hdrs = ["context_access.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "context_test",
    size = "medium",
    srcs = ["context_test.cc"],
    deps = [
        ":context",
        ":context_access",
        ":sysinfo",
        ":tracecontext",
        "//gloop/base:censushandle",
        "//gloop/perftools/tracing:mock_trace_event_listener",
        "//gloop/perftools/tracing:tracing_base",
        "//gloop/util/refcount:reffed_ptr",
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
        "@googletest//:gtest_main",
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
        "//gloop/util/atomic_danger:refcount",
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "context_origin_test",
    srcs = ["context_origin_test.cc"],
    deps = [
        ":context_origin",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "coredump_flags",
    srcs = ["coredump_flags.cc"],
    hdrs = ["coredump_flags.h"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
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

cc_library(
    name = "init_google_flags",
    srcs = ["init_google_flags.cc"],
    hdrs = ["init_google_flags.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop:enforce_gloop_support",
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
        ":signal-handler",
        "//gloop/base:config",
        "//gloop/base:examine_stack",
        "//gloop/base:tracecontext",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
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
        "//gloop/base/scheduling",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@gloop//gloop:enforce_gloop_support",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
    alwayslink = 1,
)

# This version of per-thread-sem_test links in //per-thread-sem, therefore
# picking up the symbol overrides.
cc_test(
    name = "per-thread-sem_test",
    size = "medium",
    tags = [
        "noubsan",  # TODO Times out on UBSAN+ARM
    ],
    deps = [
        ":per-thread-sem",
        "@abseil-cpp//absl/synchronization:per_thread_sem_test_common",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
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
        ":global_symbol_compliance",
        ":scoped_sigmask",
        "//gloop/base:config",
        "//gloop/base:spinlock",
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
        "//gloop/base:port",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "proc_maps",
    srcs = ["proc_maps.cc"],
    hdrs = ["proc_maps.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:port",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "proc_maps_test",
    srcs = ["proc_maps_test.cc"],
    deps = [
        ":proc_maps",
        "@abseil-cpp//absl/strings:string_view",
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
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@gloop//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
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
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@gloop//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "strerror",
    hdrs = ["strerror.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base:strerror",
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
    name = "sysinfo",
    srcs = ["sysinfo.cc"],
    hdrs = ["sysinfo.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":proc_maps",
        ":strerror",
        "//gloop/base:port",
        "//gloop/base/auxiliary:parsed_process_stat",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

sh_test(
    name = "sysinfo_errorlog_unittest",
    size = "small",
    srcs = ["sysinfo_errorlog_unittest.sh"],
    data = [
        "//gloop/base:sysinfo_unittest",
    ],
)

cc_test(
    name = "sysinfo_portable_test",
    size = "small",
    srcs = ["sysinfo_portable_test.cc"],
    deps = [
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sysinfo_unittest",
    size = "small",
    srcs = ["sysinfo_unittest.cc"],
    data = ["//gloop/base/testdata:sysinfo_test_proc"],
    malloc = "//gloop:malloc",
    deps = [
        ":proc_maps",
        "//gloop/base:sysinfo",
        "//gloop/base/auxiliary:parsed_process_stat",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:reflection",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_library(
    name = "timer",
    srcs = ["timer.cc"],
    hdrs = ["timer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":walltime",
        "//gloop/base:port",
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
    name = "tracecontext",
    srcs = ["tracecontext.cc"],
    hdrs = ["tracecontext.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":censushandle",
        ":context_origin",
        "//gloop/base:port",
        "//gloop/base:sysinfo",
        "//gloop/perftools/tracing:sync_context",
        "//gloop/perftools/tracing:trace_event_listener",
        "//gloop/perftools/tracing:trace_source_location",
        "//gloop/perftools/tracing:tracing_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
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

cc_library(
    name = "examine_stack",
    srcs = ["examine_stack.cc"],
    hdrs = ["examine_stack.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":proc_maps",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/debugging:examine_stack",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/log/internal:globals",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "static_threadlocal",
    srcs = ["static_threadlocal.cc"],
    hdrs = ["static_threadlocal.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":per_thread",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "thread_identity",
    hdrs = ["thread-identity.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/base",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "examine_stack_unittest",
    srcs = ["examine_stack_unittest.cc"],
    deps = [
        "//gloop/base:examine_stack",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@re2",
    ],
)

cc_library(
    name = "per_thread",
    srcs = ["per_thread.cc"],
    hdrs = ["per_thread.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/util/atomic_danger",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:malloc_internal",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@gloop//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
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
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ] + select({
        "//conditions:default": [],
    }),
)

cc_test(
    name = "per_thread_test",
    size = "medium",
    srcs = ["per_thread_test.cc"],
    deps = [
        ":per_thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
    deps = ["@gloop//gloop:enforce_gloop_support"],
    alwayslink = 1,
)

cc_library(
    name = "futex",
    srcs = ["futex.cc"],
    hdrs = ["futex.h"],
    visibility = [
        "//gloop/base:__subpackages__",
        "//gloop/concurrent/percpu:__pkg__",
    ],
    deps = [
        ":inlined_syscall",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "inlined_syscall",
    hdrs = ["inlined_syscall.h"],
    visibility = [
        "//gloop/base:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "fprint",
    hdrs = ["fprint.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "atomic_stats_counter",
    hdrs = ["atomic_stats_counter.h"],
    visibility = ["//visibility:public"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
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
        ":on_fatal_log_message",
        ":port",
        ":signal-handler",
        ":sysinfo",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:base_internal",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
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
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:source_location",
        "@gloop//gloop:enforce_gloop_support",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_test(
    name = "process_state_test",
    size = "small",
    srcs = ["process_state_test.cc"],
    deps = [
        ":process_state",
        ":signal-handler",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
