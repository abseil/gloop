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
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "associative_view_internal",
    hdrs = ["associative_view_internal.h"],
    deps = [
        ":requires",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:throw_delegate",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "associative_view_internal_test",
    srcs = ["associative_view_internal_test.cc"],
    deps = [
        ":associative_view_internal",
        ":set_view",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "comparator",
    hdrs = ["comparator.h"],
    deps = [
        ":compressed_tuple",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "comparator_test",
    size = "small",
    srcs = ["comparator_test.cc"],
    deps = [
        ":comparator",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "compressed_tuple",
    hdrs = ["compressed_tuple.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/container:compressed_tuple",
    ],
)

cc_test(
    name = "compressed_tuple_test",
    size = "small",
    srcs = ["compressed_tuple_test.cc"],
    deps = [
        ":compressed_tuple",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "container_logging",
    hdrs = ["container_logging.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log/internal:container",
    ],
)

cc_test(
    name = "container_logging_test",
    srcs = ["container_logging_test.cc"],
    deps = [
        ":container_logging",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "env",
    srcs = [
        "env.cc",
    ],
    hdrs = ["env.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "env_test",
    srcs = ["env_test.cc"],
    deps = [
        ":env",
        "//gloop:gloop_test_main",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "flat_internal",
    hdrs = ["flat_internal.h"],
    visibility = [
        "//gloop/util/gtl:__pkg__",
    ],
    deps = [
        ":unchecked_tags",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "flat_internal_test",
    srcs = ["flat_internal_test.cc"],
    deps = [
        ":flat_internal",
        ":requires",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/strings:cord",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "flat_map",
    hdrs = ["flat_map.h"],
    deps = [
        ":flat_internal",
        ":heterogeneous_lookup",
        ":iterator_adaptors",
        ":unchecked_tags",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:throw_delegate",
    ],
)

cc_test(
    name = "flat_map_test",
    srcs = ["flat_map_test.cc"],
    deps = [
        ":flat_map",
        ":stl_util",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/container:test_instance_tracker",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:source_location",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "flat_set",
    hdrs = ["flat_set.h"],
    deps = [
        ":flat_internal",
        ":heterogeneous_lookup",
        ":unchecked_tags",
        "//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "flat_set_test",
    srcs = ["flat_set_test.cc"],
    deps = [
        ":comparator",
        ":flat_set",
        ":stl_util",
        ":switch",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "heapqueue",
    hdrs = ["heapqueue.h"],
    deps = [
        ":priority_queue_util",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "heapqueue_unittest",
    srcs = ["heapqueue_unittest.cc"],
    deps = [
        ":comparator",
        ":heapqueue",
        ":stl_util",
        "//gloop:gloop_test_main",
        "//gloop/util/random:mt_random",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "heterogeneous_lookup",
    hdrs = ["heterogeneous_lookup.h"],
    visibility = [
        "//gloop/util/gtl:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/container:common",
    ],
)

cc_library(
    name = "interval",
    hdrs = ["interval.h"],
    deps = [
        ":heterogeneous_lookup",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "interval_test",
    size = "small",
    srcs = ["interval_test.cc"],
    deps = [
        ":interval",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "interval_set",
    hdrs = ["interval_set.h"],
    deps = [
        ":container_logging",
        ":heterogeneous_lookup",
        ":interval",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "interval_set_unittest",
    size = "medium",
    srcs = ["interval_set_unittest.cc"],
    deps = [
        ":interval",
        ":interval_set",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:container_memory",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "intrusive_list",
    hdrs = ["intrusive_list.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:prefetch",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "intrusive_list_test",
    size = "small",
    srcs = ["intrusive_list_test.cc"],
    deps = [
        ":intrusive_list",
        ":iterator_adaptors",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "iterator_adaptors",
    hdrs = ["iterator_adaptors.h"],
    deps = [
        ":compressed_tuple",
        ":requires",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:throw_delegate",
        "@abseil-cpp//absl/meta:type_traits",
    ],
)

cc_test(
    name = "iterator_adaptors_unittest",
    srcs = ["iterator_adaptors_unittest.cc"],
    deps = [
        ":flat_map",
        ":flat_set",
        ":iterator_adaptors",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:duration_cc_proto",
    ],
)

cc_library(
    name = "map_view",
    hdrs = ["map_view.h"],
    deps = [
        ":associative_view_internal",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "map_view_test",
    size = "small",
    srcs = ["map_view_test.cc"],
    shard_count = 10,
    deps = [
        ":flat_map",
        ":map_view",
        ":set_view",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:exception_testing",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "priority_queue_util",
    hdrs = ["priority_queue_util.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "priority_queue_util_test",
    size = "small",
    srcs = ["priority_queue_util_test.cc"],
    linkstatic = 1,
    deps = [
        ":comparator",
        ":priority_queue_util",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "requires",
    hdrs = ["requires.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/meta:requires",
    ],
)

cc_library(
    name = "set_view",
    hdrs = ["set_view.h"],
    deps = [
        ":associative_view_internal",
        ":iterator_adaptors",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "set_view_test",
    size = "small",
    srcs = ["set_view_test.cc"],
    deps = [
        ":flat_set",
        ":iterator_adaptors",
        ":map_view",
        ":set_view",
        "//gloop:gloop_test_main",
        "//gloop/strings:case",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "stl_util",
    hdrs = ["stl_util.h"],
    deps = [
        ":requires",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
    ],
)

cc_test(
    name = "stl_util_unittest",
    srcs = ["stl_util_unittest.cc"],
    deps = [
        ":stl_util",
        ":unique_array",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:chunked_queue",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "switch",
    hdrs = ["switch.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/utility",
    ],
)

cc_test(
    name = "switch_test",
    size = "small",
    srcs = ["switch_test.cc"],
    deps = [
        ":switch",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "top_n",
    hdrs = ["top_n.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "top_n_unittest",
    srcs = ["top_n_unittest.cc"],
    visibility = ["//visibility:private"],
    deps = [
        ":stl_util",
        ":top_n",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "typeid",
    hdrs = ["typeid.h"],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "typeid_unittest",
    srcs = [
        "typeid_unittest.cc",
        "typeid_unittest2.cc",
    ],
    deps = [
        ":typeid",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "unaligned",
    hdrs = ["unaligned.h"],
    deps = [
        ":unaligned_internal",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/meta:type_traits",
    ],
)

cc_library(
    name = "unaligned_internal",
    hdrs = ["unaligned_internal.h"],
    visibility = [
        "//gloop/util/gtl:__subpackages__",
    ],
    deps = [
        ":requires",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
    ],
)

cc_test(
    name = "unaligned_test",
    srcs = ["unaligned_test.cc"],
    deps = [
        ":requires",
        ":unaligned",
        ":unaligned_internal",
        "//gloop:gloop_test_main",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "unchecked_tags",
    hdrs = ["unchecked_tags.h"],
    visibility = [
        "//gloop/util/gtl:__pkg__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_library(
    name = "unique_array",
    srcs = ["unique_array.cc"],
    hdrs = ["unique_array.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_test(
    name = "unique_array_test",
    srcs = ["unique_array_test.cc"],
    malloc = "//gloop:malloc",
    deps = [
        ":unique_array",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "value_or_die",
    srcs = ["value_or_die.cc"],
    hdrs = ["value_or_die.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:source_location",
    ],
)

cc_test(
    name = "value_or_die_test",
    srcs = ["value_or_die_test.cc"],
    deps = [
        ":value_or_die",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "iterator_range",
    hdrs = ["iterator_range.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:iterator_traits_internal",
    ],
)

cc_test(
    name = "iterator_range_test",
    srcs = ["iterator_range_test.cc"],
    deps = [
        ":iterator_range",
        "//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "string_hash_map",
    hdrs = ["string_hash_map.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/container:container_memory",
        "@abseil-cpp//absl/container:hash_function_defaults",
        "@abseil-cpp//absl/container:layout",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/container:node_slot_policy",
        "@abseil-cpp//absl/container:raw_hash_map",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "string_hash_map_test",
    srcs = ["string_hash_map_test.cc"],
    copts = ["-DUNORDERED_MAP_CXX17"],
    deps = [
        ":string_hash_map",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:hash_policy_testing",
        "@abseil-cpp//absl/container:unordered_map_constructor_test",
        "@abseil-cpp//absl/container:unordered_map_lookup_test",
        "@abseil-cpp//absl/container:unordered_map_modifiers_test",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:any",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "generic_printer",
    hdrs = ["generic_printer.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings:generic_printer",
    ],
)

cc_test(
    name = "generic_printer_test",
    srcs = ["generic_printer_test.cc"],
    deps = [
        ":generic_printer",
        ":generic_printer_test_cc_proto",
        "//gloop:gloop_test_main",
        "//gloop/base:arena",
        "//gloop/base:arena_allocator",
        "//gloop/base:logging_extensions",
        "//gloop/util/gtl/extend",
        "//gloop/util/gtl/extend:debug_printing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:generic_printer",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:optional_ref",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf//:protobuf_lite",
    ],
)

cc_proto_library(
    name = "generic_printer_test_cc_proto",
    deps = [":generic_printer_test_proto"],
)

proto_library(
    name = "generic_printer_test_proto",
    srcs = [
        "generic_printer_test.proto",
    ],
    option_deps = ["@protobuf//:cpp_features_proto"],
)

cc_library(
    name = "intervalmap",
    hdrs = ["intervalmap.h"],
    deps = [
        ":heterogeneous_lookup",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:compressed_tuple",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "intervalmap_test",
    size = "small",
    srcs = ["intervalmap_test.cc"],
    deps = [
        ":intervalmap",
        "//gloop:gloop_test_main",
        "//gloop/base:arena",
        "//gloop/base:arena_allocator",
        "//gloop/util/random:acmrandom",
        "//gloop/util/random:distributions",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "intrusive_heap",
    srcs = ["intrusive_heap.cc"],
    hdrs = ["intrusive_heap.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "intrusive_heap_test",
    size = "small",
    srcs = ["intrusive_heap_test.cc"],
    deps = [
        ":intrusive_heap",
        "//gloop:gloop_test_main",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/random",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "lazy_static_ptr",
    hdrs = ["lazy_static_ptr.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/utility",
    ],
)

cc_test(
    name = "lazy_static_ptr_test",
    size = "small",
    srcs = [
        "lazy_static_ptr_test.h",
        "lazy_static_ptr_test1.cc",
        "lazy_static_ptr_test2.cc",
        "lazy_static_ptr_test3.cc",
    ],
    deps = [
        ":lazy_static_ptr",
        "//gloop:gloop_test_main",
        "//gloop/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "lockfree_hashmap",
    hdrs = ["lockfree_hashmap.h"],
    deps = [
        ":lockfree_hashtable_internal",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "lockfree_hashtable_internal",
    hdrs = ["lockfree_hashtable_internal.h"],
    visibility = [
        "//gloop/util/gtl:__pkg__",
    ],
    deps = [
        ":compressed_tuple",
        ":hashtable_common",
        "//gloop:enforce_gloop_support",
        "//gloop/util/bits",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "lockfree_hashmap_test",
    srcs = ["lockfree_hashmap_test.cc"],
    deps = [
        ":lockfree_hashmap",
        "//gloop:gloop_test_main",
        "//gloop/base:atomic_stats_counter",
        "//gloop/base:sysinfo",
        "//gloop/thread:thread_manager",
        "//gloop/util/gtl/extend",
        "//gloop/util/gtl/extend:equality",
        "//gloop/util/hash:transparent_hash",
        "//gloop/util/random:acmrandom",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "hashtable_common",
    hdrs = ["hashtable_common.h"],
    visibility = [
        "//gloop/util/gtl:__pkg__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "string_hash_set",
    hdrs = ["string_hash_set.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/container:container_memory",
        "@abseil-cpp//absl/container:hash_function_defaults",
        "@abseil-cpp//absl/container:node_slot_policy",
        "@abseil-cpp//absl/container:raw_hash_set",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "string_hash_set_test",
    srcs = ["string_hash_set_test.cc"],
    copts = ["-DUNORDERED_SET_CXX17"],
    deps = [
        ":string_hash_set",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/container:hash_policy_testing",
        "@abseil-cpp//absl/container:unordered_set_constructor_test",
        "@abseil-cpp//absl/container:unordered_set_lookup_test",
        "@abseil-cpp//absl/container:unordered_set_modifiers_test",
        "@abseil-cpp//absl/strings:string_view",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "subspan",
    hdrs = ["subspan.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/types:any_span",
    ],
)

cc_test(
    name = "subspan_test",
    srcs = ["subspan_test.cc"],
    deps = [
        ":subspan",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/types:any_span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "float128",
    srcs = ["float128.cc"],
    hdrs = ["float128.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest_prod",
    ],
)

cc_test(
    name = "float128_test",
    srcs = ["float128_test.cc"],
    shard_count = 50,
    deps = [
        ":float128",
        ":float128_matchers",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/strings:str_format",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "float128_matchers",
    testonly = 1,
    hdrs = ["float128_matchers.h"],
    deps = [
        ":float128",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_for_library",
    ],
)

cc_library(
    name = "circularbuffer",
    hdrs = ["circularbuffer.h"],
    deps = [
        ":container_logging",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "circularbuffer_unittest",
    srcs = ["circularbuffer_unittest.cc"],
    deps = [
        ":circularbuffer",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "message_hasher",
    srcs = ["message_hasher.cc"],
    hdrs = ["message_hasher.h"],
    deps = [
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:linked_hash_map",
        "@abseil-cpp//absl/container:linked_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:resize_and_overwrite",
        "@abseil-cpp//absl/strings:string_view",
        "@protobuf//:protobuf_lite",
        "@protobuf//src/google/protobuf/io",
    ],
)

cc_test(
    name = "message_hasher_test",
    srcs = ["message_hasher_test.cc"],
    deps = [
        ":message_hasher",
        ":message_hasher_unittest_cc_proto",
        ":stl_util",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@protobuf",
        "@protobuf//:differencer",
        "@protobuf//src/google/protobuf/io",
    ],
)

proto_library(
    name = "message_hasher_unittest_proto",
    srcs = ["message_hasher_unittest.proto"],
    option_deps = ["@protobuf//:cpp_features_proto"],
)

cc_proto_library(
    name = "message_hasher_unittest_cc_proto",
    deps = [":message_hasher_unittest_proto"],
)
