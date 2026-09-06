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

"""Starlark implementation of cc_embed_data.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _root_mapper(f):
    root = f.root.path
    if root:
        return root
    else:
        return None

def _build_command_line_common(ctx, cc_toolchain, feature_configuration, use_objcopy, strip):
    command_line = ctx.actions.args()
    command_env = {}
    if use_objcopy:
        objcopy = ""
        objcopy_options = []

        ld = ""
        ld_options = []
        if cc_common.action_is_enabled(
            feature_configuration = feature_configuration,
            action_name = "objcopy_embed_data",
        ):
            objcopy = cc_common.get_tool_for_action(
                feature_configuration = feature_configuration,
                action_name = "objcopy_embed_data",
            )
            objcopy_options = cc_common.get_memory_inefficient_command_line(
                feature_configuration = feature_configuration,
                action_name = "objcopy_embed_data",
                variables = cc_common.empty_variables(),
            )
            command_env.update(cc_common.get_environment_variables(
                feature_configuration = feature_configuration,
                action_name = "objcopy_embed_data",
                variables = cc_common.empty_variables(),
            ))
        if cc_common.action_is_enabled(
            feature_configuration = feature_configuration,
            action_name = "ld_embed_data",
        ):
            ld = cc_common.get_tool_for_action(
                feature_configuration = feature_configuration,
                action_name = "ld_embed_data",
            )
            ld_options = cc_common.get_memory_inefficient_command_line(
                feature_configuration = feature_configuration,
                action_name = "ld_embed_data",
                variables = cc_common.empty_variables(),
            )
            command_env.update(cc_common.get_environment_variables(
                feature_configuration = feature_configuration,
                action_name = "ld_embed_data",
                variables = cc_common.empty_variables(),
            ))
        command_line.add("--objcopy", objcopy)
        command_line.add_joined("--objcopy_opts", objcopy_options, join_with = " ", omit_if_empty = False)
        command_line.add_joined("--ldopts", ld_options, join_with = " ", omit_if_empty = False)
        command_line.add("--ld", ld)
    else:
        command_line.add("--data_in_cc")

    if ctx.attr.flatten:
        command_line.add("--flatten")
    if ctx.attr.redact_filename:
        command_line.add("--redact_filename")

    # only include strip args if we're going to pass in the files (which we don't
    # for the .h outputting action)
    if strip:
        # Note the order of the --strip arguments matter:
        # //tools:filewrapper will apply each argument in order to the same file
        # path, overwritting the previous string after strip.
        # First, strip bazel artifact paths like bazel-out/k8-opt/bin, etc.
        # Order within this section doesn't matter, since a source will only be under
        # one artifact root.
        # We grab them off the src files to handle non-standard paths such as those
        # caused by transitions
        command_line.add_all(
            ctx.files.srcs,
            map_each = _root_mapper,
            uniquify = True,
            before_each = "--strip",
        )

        # Second, strip the actual google3 prefix w.r.t. the cc_embed_data's BUILD
        # package.
        prefix = ctx.label.package
        if len(ctx.attr.strip) != 0:
            if paths.is_absolute(ctx.attr.strip):
                prefix = ctx.attr.strip.lstrip("/")
            else:
                prefix = paths.join(ctx.label.package, ctx.attr.strip)
        prefix = paths.normalize(prefix)
        command_line.add("--strip", prefix)

    command_line.add("--include_path", ctx.label.package)

    # Do this after adding all the other command line arguments so that user-provided arguments
    # override the ones specified above.
    command_line.add_all(ctx.attr.embedopts)

    return command_line, command_env

def _cc_skylark_embed_data_impl(ctx):
    cc_toolchain = find_cc_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    cc_file_artifact = None
    h_file_artifact = None
    o_file_artifact = None
    for output in ctx.outputs.outs:
        if output.path.endswith(".cc") or output.path.endswith(".cpp"):
            cc_file_artifact = output
        elif output.path.endswith(".h"):
            h_file_artifact = output
        elif output.path.endswith(".o"):
            o_file_artifact = output

    has_legacy_tool_paths = False

    has_objcopy_embed_data_action = cc_common.action_is_enabled(
        feature_configuration = feature_configuration,
        action_name = "objcopy_embed_data",
    )
    has_ld_embed_data_action = cc_common.action_is_enabled(
        feature_configuration = feature_configuration,
        action_name = "ld_embed_data",
    )
    has_objcopy_actions = has_objcopy_embed_data_action and has_ld_embed_data_action

    is_objcopyable_cpu = True
    if cc_toolchain.cpu:
        # RISC-V cannot use objcopy if the lld linker is in use.  Because most
        # of google3 code is built with lld, assume that we are using lld, which
        # cannot support the linking of the generated object files.  This
        # restriction can be lifted if D106378 is merged upstream.  We perform a
        # string match as there are a multitude of RISCV environments (e.g.
        # rv64gcv, rv32im, riscv64 etc).
        if cc_toolchain.cpu.startswith("rv") or cc_toolchain.cpu.startswith("riscv"):
            is_objcopyable_cpu = False

    use_objcopy = (
        o_file_artifact != None and
        (has_objcopy_actions or has_legacy_tool_paths) and
        is_objcopyable_cpu
    )

    files_to_build = None
    if use_objcopy:
        files_to_build = depset([cc_file_artifact, h_file_artifact, o_file_artifact])
    else:
        files_to_build = depset([cc_file_artifact, h_file_artifact])

    base_name = paths.split_extension(h_file_artifact.basename)[0]

    header_command_line, header_env = _build_command_line_common(ctx, cc_toolchain, feature_configuration, use_objcopy, strip = False)
    header_command_line.add("--nocreate_impl")
    header_command_line.add("--out_h", h_file_artifact)
    header_command_line.add(base_name)
    ctx.actions.run(
        inputs = [],
        outputs = [h_file_artifact],
        arguments = [header_command_line],
        env = header_env,
        mnemonic = "CcEmbedDataHeader",
        progress_message = (
            "Creating cc_embed_data header for {}".format(ctx.label)
        ),
        executable = ctx.executable._filewrapper,
    )

    cc_o_outputs = [cc_file_artifact]
    cc_o_command_line, cc_o_env = _build_command_line_common(ctx, cc_toolchain, feature_configuration, use_objcopy, strip = True)
    if use_objcopy:
        cc_o_outputs.append(o_file_artifact)
        cc_o_command_line.add("--out_o", o_file_artifact)

    cc_o_command_line.add("--nocreate_header")
    cc_o_command_line.add("--out_cc", cc_file_artifact)
    cc_o_command_line.add(base_name)
    cc_o_command_line.add_all(ctx.files.srcs)
    ctx.actions.run(
        inputs = depset(
            ctx.files.srcs,
        ),
        outputs = cc_o_outputs,
        arguments = [cc_o_command_line],
        env = cc_o_env,
        mnemonic = "CcEmbedData",
        progress_message = (
            "Embedding data {}".format(ctx.label)
        ),
        executable = ctx.executable._filewrapper,
    )

    (compilation_context, compilation_outputs) = cc_common.compile(
        name = ctx.label.name,
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        srcs = [cc_file_artifact],
        public_hdrs = [h_file_artifact],
        compilation_contexts = [
        ],
    )

    if use_objcopy:
        additional_output_files = cc_common.create_compilation_outputs(
            objects = depset([o_file_artifact]),
            pic_objects = depset([o_file_artifact]),
        )
        compilation_outputs = cc_common.merge_compilation_outputs(
            compilation_outputs = [compilation_outputs, additional_output_files],
        )
    elif o_file_artifact != None:
        # Add a generating action that fails if anything explicitly depends on the '.o' file.
        ctx.actions.run_shell(
            mnemonic = "CcEmbedDataFail",
            inputs = [],
            outputs = [o_file_artifact],
            progress_message =
                "Failing after trying to get object file {}".format(o_file_artifact.path),
            command = "fail",
        )

    supports_dynamic_linker = cc_common.is_enabled(
        feature_configuration = feature_configuration,
        feature_name = "supports_dynamic_linker",
    )
    (linking_context, _linking_outputs) = cc_common.create_linking_context_from_compilation_outputs(
        name = ctx.label.name,
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        compilation_outputs = compilation_outputs,
        alwayslink = ctx.attr.alwayslink,
        disallow_dynamic_library = ctx.attr.linkstatic or not supports_dynamic_linker,
    )

    return [
        DefaultInfo(files = files_to_build),
        CcInfo(
            compilation_context = compilation_context,
            linking_context = linking_context,
        ),
    ]

cc_embed_data = rule(
    implementation = _cc_skylark_embed_data_impl,
    output_to_genfiles = True,
    attrs = {
        "alwayslink": attr.bool(
            default = False,
            doc = """
