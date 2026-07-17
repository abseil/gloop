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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
)

platform(
    name = "rbe",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = {
        "container-image": "docker://gcr.io/google.com/absl-177019/gloop-linux-latest@sha256:f2a037597a30bf894567ee69ed8190aa93b203c037cb524d6db4fd7327e5784b",
        "OSArch": "amd64",
        "dockerRunAsRoot": "true",
        "cache-silo-key": "n2-highmem-8-2026-05-02",
    },
)

platform(
    name = "rbe-arm",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    exec_properties = {
        "container-image": "docker://gcr.io/google.com/absl-177019/gloop-linux-latest@sha256:f2a037597a30bf894567ee69ed8190aa93b203c037cb524d6db4fd7327e5784b",
        "OSArch": "arm64",
        "dockerRunAsRoot": "true",
        "cache-silo-key": "t2a-standard-8-2026-05-02",
    },
)

# Verification of our CI toolchains.

cc_binary(
    name = "toolchain_test",
    srcs = ["toolchain_test.cc"],
    deps = ["//gloop:enforce_gloop_support"],
)

# Architecture verification tests
sh_test(
    name = "x86_64_architecture_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "arch",
        "X86-64",
    ],
    data = [":toolchain_test"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
)

sh_test(
    name = "aarch64_architecture_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "arch",
        "AArch64",
    ],
    data = [":toolchain_test"],
    target_compatible_with = ["@platforms//cpu:aarch64"],
)

# Standard library dynamic linkage verification tests
sh_test(
    name = "libstdcxx_verification_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "needed",
        "libstdc++.so",
    ],
    data = [":toolchain_test"],
    target_compatible_with = select({
        "//gloop:libstdcxx_enabled": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

sh_test(
    name = "libcxx_verification_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "needed",
        "libc++.so",
    ],
    data = [":toolchain_test"],
    target_compatible_with = select({
        "//gloop:libstdcxx_enabled": ["@platforms//:incompatible"],
        # These sanitizers link libc++ statically.
        "//gloop:tsan_enabled": ["@platforms//:incompatible"],
        "//gloop:msan_enabled": ["@platforms//:incompatible"],
        "//conditions:default": [
        ],
    }),
)

# Sanitizer runtime instrumentation verification tests
sh_test(
    name = "asan_verification_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "symbol",
        "__asan",
    ],
    data = [":toolchain_test"],
    target_compatible_with = select({
        "//gloop:asan_enabled": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

sh_test(
    name = "ubsan_verification_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "symbol",
        "__ubsan",
    ],
    data = [":toolchain_test"],
    target_compatible_with = select({
        "//gloop:ubsan_enabled": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

sh_test(
    name = "msan_verification_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "symbol",
        "__msan",
    ],
    data = [":toolchain_test"],
    target_compatible_with = select({
        "//gloop:msan_enabled": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

sh_test(
    name = "tsan_verification_test",
    srcs = ["verify_binary.sh"],
    args = [
        "$(location :toolchain_test)",
        "symbol",
        "__tsan",
    ],
    data = [":toolchain_test"],
    target_compatible_with = select({
        "//gloop:tsan_enabled": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)
