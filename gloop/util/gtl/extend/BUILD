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

# This package defines the gtl::Extend templates and several commonly used
# extensions.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "equality",
    hdrs = ["equality.h"],
    deps = [
        ":extend",
        "//gloop/util/gtl/extend/internal:is_tuple_hashable",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "equality_test",
    srcs = ["equality_test.cc"],
    deps = [
        ":equality",
        ":extend",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "extend",
    hdrs = ["extend.h"],
    deps = [
        # TODO: Remove once all extensions are migrated to new target.
        ":extension",
        "@gloop//gloop:enforce_gloop_support",
        "//gloop/util/gtl/extend/internal:extend",
        "//gloop/util/gtl/internal:dependencies",
    ],
)

cc_test(
    name = "extend_test",
    srcs = ["extend_test.cc"],
    deps = [
        ":extend",
        "//gloop/util/gtl:requires",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "extension",
    hdrs = ["extension.h"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "ordering",
    hdrs = ["ordering.h"],
    deps = [
        ":equality",
        ":extend",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "ordering_test",
    srcs = ["ordering_test.cc"],
    deps = [
        ":extend",
        ":ordering",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tuple",
    hdrs = ["tuple.h"],
    deps = [
        ":extend",
        "//gloop/util/gtl/internal:aggregate",
        "@abseil-cpp//absl/base:core_headers",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "tuple_test",
    srcs = ["tuple_test.cc"],
    deps = [
        ":extend",
        ":tuple",
        "@abseil-cpp//absl/meta:constexpr_testing",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "debug_printing",
    hdrs = ["debug_printing.h"],
    deps = [
        ":stream_printing",
        ":stringify",
        "//gloop/util/gtl/extend:extension",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "stream_printing",
    hdrs = ["stream_printing.h"],
    deps = [
        ":reflection_extension",
        "//gloop/util/gtl:generic_printer",
        "//gloop/util/gtl/extend:extension",
        "//gloop/util/gtl/extend/internal:debug_print_value",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "stringify",
    hdrs = ["stringify.h"],
    deps = [
        ":extension",
        ":reflection_extension",
        "//gloop/util/gtl:generic_printer",
        "//gloop/util/gtl/extend/internal:debug_print_value",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "debug_printing_test",
    srcs = ["debug_printing_test.cc"],
    deps = [
        ":debug_printing",
        ":stringification_tests",
        "//gloop/util/gtl:generic_printer",
        "//gloop/util/gtl/extend",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "reflection_extension",
    hdrs = ["reflection_extension.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
    ],
    deps = [
        "//gloop/util/gtl/extend",
        "//gloop/util/gtl/extend/internal:reflection",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "stream_printing_test",
    srcs = ["stream_printing_test.cc"],
    deps = [
        ":stream_printing",
        ":stringification_tests",
        "//gloop/util/gtl:generic_printer",
        "//gloop/util/gtl/extend",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "stringify_test",
    srcs = ["stringify_test.cc"],
    deps = [
        ":extend",
        ":stringification_tests",
        ":stringify",
        "//gloop/util/gtl:generic_printer",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "stringification_tests",
    testonly = True,
    hdrs = ["stringification_tests.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
    ],
    deps = [
        "//gloop/util/gtl/extend",
        "//gloop/util/gtl/extend:equality",
        "//gloop/util/gtl/extend/internal:reflection",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
        "@googletest//:gtest_for_library",
    ],
)

cc_test(
    name = "reflection_extension_test",
    srcs = ["reflection_extension_test.cc"],
    deps = [
        ":reflection_extension",
        "//gloop/util/gtl/extend",
        "//gloop/util/gtl/extend:equality",
        "//gloop/util/gtl/extend/internal:reflection",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
