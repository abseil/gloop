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

load("@rules_cc//cc:action_names.bzl", "ACTION_NAME_GROUPS")
load(
    "@rules_cc//cc:cc_toolchain_config_lib.bzl",
    "feature",
    "flag_group",
    "flag_set",
    "tool_path",
)
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/toolchains:cc_toolchain_config_info.bzl", "CcToolchainConfigInfo")

all_compile_actions = ACTION_NAME_GROUPS.all_cpp_compile_actions
all_link_actions = ACTION_NAME_GROUPS.all_cc_link_actions

def _sanitizer_feature(name = "", sanitizer = "", specific_compile_flags = [], specific_link_flags = []):
    return feature(
        name = name,
        flag_sets = [
            flag_set(
                actions = all_compile_actions,
                flag_groups = [
                    flag_group(flags = [
                        "-fno-omit-frame-pointer",
                        "-fno-sanitize-recover=all",
                        "-fsanitize=" + sanitizer,
                    ] + specific_compile_flags),
                ],
            ),
            flag_set(
                actions = all_link_actions,
                flag_groups = [
                    flag_group(
                        flags = specific_link_flags + ["-fsanitize=" + sanitizer],
                    ),
                ],
            ),
        ],
    )

def _rbe_toolchain_config_impl(ctx):
    features = [
        _sanitizer_feature(name = "asan", sanitizer = "address"),
        _sanitizer_feature(name = "ubsan", sanitizer = "undefined"),
        _sanitizer_feature(name = "msan", sanitizer = "memory"),
        _sanitizer_feature(name = "tsan", sanitizer = "thread"),
        feature(name = "opt"),
        feature(name = "dbg"),
        feature(name = "supports_pic", enabled = True),
        feature(name = "supports_dynamic_linker", enabled = True),
        feature(
            name = "default_flags",
            enabled = True,
            flag_sets = [
                flag_set(
                    actions = all_link_actions,
                    flag_groups = ([
                        flag_group(
                            flags = [
                                "-fuse-ld=lld",
                                "-L/opt/llvm/lib/x86_64-unknown-linux-gnu",
                                "-lc++",
                                "-lc++abi",
                                "-lm",
                                "-Wl,-rpath=/opt/llvm/lib/x86_64-unknown-linux-gnu",
                            ],
                        ),
                    ]),
                ),
                flag_set(
                    actions = all_compile_actions,
                    flag_groups = ([
                        flag_group(
                            flags = [
                                "-std=c++20",
                                "-nostdinc++",
                                "-isystem/usr/local/include/llvm/c++/v1",
                                "-isystem/usr/local/include/llvm/x86_64-unknown-linux-gnu/c++/v1",
                                "-isystem/opt/llvm/lib/clang/21/include",
                                "-isystem/usr/include",
                                "-isystem/usr/include/x86_64-linux-gnu",
                            ],
                        ),
                    ]),
                ),
            ],
        ),
    ]
    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        compiler = "clang",
        features = features,
        toolchain_identifier = "rbe_x86_64_clang",
        tool_paths = [
            tool_path(name = "gcc", path = "/opt/llvm/bin/clang"),
            tool_path(name = "ld", path = "/opt/llvm/bin/ld.lld"),
            tool_path(name = "ar", path = "/opt/llvm/bin/llvm-ar"),
            tool_path(name = "cpp", path = "/opt/llvm/bin/clang-cpp"),
            tool_path(name = "gcov", path = "/opt/llvm/bin/llvm-cov"),
            tool_path(name = "nm", path = "/opt/llvm/bin/llvm-nm"),
            tool_path(name = "objdump", path = "/opt/llvm/bin/llvm-objdump"),
            tool_path(name = "strip", path = "/opt/llvm/bin/llvm-strip"),
        ],
        cxx_builtin_include_directories = [
            "%workspace%/gloop/rbe_toolchain/sysroot/include",
            "%workspace%/gloop/rbe_toolchain/sysroot/include/x86_64-unknown-linux-gnu",
            "%workspace%/gloop/rbe_toolchain/llvm/include/c++/v1",
            "%workspace%/gloop/rbe_toolchain/llvm/include/x86_64-unknown-linux-gnu/c++/v1",
            "%workspace%/gloop/rbe_toolchain/clang/21/include",
            "/usr/local/include/llvm/c++/v1",
            "/usr/local/include/llvm/x86_64-unknown-linux-gnu/c++/v1",
            "/opt/llvm/lib/clang/21/include",
            "/opt/llvm/lib/clang/21/share",
            "/usr/include",
            "/usr/include/x86_64-unknown-linux-gnu",
        ],
    )

rbe_toolchain_config = rule(
    implementation = _rbe_toolchain_config_impl,
    attrs = {},
    provides = [CcToolchainConfigInfo],
)
