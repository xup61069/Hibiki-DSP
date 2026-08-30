# Contributing to Hibiki DSP

Hibiki is developed as a public monorepo where human contributors and AI agents follow the
same issue-scoped workflow. The canonical rules live in a small set of documents; this file
points at them instead of restating them so it cannot drift.

## Read first

1. `AGENTS.md` — tiered limits, authority order, and the scope-triggered validation matrix.
2. `docs/START_HERE.md` — fresh-clone flow and live-check boundaries.
3. `docs/AI_HANDOFF.md` and `docs/ai/MULTI_AGENT.md` — the multi-contributor protocol.
4. `docs/specs/INDEX.md` and `docs/adr/` for the area you intend to touch.

## Delivering work

- A fix or implementation request means implement it, run matching verification, and integrate it.
  Issues, branches, PRs, and CI are coordination records, not completion or stopping points.
- Unless the maintainer explicitly asks for backlog planning, do not create candidate, TBD,
  pre-claim, or queued Issues. Create one fully specified execution Issue only when its writer will
  start immediately. Before writing, complete its handoff, serialized claim, non-`main` branch,
  exclusive `scope_globs`, and ownership checks.
- Use a dedicated worktree when another writer is active, the branch is occupied, or occupancy is
  uncertain. Never modify another writer's branch/worktree or force-push a published branch.
- Open one draft PR after the first reproducible WIP/reviewable push, then keep working through
  acceptance, fresh exact-head green checks, ready state, and merge. Pause only for a concrete
  safety, permission, scope, or external blocker and record it in the handoff. Integrators drain
  safe green PRs before finding or scheduling more work.

## Verification

Run the always-run and scope-triggered checks in `AGENTS.md`. Queued, in-progress, stale-head, or
branch-only checks do not prove the integrated result. Baseline counters are measured live by
`tools/docs-check.ps1`; contributors do not maintain a counter file.

## Evidence honesty

Record what was actually executed in anonymous evidence under `evidence/`, and keep
limitation statements explicit: user-space probes are not physical-audio or driver/WaveRT
evidence, and opt-in live checks must never be triggered implicitly. This project does not
require HLK or any code signing.

## Licenses and dependencies

All contributions are accepted under the component license shown by their SPDX headers.
Use `Signed-off-by` (DCO style) and do not add dependencies without updating
`THIRD_PARTY.yml` and the SBOM policy. Keep private device data under `.local/`.
