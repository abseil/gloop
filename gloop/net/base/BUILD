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
# that are not low level. For more information, including how to submit
# changes to this file, see <link>.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "ipaddress",
    srcs = ["ipaddress.cc"],
    hdrs = ["ipaddress.h"],
    features = ["header_modules"],
    linkopts = select({
        "//conditions:default": [],
    }),
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:port",
        "//gloop/strings:host_port",
        "//gloop/strings:numbers",
        "//gloop/strings:util",
        "//gloop/util/bits",
        "//gloop/util/endian",
        "//gloop/util/gtl:comparator",
        "//gloop/util/hash",
        "//gloop/util/status:errno_mapping",
        "//gloop/util/status:status_macros",
        "//gloop/util/tuple/components:dump_vars",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:compare",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "ipaddress_test",
    size = "small",
    timeout = "moderate",
    srcs = ["ipaddress_test.cc"],
    shard_count = 4,
    visibility = ["//visibility:private"],
    deps = [
        ":ipaddress",
        "//gloop:gloop_test_main",
        "//gloop/strings:host_port",
        "//gloop/util/endian",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/numeric:int128",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:compare",
        "@abseil-cpp//absl/types:source_location",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
