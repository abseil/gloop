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
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//gloop/tools/build_defs/cc:cc_embed_data.bzl", "cc_embed_data")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_visibility = ["//visibility:private"],
)

# An implicit dependency of all "cc_embed_data" rules.
cc_binary(
    name = "filewrapper_impl",
    srcs = ["filewrapper.cc"],
    data = [
        "//gloop/base:file_toc.h",
    ],
    malloc = "//gloop/base:system_malloc",  # avoid extra dependencies
    stamp = 0,
    visibility = [
        "//gloop/tools:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "@boringssl//:crypto",
        "@rules_cc//cc/runfiles",
    ],
)

cc_binary(
    name = "filewrapper_testdata_generator_bin",
    testonly = 1,
    srcs = ["filewrapper_testdata_generator.cc"],
    deps = ["//gloop:enforce_gloop_support"],
)

genrule(
    name = "filewrapper_testdata_generator",
    testonly = 1,
    outs = ["filewrapper_testdata.txt"],
    cmd = "$(location :filewrapper_testdata_generator_bin) > $@",
    tools = [":filewrapper_testdata_generator_bin"],
)

cc_embed_data(
    name = "filewrapper_testdata",
    testonly = 1,
    srcs = [":filewrapper_testdata_generator"],
    outs = [
        "filewrapper_testdata.cc",
        "filewrapper_testdata.h",
        "filewrapper_testdata_data.o",
    ],
)

cc_test(
    name = "filewrapper_embed_test",
    srcs = ["filewrapper_embed_test.cc"],
    data = [
        ":filewrapper_testdata",  # To check that the sources are UTF-8
        ":filewrapper_testdata_generator",
    ],
    linkstatic = True,
    deps = [
        ":filewrapper_testdata",
        "//gloop:gloop_test_main",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