See <link>.alwayslink
""",
        ),
        "embedopts": attr.string_list(
            doc = """
A list of options that will simply be passed through to `//tools:filewrapper`.
""",
        ),
        "flatten": attr.bool(
            doc = """
If non-zero, the leading path components are removed from each `srcs` file.

The `srcs` list may contain files that are pulled in from other parts of `google3`. `flatten = 1`
prevents the `//directory/path/at/the/beginning` from cluttering up the names in the table of
contents, preserving only the base name of the file. `flatten` overrides `strip`. This only affects
how the names are stored in the table of contents.
""",
        ),
        "linkstatic": attr.bool(
            default = False,
            doc = """
See <link>.linkstatic
""",
        ),
        "outs": attr.output_list(
            mandatory = False,
            doc = """
A list of output files generated by this rule.

You can provide names for all three of the generated files or omit the `*.o`. They are recognized by their
suffixes: `.cc`;`.h`; and `_data.o`. The build system enforces that all output files go in the
same directory.
""",
        ),
        "redact_filename": attr.bool(
            doc = """
If true, the `name` member of the FileToc structure will be empty. This is useful if the binary
is published externally, for example in an Android app, and the file name would leak information
about unreleased products or features.
""",
        ),
        # SKIP_CONSTRAINTS_OVERRIDE will prevent errors due to dependency compatibility checks, see:
        # https://bazel.build/extending/rules#constraints
        "srcs": attr.label_list(
            allow_files = True,
            flags = ["SKIP_CONSTRAINTS_OVERRIDE"],
            doc = """
