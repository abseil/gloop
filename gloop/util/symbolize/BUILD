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
# that are not low level. For more information, see <link>
#
# Description: Code for symbolizing a stack trace of the current process.

load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

# Define a configurable build setting for optimization level.
bool_flag(
    name = "optimized",
    build_setting_default = True,
    visibility = ["//visibility:private"],
)

config_setting(
    name = "optimize",
    flag_values = {":optimized": "True"},
    visibility = ["//visibility:private"],
)

# Explicitly force this library to be built with optimizations turned
# on and debug checking off. This is often used within unit tests and
# other code build without optimizations, but *this* library is not
# the code under test or in need of debugging. We also turn off any
# debug checking done by the STL or other libraries.
COPTS = select({
    ":optimize": [
        "-O3",
        "-DNDEBUG",
    ],
    # No optimization if explicitly turned off via "optimize" setting.
    "//conditions:default": [],
})

cc_library(
    name = "demangle",
    srcs = ["demangle.cc"],
    hdrs = ["demangle.h"],
    copts = COPTS,
    features = ["-use_header_modules"],
    deps = ["@abseil-cpp//absl/debugging:demangle_rust"],
)

cc_test(
    name = "demangle_unittest",
    srcs = ["demangle_unittest.cc"],
    deps = [
        ":demangle",
        ":demangled_type_name",
        "//gloop/thread",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@re2",
    ],
)

cc_library(
    name = "demangled_type_name",
    hdrs = ["demangled_type_name.h"],
    deps = ["@abseil-cpp//absl/strings:string_view"],
)

cc_library(
    name = "elf_reader",
    srcs = ["elf_reader.cc"],
    hdrs = ["elf_reader.h"],
    copts = COPTS,
    deps = [
        ":symbol_map_sink",
        "//gloop/base:commandlineflags",
        "//gloop/base:port",
        "//gloop/testing/production_stub:testvalue",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/strings",
        "@zlib",
    ],
)

cc_test(
    name = "elf_reader_unittest",
    srcs = ["elf_reader_unittest.cc"],
    data = [
    ],
    deps = [
        ":elf_reader",
        ":symbolize",
        "//gloop/base:init_google",
        "//gloop/base:log_file_flags",
        "//gloop/base:proc_maps",
        "//gloop/base:strerror",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "elf_reader_sandbox_test",
    srcs = ["elf_reader_sandbox_test.cc"],
    deps = [
        ":elf_reader",
        "//gloop/base:init_google",
        "//gloop/testing/production_stub:testvalue",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# On an Apple OS this target will always supply empty results and fail to symbolize all addresses.
cc_library(
    name = "symbolize",
    srcs = [
        "symbolize.cc",
        "symbolized_stack_writer.cc",
        "symbolized_stacktrace.cc",
    ],
    hdrs = [
        "symbolize.h",
        "symbolize-inl.h",
        "symbolized_stack_writer.h",
        "symbolized_stacktrace.h",
    ],
    copts = COPTS,
    features = ["-use_header_modules"],
    deps = [
        ":demangle",
        ":symbol_map_sink",
        "//gloop/base:arena",
        "//gloop/base:proc_maps",
        "//gloop/strings:util",
        "//gloop/thread",
        "//gloop/util/coding:shrunk-array",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/debugging:debugging_internal",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ] + select({
        "//conditions:default": [
            ":elf_reader",
        ],
    }),
)

cc_test(
    name = "symbolize_unittest",
    srcs = ["symbolize_unittest.cc"],
    malloc = "//gloop:malloc",  # b/179154437
    deps = [
        ":demangle",
        ":symbolize",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/debugging:debugging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_test(
    name = "symbolized_stack_writer_unittest",
    srcs = ["symbolized_stack_writer_unittest.cc"],
    deps = [
        ":demangle",
        ":symbolize",
        "//gloop/thread",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "symbolized_stacktrace_unittest",
    srcs = ["symbolized_stacktrace_unittest.cc"],
    deps = [
        ":symbolize",
        "//gloop/base:examine_stack",
        "//gloop/base:init_google",
        "//gloop/base:log_file_flags",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Build foobar.cc with -fbasic-block-sections=all.
cc_library(
    name = "foobar_bbsections_all",
    testonly = 1,
    srcs = ["foobar.cc"],
    hdrs = ["foobar.h"],
    copts = ["-fbasic-block-sections=all"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
    deps = [
        ":symbolize",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "symbolize_dlopen_with_offset_test_helper",
    testonly = True,
    srcs = ["symbolize_dlopen_with_offset_test_helper.cc"],
)

cc_test(
    name = "symbolized_stacktrace_bb_sections_unittest",
    srcs = ["symbolized_stacktrace_bb_sections_unittest.cc"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
    deps = [
        ":foobar_bbsections_all",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

# Note: this doesn't *have* to be a cc_test (could be a cc_binary
# exercised by symbolize_manythreads_test_sh), but see b/118684285.
cc_test(
    name = "symbolize_manythreads_test",
    testonly = 1,
    srcs = ["symbolize_manythreads_test.cc"],
    malloc = "//gloop:malloc",
    deps = [
        ":symbolize",
        "//gloop/base:sysinfo",
        "//gloop/thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_library(
    name = "symbol_map_sink",
    hdrs = ["symbol_map_sink.h"],
    # We don't expect anyone to provide a different SymbolMapSink.
    # This library exists only to allow elf_reader to not depend on symbolize.
    visibility = [
        "//gloop/util/symbolize:__subpackages__",
    ],
)

cc_test(
    name = "elf_reader_fuzzer",
    srcs = ["elf_reader_fuzzer.cc"],
    deps = [
        ":elf_reader",
        ":symbolize",
        "@abseil-cpp//absl/strings",
        "@fuzztest//fuzztest",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
