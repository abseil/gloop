# Gloop Release Process

This document details the automated release process for **Gloop**, including
versioning conventions, branch management, release creation, and publishing to
the **Bazel Central Registry (BCR)** via GitHub Actions.

--------------------------------------------------------------------------------

## Versioning & Branching Conventions

*   **Release Branch Format**: `YYYYMMDD.x` (e.g., `20260826.x`)
    *   Created off `main` when cutting a new release candidate or starting a
        new release cycle.
*   **Release Tag Format**: `YYYYMMDD.<patch>`
    *   **Release Candidates**: `YYYYMMDD.rc1`, `YYYYMMDD.rc2`, etc.
    *   **Initial Release**: `YYYYMMDD.0`
    *   **Patch Releases**: `YYYYMMDD.1`, `YYYYMMDD.2`, etc. (on the existing
        `YYYYMMDD.x` branch)

--------------------------------------------------------------------------------

## Cutting a Release

All releases are driven through GitHub Actions using the **Release** workflow
(`.github/workflows/release.yml`).

### Cutting a New Release Candidate from HEAD (Simplest Case)

To cut a new release candidate from `HEAD`:

1.  Navigate to **Actions** > **Release** > **Run workflow**.
2.  Click **Run workflow** (all default options apply: cuts `rc1` for today's
    date, creates branch `YYYYMMDD.x` off `main`, runs test matrix, publishes
    release, and triggers BCR publication).

Alternatively, using the `gh` CLI:

```bash
# Simplest single-command release candidate (creates YYYYMMDD.x branch and YYYYMMDD.rc1 tag):
gh workflow run release.yml
```

### Cutting an Initial or Final Release (`0`)

1.  Navigate to **Actions** > **Release** > **Run workflow**.
2.  Set `patch` to `0`.
3.  Click **Run workflow**.

Alternatively, using the `gh` CLI:

```bash
gh workflow run release.yml -f patch=0
```

### Releasing a Patch or New RC on an Existing Release Branch

To cut a subsequent candidate (`rc2`) or a patch release (`1`, `2`, ...) on an
existing release branch:

1.  Navigate to **Actions** > **Release** > **Run workflow** (from `main`).
2.  Set `branch` to the existing release branch (e.g. `20260826.x`).
3.  Set `patch` to the desired version (e.g. `rc2`, `1`, `2`).
4.  Click **Run workflow**.

Alternatively, using the `gh` CLI:

```bash
# Cutting rc2 on an existing release branch:
gh workflow run release.yml -f branch=20260826.x -f patch=rc2

# Cutting patch 1 on an existing release branch:
gh workflow run release.yml -f branch=20260826.x -f patch=1
```

--------------------------------------------------------------------------------

## Workflow Options

The **Release** workflow supports the following inputs:

| Input         | Type      | Default | Description                     |
| :------------ | :-------- | :------ | :------------------------------ |
| `patch`       | `string`  | `rc1`   | Version suffix/patch identifier |
:               :           :         : (`rc1`, `rc2`, `0`, `1`, `2`,   :
:               :           :         : etc.).                          :
| `branch`      | `string`  | `""`    | Target release branch (e.g.     |
:               :           :         : `20260826.x`). If left empty,   :
:               :           :         : uses current ref or             :
:               :           :         : auto-creates `YYYYMMDD.x` from  :
:               :           :         : today's UTC date.               :
| `run_tests`   | `boolean` | `true`  | Runs the full CI test suite     |
:               :           :         : matrix before tagging and       :
:               :           :         : publishing.                     :
| `publish_bcr` | `boolean` | `true`  | Publishes the release to Bazel  |
:               :           :         : Central Registry (BCR) via      :
:               :           :         : `bazel-contrib/publish-to-bcr`. :
| `draft`       | `boolean` | `false` | When `true`, creates the        |
:               :           :         : release as a draft for review   :
:               :           :         : instead of publishing           :
:               :           :         : immediately.                    :

--------------------------------------------------------------------------------

## Publishing to Bazel Central Registry (BCR)

### Automated Publishing (Single-Click Flow)

When the **Release** workflow runs:

1.  It cuts the branch/tag, creates a static release archive
    (`gloop-<tag>.tar.gz`), and creates the GitHub Release with the attached
    tarball.
2.  It automatically invokes `bazel-contrib/publish-to-bcr` to publish the
    release to the Bazel Central Registry (BCR).
3.  `bazel-contrib/publish-to-bcr` fetches the release archive, computes hashes,
    stamps the module version, and opens a pull request against
    [bazel-central-registry](https://github.com/bazelbuild/bazel-central-registry)
    from the configured fork (`abseil/bazel-central-registry`).

If a release was initially created as a draft (`draft: true`), publishing the
draft release later via the GitHub UI will automatically trigger the standalone
**Publish to BCR** workflow (`.github/workflows/publish-bcr.yml`).

### Manual Trigger / Retry

If a BCR publication needs to be retried manually:

1.  Navigate to **Actions** > **Publish to BCR** > **Run workflow**.
2.  Enter the release tag name (e.g., `20260826.rc1` or `20260826.0`).
3.  Click **Run workflow**.

--------------------------------------------------------------------------------

## Prerequisites & Setup

1.  **GitHub Secret (`BCR_PUBLISH_TOKEN`)**: Add `BCR_PUBLISH_TOKEN` to the
    repository secrets. This must be a Personal Access Token (PAT) with `repo`
    scope to open pull requests against `bazelbuild/bazel-central-registry`.
2.  **Registry Fork**: A fork of `bazelbuild/bazel-central-registry` under the
    repository org (e.g., `abseil/bazel-central-registry`).
3.  **`.bcr/` Configuration**:
    *   `.bcr/metadata.template.json`: Module metadata, maintainers, and
        homepage.
    *   `.bcr/source.template.json`: Source archive URL format (pointing to the
        stable release asset) and strip prefix.
    *   `.bcr/presubmit.yml`: BCR CI test configuration.