The data files to be encapsulated.

These names also identify the files in the table of contents.  If the files are all in a
subdirectory, you can use the `strip` argument to remove the directory name.
""",
        ),
        "strip": attr.string(
            doc = """
A leading directory name to be removed from the beginning of each `srcs` file.

It's likely that a package's to-be-wrapped files will be kept in a subdirectory.  The `strip`
option removes the directory name.  This only affects how the names are stored in the table of
contents.

The package of the label that declares the embed rule will be stripped automatically.

Example values: "`statstables/splits`", "`resources`", "`//mydata/resources/names_prefixes`".
""",
        ),
        "_filewrapper": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("//gloop/tools:filewrapper_impl"),
        ),
        "_use_auto_exec_groups": attr.bool(default = False),
    },
    fragments = ["google_cpp", "cpp"],
    provides = [CcInfo],
    toolchains = use_cc_toolchain(),
    doc =
        """
Add `load("//tools/build_defs/cc:cc_embed_data.bzl", "cc_embed_data")` to BUILD file.
`cc_embed_data()` encapsulates a collection of files, as-is, into an object file, so that their
contents are accessible from the program as constant byte arrays.

A *table of contents* is generated, containing the information needed to refer to those bytes.
This is an array of name-&gt;value pairs.  Each name is the name of a source data file. The
corresponding value is the address of that file's data and the length of that file's data.

The `cc_embed_data` rule works like a `cc_library` - it compiles the generated code, and it should
be placed in the deps attribute of dependent rules.

