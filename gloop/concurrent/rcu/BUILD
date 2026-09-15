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
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["//gloop:license"],
    default_hdrs_check = "strict",
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "view",
    srcs = ["view.cc"],
    hdrs = [
        "view.h",
        "view_advanced.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":global_domain",
        ":internal",
        ":policy",
        ":rcu",
        ":snapshot",
        "//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "snapshot",
    hdrs = ["snapshot.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":snapshot_rcu_deleter",
        "//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "snapshot_rcu_deleter",
    hdrs = ["snapshot_rcu_deleter.h"],
    visibility = [
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        ":global_domain",
        ":rcu",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "global_domain",
    hdrs = ["global_domain.h"],
    visibility = [
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        ":rcu",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "internal",
    srcs = ["internal.cc"],
    hdrs = ["internal.h"],
    visibility = [
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        ":global_domain",
        ":rcu",
        ":snapshot",
        ":snapshot_rcu_deleter",
        "//gloop:enforce_gloop_support",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "policy",
    hdrs = ["policy.h"],
    visibility = [
        "//gloop/concurrent:__subpackages__",
    ],
    deps = ["//gloop:enforce_gloop_support"],
)

cc_test(
    name = "view_test",
    srcs = ["view_test.cc"],
    linkstatic = 1,
    deps = [
        ":view",
        "//gloop:gloop_test_main",
        "//gloop/base:sysinfo",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "llist",
    hdrs = ["llist.h"],
    visibility = [
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/util/atomic_danger",
    ],
)

cc_library(
    name = "pile",
    srcs = ["pile.cc"],
    hdrs = ["pile.h"],
    implementation_deps = [
        "//gloop/base:scoped_sigmask",
        "//gloop/base:signal_util_subtle",
        "//gloop/util/atomic_danger",
        "@abseil-cpp//absl/base:malloc_internal",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/debugging:leak_check",
    ],
    visibility = [
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        "//gloop:enforce_gloop_support",
        "//gloop/base:percpu",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/numeric:bits",
        "@tcmalloc//tcmalloc/internal:sysinfo",
    ],
)

cc_library(
    name = "rcu",
    srcs = [
        "rcu.cc",
        "rcu-impl.inc",
    ],
    hdrs = [
        "rcu.h",
    ],
    implementation_deps = [
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@tcmalloc//tcmalloc:malloc_extension",
        "@tcmalloc//tcmalloc/internal:sysinfo",
    ],
    visibility = [
        "//gloop/base:__pkg__",
        "//gloop/concurrent/rcu:__subpackages__",
    ],
    deps = [
        ":llist",
        ":pile",
        "//gloop:enforce_gloop_support",
        "//gloop/base:percpu",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling:scheduling_mode",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test(
    name = "rcu_test",
    size = "medium",
    srcs = ["rcu_test.cc"],
    linkstatic = 1,
    shard_count = 2,
    deps = [
        ":rcu",
        "//gloop:gloop_test_main",
        "//gloop/base:googleinit",
        "//gloop/base:signal_util_subtle",
        "//gloop/base:sysinfo",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/util/random:mt_random",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "rcu_slow_test",
    size = "medium",
    timeout = "long",
    srcs = ["rcu_test.cc"],
    linkstatic = 1,
    shard_count = 2,
    deps = [
        ":rcu",
        "//gloop:gloop_test_main",
        "//gloop/base:googleinit",
        "//gloop/base:percpu_disable_rseq",
        "//gloop/base:signal_util_subtle",
        "//gloop/base:sysinfo",
        "//gloop/thread",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/util/random:mt_random",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
