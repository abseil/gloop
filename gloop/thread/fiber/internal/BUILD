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
    default_visibility = ["//visibility:private"],
    features = ["header_modules"],
)

cc_library(
    name = "fiber-domain-support",
    srcs = ["fiber-domain-support.cc"],
    hdrs = ["fiber-domain-support.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
        "@gloop//gloop/thread/fiber:__pkg__",
        "@gloop//gloop/thread/fiber/internal:__subpackages__",
    ],
    deps = [
        ":fiber_thread_options",
        ":fiber_thread_pool",
        "//gloop/base:static_threadlocal",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:domain_thread_assignment_callback_accessor",
        "//gloop/base/scheduling:low-level-support",
        "//gloop/thread",
        "//gloop/thread:wait_state",
        "//gloop/thread/fiber:fiber-internal",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization:kernel_timeout_internal",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_library(
    name = "fiber_thread_options",
    hdrs = ["fiber-thread-options.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
    ],
    deps = ["@gloop//gloop:enforce_gloop_support"],
)

cc_library(
    name = "fiber_thread_pool",
    srcs = ["fiber-thread-pool.cc"],
    hdrs = ["fiber-thread-pool.h"],
    visibility = [
        "//gloop/thread/fiber:__subpackages__",
        "//gloop/thread/fiber/internal:__subpackages__",
        "@gloop//gloop/thread/fiber:__pkg__",
        "@gloop//gloop/thread/fiber/internal:__subpackages__",
    ],
    deps = [
        ":fiber_thread_options",
        "//gloop/base:spinlock",
        "//gloop/base/scheduling",
        "//gloop/base/scheduling:scheduling_mode",
        "//gloop/thread",
        "//gloop/thread:wait_state",
        "//gloop/thread/fiber:channel",
        "//gloop/thread/fiber:scheduler_types",
        "//gloop/util/gtl:intrusive_list",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:raw_logging_internal",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:enforce_gloop_support",
    ],
)

cc_test(
    name = "fiber-thread-pool_test",
    size = "enormous",  # any mobile variant of this test will run for a very long time O(10 minutes) due to the slowness of emulation.
    srcs = ["fiber-thread-pool_test.cc"],
    args = [
        # we explicility set these flags as they are much larger by default on mobile, which causes tests to timeout.
        "--switchto_domain_idle_thread_death_interval_ms=10",
        "--switchto_domain_idle_thread_timeout_ms=1",
        "--fiber_thread_reaper_cooldown_ms=5",
    ],
    visibility = ["//visibility:private"],
    deps = [
        ":fiber_thread_pool",
        "//gloop/base/scheduling",
        "//gloop/thread/fiber",
        "//gloop/thread/fiber:fiber-options",
        "//gloop/thread/fiber:fifolifo-schedulers",
        "//gloop/thread/fiber:futex-domain",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gloop//gloop:gloop_test_main",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest",
    ],
)
