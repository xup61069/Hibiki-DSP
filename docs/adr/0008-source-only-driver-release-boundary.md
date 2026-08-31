# ---
# id: ADR-0008
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-31
# last_reviewed: 2026-08-31
# review_after_days: 90
# supersedes: [ADR-0004]
# related_specs: [SPEC-0003, SPEC-0005]
# source_globs: ["docs/adr/0004-fixed-endpoint-topology-and-channel-masks.md", "docs/specs/SPEC-0003-virtual-endpoints-and-routing.md", "driver/README.md", "docs/state/BASELINE.md"]
# ---

# ADR-0008：source-only driver release-boundary supersession

## Status

Accepted

## Context

ADR-0004 fixed the endpoint topology and Windows channel-mask contract. Its
final consequence also described INF, HLK, Microsoft signing and physical
plug/unplug validation as later release gates. That delivery wording conflicts
with ADR-0006, ADR-0007, SPEC-0005 and SOURCE_POLICY: Hibiki is source/text-only
and neither HLK nor signing is a release requirement. ADR-0004 is accepted and
must remain intact for the topology decision, so the narrow conflict needs an
explicit supersession boundary.

## Decision

- This ADR supersedes only ADR-0004's consequence that treats INF, HLK or
  Microsoft signing as a release gate. ADR-0004 remains accepted for the four
  endpoint identities, channel counts, channel masks, rates, buffer sizes and
  Apache/MS-PL topology boundary.
- A source-built package may be checked on an explicitly approved target
  machine, but Secure Boot/HVCI acceptance is a user-environment limitation,
  not a Hibiki signing task or delivery guarantee. No paid channel, Gumroad
  artifact, signing credential or HLK/WHCP workflow is introduced.
- Source, WDK and documentation checks can only establish their stated source
  contracts. Any claim about driver loading, WaveRT streaming, endpoint
  behavior, installation lifecycle or physical audio requires separately
  obtained target-machine evidence and explicit machine-state consent.

## Consequences

- Current product documentation must not list signing, HLK, Gumroad delivery or
  a signability gate as V1 work. It must instead describe the actual remaining
  source and target-machine boundaries precisely.
- The project can keep its fixed topology contract and source-only release
  policy without implying a consumer-ready driver package or physical-audio
  evidence.
