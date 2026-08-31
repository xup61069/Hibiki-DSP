# ---
# id: ADR-0007
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-31
# last_reviewed: 2026-08-31
# review_after_days: 90
# supersedes: [ADR-0001]
# related_specs: [SPEC-0005]
# source_globs: ["docs/adr/0001-public-monorepo-and-component-licenses.md", "docs/adr/0006-no-hlk-no-signing-release.md", "docs/specs/SPEC-0005-source-only-paid-release.md", "SOURCE_POLICY.md", "schemas/release-manifest-v1.schema.json", "installer/HibikiSetup.ps1", "driver/inf/HibikiVirtualAudio.inf"]
# ---

# ADR-0007：source-only release policy supersession boundary

## Status

Accepted

## Context

ADR-0001 established the public monorepo and the separate component-license
decisions. Its historical release paragraph also described a paid,
signed-installer delivery path, which conflicts with the later accepted
no-HLK/no-signing decision in ADR-0006.

## Decision

- This ADR supersedes only the signed and paid-release clauses of ADR-0001.
  ADR-0001 remains accepted for the public monorepo, GPL user-space, MS-PL
  driver, Apache-2.0 SDK/schema and CC-BY documentation decisions.
- ADR-0006 and SPEC-0005 are the authoritative release policy: Hibiki publishes
  source and text provenance only, without a GitHub release asset, compiled
  binary, signing requirement, paid channel or HLK/WHCP prerequisite.
- `ReleaseManifest v1` records reproducible source identity, the required
  non-empty `distribution_id`, content hashes, SBOM and test metadata. It does
  not create a release artifact or grant a driver-install guarantee.

## Consequences

The repository can describe a complete source release without implying that a
private signing service, paid distribution channel or binary custody exists.
The component-license boundary and GPL redistribution rights from ADR-0001
remain unchanged. Secure Boot/HVCI acceptance of a locally rebuilt kernel
driver remains an external user-environment concern, not a project release
gate.
