load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")

# buildifier: disable=out-of-order-load
load("@gloop//gloop/tools/build_defs/cc:cc_embed_data.bzl", "cc_embed_data")

package(
    default_applicable_licenses = ["@gloop//gloop:license"],
)

cc_library(
    name = "example_lib",
    srcs = ["example.cc"],
    hdrs = ["example.h"],
    deps = [
        ":example_cc_proto",
        "@abseil-cpp//absl/status",
        "@gloop//gloop/net/base:ipaddress",
        "@gloop//gloop/util/math:mathutil",
        "@gloop//gloop/util/status",
        "@gloop//gloop/util/status:codes_cc_proto",
        "@gloop//gloop/util/status:ret_check",
        "@gloop//gloop/util/status:status_builder",
        "@gloop//gloop/util/status:status_cc_proto",
        "@gloop//gloop/util/status:status_macros",
    ],
)

cc_test(
    name = "example_test",
    srcs = ["example_test.cc"],
    deps = [
        ":example_lib",
        "@gloop//gloop/base:config",
        "@googletest//:gtest_main",
    ],
)

proto_library(
    name = "example_proto",
    srcs = ["example.proto"],
    deps = [
        "@gloop//gloop/util/status:codes_proto",
        "@gloop//gloop/util/status:status_proto",
    ],
)

cc_proto_library(
    name = "example_cc_proto",
    deps = [":example_proto"],
)

cc_embed_data(
    name = "embedded_test_data",
    srcs = ["test_data.txt"],
    outs = [
        "embedded_test_data.cc",
        "embedded_test_data.h",
    ],
    flatten = True,
)

cc_test(
    name = "cc_embed_data_test",
    srcs = ["cc_embed_data_test.cc"],
    deps = [
        ":embedded_test_data",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest_main",
    ],
)
