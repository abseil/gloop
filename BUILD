# Description:
#   Utility functions for sharing code originally written in google3.

load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

license(
    name = "license",
    package_name = "gloop",
)

licenses(["notice"])

exports_files(["LICENSE"])

cc_test(
    name = "basic_test",
    srcs = ["basic_test.cc"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
