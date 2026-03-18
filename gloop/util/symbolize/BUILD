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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

# Explicitly force this library to be built with optimizations turned
# on and debug checking off. This is often used within unit tests and
# other code build without optimizations, but *this* library is not
# the code under test or in need of debugging. We also turn off any
# debug checking done by the STL or other libraries.
COPTS = [
    "-O3",
    "-DNDEBUG",
]

cc_library(
    name = "demangle",
    srcs = ["demangle.cc"],
    hdrs = ["demangle.h"],
    copts = COPTS,
    features = ["-use_header_modules"],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_test(
    name = "demangle_unittest",
    srcs = ["demangle_unittest.cc"],
    deps = [
        ":demangle",
        ":demangled_type_name",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@re2",
    ],
)

cc_library(
    name = "demangled_type_name",
    hdrs = ["demangled_type_name.h"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
        "@gloop//gloop:enforce_gloop_support",
    ],
)
