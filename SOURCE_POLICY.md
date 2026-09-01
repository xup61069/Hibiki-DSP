# Source and distribution policy

GitHub is the canonical source repository. Git history, source tags, text
manifests, SBOM and notices remain the release provenance; no compiled output
is ever tracked in Git, cached by public CI, or uploaded by an Actions workflow.

ADR-0011 / SPEC-0026 permit one narrow manual exception: the official `v1.0.0`
GitHub Release may contain exactly an unsigned Windows x64 DesktopCompat
portable-preview ZIP and its SHA-256 sidecar. The ZIP is an external release
asset, never a tracked repository file or Actions artifact. It must pass the
strict portable-preview package check and may not contain a driver, installer,
service, signing material, VST3, or updater. Its self-contained .NET runtime payload must be
attributable, listed and hash-checked alongside every other package file, and the package must
include the source-tag `THIRD_PARTY.yml`; unlisted or opaque prebuilt dependencies remain prohibited.

Public CI may compile and test in an ephemeral workspace. It must not upload
artifacts, publish packages, persist build outputs in caches or use signing
permissions. The project does not use paid delivery, signing, activation or
runtime DRM, and HLK/WHCP is not a release prerequisite.

Every official source tag records toolchain, dependency lock, content hashes,
the non-empty `distribution_id` and test evidence in a text-only
`SourceReleaseManifest v1`. It declares `release_kind: source-only` and both
artifact statuses as `not-published`; it never contains package hashes. The
release notes, SPDX SBOM and notices remain text provenance. To avoid a
self-referential Git commit, an annotated source tag `T` directly points to a
single-parent provenance metadata commit whose only change is the newly added
`release/manifests/T.json`. That regular text blob records `source_tag: T`, a
matching tag version, and the metadata commit's direct parent as
`source_commit`. The tag-triggered provenance gate verifies the direct target,
schema, source-only status, parent identity, profile `distribution_id`, fixed
text-provenance paths, SPDX structure and raw Git-blob SHA-256 values before
accepting the metadata diff. It neither creates a tag nor publishes a release.
Anyone can obtain the corresponding source at no additional charge and rebuild
it.
