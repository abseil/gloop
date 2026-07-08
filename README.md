# Gloop

test

Gloop is a library designed to speed up the transition of internal code to open
source, minimizing friction for projects moving out of Google's internal
monorepo (google3). It contains primarily common C++ libraries that were
previously internal to Google.

## Disclaimer & Intended Audience

Gloop is strictly intended for a limited set of Google projects. It is not a
general-purpose open-source library and should not be directly depended on by
non-Google projects. Depending on Gloop has the following consequences:

-   **Frequent Breaking Changes**: Gloop follows an aggressive release model
    aligned with Google's internal monorepo. We intentionally make regular
    breaking changes and do not offer Long-Term Support (LTS) releases or
    backward compatibility guarantees.
-   **Strict Expectations**: Users must live at head and stay up-to-date with
    the Gloop main branch.
-   **No External Support**: Issues and pull requests submitted by external
    contributors will not be accepted. If you use a project that depends on
    Gloop, please channel your request through that project.
-   **Commit Consistency**: We reserve the right to edit our history on the
    `main` branch in order to obliterate changes. This is only for exceptional
    situations though, and won't be done regularly.

## Purpose and Scope

Gloop sits above Abseil, which has strict requirements for quality and a wide
support matrix to cover OSS, but below domain-specific libraries exported by
Google. It consists of common C++ libraries and build tools, that are general
purpose and not specific to any particular product.

The goal of Gloop is to provide an intermediate layer facilitating Google
projects moving out of our internal monorepo without having to restrict their
dependencies to existing open-source libraries. Historically, this code was not
available and open-source replacements either didn't exist or were difficult to
migrate to.

Gloop itself is largely considered technical debt. Much of the code it contains
is very old, and was never intended for external use. The long-term vision for
code in Gloop is to either delete, replace, or re-home everything in it into
another repository with a more principled and longer-term support strategy such
as Abseil.

--------------------------------------------------------------------------------

## Support Policy and Matrix

Gloop maintains a minimal, highly restrictive support matrix aimed at maximizing
development velocity and ensuring consistency with the core development
environment. We test our full support matrix and strictly enforce it.
Unsupported environments or compilers are expressly rejected whenever possible.

### Architecture & Operating Systems

Environment      | Supported Version | Last Changed | Next Change
:--------------- | :---------------- | :----------- | :----------
**Architecture** | x86_64, ARM64     | -            | -
**Debian**       | >= 13             | 2026-02-03   | TBD
**Ubuntu**       | >= 24.04          | 2026-03-06   | TBD
**Linux Kernel** | >= 6.6            | 2026-04-16   | TBD

Windows and Apple operating systems are unsupported and blocked by policy.

### Build & Toolchains

Component        | Supported Version  | Last Changed | Next Change
:--------------- | :----------------- | :----------- | :----------
**Clang**        | >= 21              | 2026-02-03   | 2026-08-24
**Bazel**        | >= 8 (Bzlmod only) | 2026-02-03   | 2027-01-20
**C++ Standard** | >= 20              | 2026-02-03   | TBD
**libc++**       | >= 21              | 2026-04-16   | 2026-08-24
**glibc**        | >= 2.39            | 2026-04-16   | TBD

We maintain a trailing support window for our build dependencies. The timer
starts when a new version is released, and we advance our baseline after a fixed
period (e.g., 1 year for Bazel and 6 months for Clang/libc++). C++ standard
transitions track internal rollouts.

Alternative build systems (CMake, Make), compilers (GCC, MSVC), and standard
libraries (libstdc++) are completely unsupported and blocked by policy.

### Build Flags

To maintain consistency with internal environments, Gloop requires the following
build flags:

-   `-fno-exceptions` - C++ exceptions are not supported in Gloop code
-   `-funsigned-char` - The char type is assumed to be unsigned

## Contact

If you encounter problems or have questions, please contact the maintainers of
the project using Gloop.