Either two or three output files are generated, based on the `outs` attribute.
* A `.h` file, containing the declarations for the table of contents, is always generated. This
  header can be included from rules that directly depend on the `cc_embed_data` rule.
* A `.cc` file, containing the data for the table of contents, is always generated and compiled.
* A `_data.o` file, containing the wrapped files, is optionally generated. If the `_data.o` file is
  not mentioned in `outs`, the wrapped files are instead encapuslated in the `.cc` file. (This is
  useful on platforms where the `_data.o` file cannot be generated directly.)

#### Notes

* *Either two or three output files must be listed.* It is an error to declare fewer than two or
  more than three files, or to declare files that do not match the pattern above.

* *The order of the files in the table of contents is the same as their order in the `srcs`
  attribute.*
""" +
        """
* The tool that does the work is `//tools:filewrapper`. Please direct questions about it to
  the bazel team via <link>.

#### Examples
This example shows how one can encapsulate some graphics, JavaScript, and a stylesheet, to be
embedded in a program that needs them for its web console.

```
# from <path>

cc_embed_data(
    name = "this_package_resources",  # must be unique, see attribute documentation
    srcs = [
        "resources/console.css",
        "resources/overlib.js",
        "resources/sorttable.js",
        "resources/r10.png",
        "resources/dr10.png",
        "resources/g10.png",
        "resources/dg10.png",
        "resources/k10.png",
    ],
    outs = [
        "this_package_resources.cc",
        "this_package_resources.h",
        "this_package_resources_data.o",
    ],
    embedopts = ["--namespace=my_monitoring"],
    strip = "resources",  # remove the directory name
)
```
For backwards compatibility, it is still possible to place the rule into the srcs of a cc_library.
However, the recommended style is to reference it only in the deps, as shown here.

```
cc_library(
    name = "b3m",
    srcs = [
        ...
        "b3m.cc",
    ],
    deps = [
        ...
        ":this_package_resources",
    ],
)
```
The generated header file contains the declarations needed to access the encapsulated files.

```
/* The canonical definition is in "gloop/base/file_toc.h". */
struct FileToc {
  const char* name;             // the file's original name
  const char* data;             // beginning of the file
  size_t size;                  // length of the file
  unsigned char md5digest[16];  // MD5 digest of the file
};
namespace my_monitoring {
extern const struct FileToc* this_package_resources_create();  // name_create()
}  // my_monitoring namespace
```

For most purposes, you can obtain the definition of this structure simply by #including the `rule.h`
generated header file.  If you're writing a program that manipulates `FileToc` objects generically
and do not have access to a generated header, do not copy-and-paste the above declaration, but
instead `#include gloop/base/file_toc.h`. This will insulate you against breakage caused
by future changes in this structure.

The data in memory is always followed by a `NUL` character (ASCII 0), so if the original
file contains no embedded `NUL` characters, `data` can be treated as a C string.  The extra `NUL` is
not counted in the `size` or `md5digest` fields; `size` will be the length of the original file,
and `md5digest` will be its MD5 digest.

The `name_create()` procedure returns an array of table-of-contents entries. The
array is terminated with an entry in which all fields are set to `NULL`. The code needed
to use this is simple:

```
for (const FileToc* p = this_package_resources_create() ; p->name != nullptr ; ++p) {
  //  p->name       has the file name - "console.css".
  //  p->data       points to the start of the data.
  //  p->size       has the size of the data.
  //  p->md5digest  has the MD5 digest of the data.
}
```

The name will be used to generate an external symbol, so it must be globally unique. The compiler
might *not* warn you otherwise, leading to subtle and time-consuming bugs. This constraint may be
relaxed by passing the `--namespace=` argument in `embedopts`, which makes the generated code be in
the specified C++ namespace. In that case, the name must be unique only within that namespace.

`data`, `deps`, `deprecation`, `distribs`, and `licenses` attributes are not permitted.
""",
)
