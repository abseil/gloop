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

load("@rules_cc//cc:defs.bzl", "cc_toolchain", "cc_toolchain_suite")
load(":cc_toolchain_config.bzl", "rbe_toolchain_config")

package(default_visibility = ["//visibility:public"])

cc_toolchain_suite(
    name = "rbe_suite",
    toolchains = {
        "x86_64": ":cc-compiler-rbe",
    },
)

rbe_toolchain_config(name = "rbe_config")

cc_toolchain(
    name = "cc-compiler-rbe",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":rbe_config",
    toolchain_identifier = "rbe_clang",
)

toolchain(
    name = "cc-toolchain-rbe",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    toolchain = ":cc-compiler-rbe",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

filegroup(name = "empty")
