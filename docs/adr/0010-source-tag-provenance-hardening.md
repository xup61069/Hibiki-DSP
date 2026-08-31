# ---
# id: ADR-0010
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-31
# last_reviewed: 2026-08-31
# review_after_days: 90
# supersedes: [ADR-0009]
# related_specs: [SPEC-0005, SPEC-0025]
# source_globs: ["docs/adr/0009-source-release-manifest-boundary.md", "schemas/source-release-manifest-v1.schema.json", "tools/release-provenance-check.ps1", "docs/specs/SPEC-0005-source-only-paid-release.md", "docs/specs/SPEC-0025-source-tag-manifest-provenance.md", "SOURCE_POLICY.md"]
# ---

# ADR-0010：Source-Tag Provenance Hardening Boundary

## Status

Accepted

## Context

ADR-0009 established the separation between the official source-tag manifest
and the external `ReleaseManifest v1` used by an installer outside the source
repository. Its broad decision remains sound, but the original source-tag
clauses did not fully express the source-only artifact status, canonical text
provenance locations, or the identity and raw-blob checks now required to make
a V1 source tag independently verifiable.

ADR-0009 is already accepted and must remain intact as the historical decision
for manifest separation. The narrower provenance requirements therefore need a
new decision rather than a rewrite of the accepted ADR.

## Decision

- This ADR supersedes only ADR-0009's official source-tag manifest field and
  verification clauses. ADR-0009 remains accepted for the
  `SourceReleaseManifest v1` versus external `ReleaseManifest v1` boundary and
  for the prohibition on fabricated binary hashes.
- An official source tag uses `SourceReleaseManifest v1` with
  `release_kind: source-only` and exactly two artifact statuses: `driver` and
  `installer` are both `not-published`. It contains no package, catalog,
  installer hash, signed-payload hash, or payload declaration.
- The provenance gate must fail closed unless the annotated tag, its
  single-parent metadata commit, the newly added manifest, tag/version,
  `distribution_id`, canonical toolchain/dependency/SBOM/release-note/notice
  paths, SPDX structure, and every raw Git-blob SHA-256 bind to the source
  commit. The metadata commit may add only
  `release/manifests/<tag>.json`.
- ADR-0006, ADR-0007, and ADR-0008 remain unchanged: the project publishes
  source and text provenance only, with no binary delivery, signing, HLK, or
  physical-audio claim introduced by this decision.

## Consequences

- The source-tag schema, validation tool, release-policy specifications, and
  source policy must use the same immutable add-only topology and source-only
  artifact semantics.
- A passing provenance gate is source-level evidence only. It does not create a
  tag or GitHub Release, publish an artifact, install a driver, establish
  WaveRT streaming, control an endpoint, or verify physical audio.
