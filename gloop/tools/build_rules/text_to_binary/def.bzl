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

"""
BUILD rules for converting text files into various binary formats.
"""

load("@bazel_skylib//rules:build_test.bzl", "build_test")
load("@protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

# BUILD rule to convert a protocol buffer in text (a.k.a. ASCII) format into the
# standard binary, jspb, or json formats.
#
# Sample usage:
#   proto_data(
#       name = "my_config",
#       src = "my_config.textproto",
#       proto_name = "foo_pkg.MyConfig",
#       proto_deps = ["//some/package:foo_proto"],
#   )
#
# If proto_deps contains a BUILD label pointing to a proto_library then its
# transitive dependencies are automatically included.
#
# "json" format is compliant with <link>.
#
# Binary output is deterministic (<link>).
def proto_data(
        name,
        src,
        proto_name,
        proto_deps = None,
        out = None,
        out_format = "binarypb",
        output_to_bindir = None,
        descriptor_set = None,
        include_test = False,
        test_tags = [],
        **kwargs):
    """Converts a protocol buffer in text format into the standard binary format.

    Args:
      name: The name of the build target.
      src: A text formatted protocol buffer (<link>).
      proto_name: The name of the message type in the .proto files that "src" file
                  represents.
      proto_deps: The list of proto_library targets where "proto" is defined.
                  Transitive dependencies are pulled in automatically.
      out: (optional) The name of output file. If out is not set then name of
           output file is name + ".binarypb" extension (or ".jspb", ".json").
      out_format: (optional) The format of the output file.
           Can be "binarypb", "jspb", or "json", default is binarypb.
      output_to_bindir: output_to_bindir attribute for genrule.
      descriptor_set: A transitive_descriptor_set target. If this is
          provided, don't set proto_deps.
      include_test: (optional) If true a build_test will be created for the proto_data rule. The
          default is false.
      test_tags: Tag supplied by user for the build_test.
      **kwargs: Other args to be passed on to anticodex_tool.
    """
    anticodex_tool(
        name = name,
        src = src,
        out = out or (name + "." + out_format),
        # 'binarypb' translates to 'protobuf' for anticodex. Unrecognized strings also default to
        # 'protobuf' (some proto_data invocations rely on this behavior, e.g. setting
        # out_format == 'pb').
        output = (out_format if out_format in _DIRECTLY_SUPPORTED_OUTPUT_FORMAT_ALIASES else "protobuf"),
        proto = proto_name,
        proto_deps = proto_deps,
        output_to_bindir = output_to_bindir,
        descriptor_set = descriptor_set,
        **kwargs
    )

    if include_test:
        build_test(
            name = "%s_build_test" % name,
            targets = [":%s" % name],
            tags = test_tags,
        )

# BUILD rule for anticodex command line tool.
# <link>
def anticodex_tool(
        name,
        src,
        out,
        output,
        proto = "",
        proto_deps = None,
        output_to_bindir = None,
        descriptor_set = None,
        **kwargs):
    """Converts text file into binary file.

    Args:
      name: The name of the build target.
      src: A text formatted protocol buffer (<link>).
      out: The name of output file.
      output: The format of output file.
      proto: The name of the protocol message to output value.
      proto_deps: proto targets to use to support the protobuf output format.
      output_to_bindir: output_to_bindir attribute for genrule.
      descriptor_set: A transitive_descriptor_set target. If this is
          provided, don't set proto_deps.
      **kwargs: Other args to be passed on to genrules.
    """

    if proto_deps == None:
        proto_deps = []

    if output == "protobuf":
        # Simple conversion, use protocol_compiler
        if output_to_bindir:
            rule = _run_protoc_bin
        else:
            rule = _run_protoc_genfiles
        rule(
            name = name,
            src = src,
            outs = [out],
            deps = proto_deps,
            descriptor_set = descriptor_set,
            proto_name = proto,
            **kwargs
        )
    else:
        fail("Unsupported output format: %s" % output)

def _descriptor_sets_depset(deps, descriptor_set):
    """Makes a depset of distinct FileDescriptorSet files."""

    transitive = []
    for dep in deps:
        if ProtoInfo in dep:
            transitive.append(dep[ProtoInfo].transitive_descriptor_sets)
    direct = []
    if descriptor_set != None:
        direct.append(descriptor_set)
    return depset(direct = direct, transitive = transitive)

def _run_protoc_impl(ctx):
    """Rule to translate text to binary using protocol_compiler."""

    proto_descriptor_sets = _descriptor_sets_depset(ctx.attr.deps, ctx.file.descriptor_set)

    if len(ctx.outputs.outs) != 1:
        fail("Expected exactly one output")
    out = ctx.outputs.outs[0]

    args = ctx.actions.args()
    args.set_param_file_format("multiline")
    args.add("--encode=%s" % ctx.attr.proto_name)
    args.add("--deterministic_output")
    args.add_joined(
        proto_descriptor_sets,
        join_with = ":",
        format_joined = "--descriptor_set_in=%s",
    )

    # We always use a flagfile to avoid flattening the depset to check command line length.
    flagfile = ctx.actions.declare_file(ctx.attr.name + ".flagfile")
    ctx.actions.write(
        output = flagfile,
        content = args,
    )

    redirect = [
        "< %s" % ctx.file.src.path,
        "> %s" % out.path,
    ]

    ctx.actions.run_shell(
        outputs = ctx.outputs.outs,
        inputs = depset([ctx.file.src, flagfile], transitive = [proto_descriptor_sets]),
        tools = [ctx.executable._tool],
        command = " ".join([ctx.executable._tool.path, "@%s" % flagfile.path] + redirect),
        mnemonic = "ProtoDataCompilerFlagfile",
        use_default_shell_env = False,
    )
    return DefaultInfo(runfiles = ctx.runfiles(files = ctx.outputs.outs))

# Rule to translate single text file to single binary file using
# protocol_compiler. The single output is in a list 'outs' to match the
# expectations of BBCP.
def _make_protoc_rule(output_to_genfiles):
    return rule(
        output_to_genfiles = output_to_genfiles,
        attrs = {
            "deps": attr.label_list(allow_files = True, default = []),
            "descriptor_set": attr.label(allow_single_file = True),
            "outs": attr.output_list(mandatory = True),
            "proto_name": attr.string(mandatory = True),
            "src": attr.label(allow_single_file = True, mandatory = True),
            "_tool": attr.label(
                default = "@protobuf//:protoc",
                executable = True,
                cfg = "exec",
            ),
        },
        implementation = _run_protoc_impl,
    )

_run_protoc_bin = _make_protoc_rule(output_to_genfiles = False)

_run_protoc_genfiles = _make_protoc_rule(output_to_genfiles = True)

# These are the values for proto_data's 'out_format' arg which are passed through unmodified.
_DIRECTLY_SUPPORTED_OUTPUT_FORMAT_ALIASES = ["jspb", "json"]
