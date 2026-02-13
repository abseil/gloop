# Gloop

go/gloop

`gloop` is a library designed to speed up the process of sharing code originally
written in Google's codebase.

## Purpose and Scope

-   **Layering**: `gloop` sits above `absl`, which has strict requirements for
    quality and fit-for-use for a wide range of OSS, but below the
    domain-specific libraries of the projects being exported.
-   **Content**: Utility functions that are not Google production or
    product-specific.

The goal of `gloop` is to provide a middle layer of utilities that facilitates
the transition of internal code to open source without the immediate need for a
full OSS-ready library like `absl`.

## Contact

If you have any questions, please reach out to the gloop-eng@ mailing list.
