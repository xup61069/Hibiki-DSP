# Source and distribution policy

GitHub is the canonical source repository. Source tags and release notes are
the release provenance; text manifests, SBOM and notices may accompany that
source metadata, but no GitHub release asset is uploaded and no compiled
executables, drivers, installers, packages or opaque prebuilt dependencies are
published.

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
