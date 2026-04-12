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
# that are not low level.   For more information, including how to submit
# changes to this file, see    <link>howto/build-monitors.html

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
    features = ["header_modules"],
)

cc_library(
    name = "endian",
    hdrs = [
        "endian.h",
        "endian-fp.h",
    ],
    deps = [
        "//gloop/base:port",
        "//gloop/base:uword",
        "//gloop/util/gtl:unaligned",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/numeric:int128",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "endian_unittest",
    srcs = ["endian_unittest.cc"],
    deps = [
        ":endian",
        "//gloop/base:port",
        "//gloop/base:uword",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@gloop//gloop:gloop_test_main",
    ],
)

cc_test(
    name = "endian-fp_unittest",
    srcs = ["endian-fp_unittest.cc"],
    deps = [
        ":endian",
        "@abseil-cpp//absl/log:check",
        "@gloop//gloop:gloop_test_main",
    ],
)
