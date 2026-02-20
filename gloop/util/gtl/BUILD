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

load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_applicable_licenses = ["@gloop//gloop:license"],
)

# TODO: Remove this placeholder once there are a non-zero number of targets in this package.
#       This test_suite rule only serves to prevent TAP errors resulting from references to this
#       package in blueprints that resolve to zero rules.
cc_test(
    name = "placeholder",
    deps = [
        "@gloop//gloop:enforce_gloop_support",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
