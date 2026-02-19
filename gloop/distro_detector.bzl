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

"""A repository rule that detects the host distribution and defines a macro to check it."""

def _distro_detector_impl(repository_ctx):
    is_debian = False
    os_release_path = repository_ctx.path("/etc/os-release")

    if os_release_path.exists:
        content = repository_ctx.read(os_release_path)
        lines = content.split("\n")
        for line in lines:
            line = line.strip()
            if line == "ID=debian":
                is_debian = True
                break

    # Generate the build files regardless of the OS
    repository_ctx.file("BUILD", """
load("@rules_cc//cc:cc_library.bzl", "cc_library")
cc_library(
    name = "gloop_distro",
    hdrs = ["gloop_distro.h"],
    visibility = ["@gloop//gloop:__subpackages__"],
)
""")

    repository_ctx.file("gloop_distro.h", """
#ifndef GLOOP_ON_DEBIAN_H_
#define GLOOP_ON_DEBIAN_H_
{}
#endif  // GLOOP_ON_DEBIAN_H_
""".format("#define GLOOP_ON_DEBIAN 1" if is_debian else ""))

distro_detector = repository_rule(
    implementation = _distro_detector_impl,
    local = True,
)

def _distro_detector_extension_impl(module_ctx):
    distro_detector(name = "host_distro")
    return module_ctx.extension_metadata(reproducible = True)

host_distro_detector_extension = module_extension(
    implementation = _distro_detector_extension_impl,
)
