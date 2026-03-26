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

load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Internal extend utilities and implementation details.
package(
    default_applicable_licenses = ["//gloop:license"],
    features = ["header_modules"],
)

cc_library(
    name = "extend",
    hdrs = ["extend.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
    ],
    deps = [
        "//gloop/util/gtl/extend:extension",
        "//gloop/util/gtl/internal:aggregate",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "is_tuple_hashable",
    hdrs = ["is_tuple_hashable.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/hash",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "debug_print_value",
    hdrs = ["debug_print_value.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
    ],
    deps = [
        ":reflection",
        "//gloop/util/gtl/extend:reflection_extension",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "reflection",
    srcs = ["reflection.cc"],
    hdrs = ["reflection.h"],
    visibility = [
        "//gloop/util/gtl/extend:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@gloop//gloop:enforce_gloop_support",
    ],
)
