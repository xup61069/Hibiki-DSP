# Contributing to Hibiki DSP

Hibiki is developed as a public monorepo where human contributors and AI agents follow the
same issue-scoped workflow. The canonical rules live in a small set of documents; this file
points at them instead of restating them so it cannot drift.

## Read first

1. `AGENTS.md` — tiered limits, authority order, and the scope-triggered validation matrix.
2. `docs/START_HERE.md` — fresh-clone flow and live-check boundaries.
3. `docs/AI_HANDOFF.md` and `docs/ai/MULTI_AGENT.md` — the multi-contributor protocol.
4. `docs/specs/INDEX.md` and `docs/adr/` for the area you intend to touch.

## Claiming work

- Read-only reconnaissance needs no claim. Writes require explicit maintainer/orchestrator
  assignment, one GitHub Issue, one non-`main` branch named `codex/<issue>-<slug>`, one handoff
  block and one draft PR. A maintainer's direct request is assignment; agents do not self-select backlog.
- Use a dedicated worktree whenever another writer is active, the branch is occupied, or occupancy
  is uncertain; it remains recommended for a single writer. Live ownership initially comes from the
  Issue assignee, lifecycle label and handoff block. Open the draft PR after the first WIP/reviewable
  commit instead of creating an empty claim commit.
  Declare `scope_globs` and `shared_paths` in the Issue body handoff block before
  editing anything, and stay inside that scope.
- Never push to `main`, never force-push published branches, and never modify another lane's
  branch or worktree.

## Before requesting review

- Baseline counters are measured live by `tools/docs-check.ps1`; slices never edit a counter
  file (`build/baseline-counters.json` is retired since #197).
- Run the always-run checks listed in `AGENTS.md` (scoped handoff-check, docs-check,
  source-policy and `git diff --check`); add doctor/build/verify/workflow and subsystem gates
  only when your scope or acceptance triggers them.
- GitHub Actions must finish green before review is requested; queued or in-progress runs do
  not count as passing.

## Evidence honesty

Record what was actually executed in anonymous evidence under `evidence/`, and keep
limitation statements explicit: user-space probes are not physical-audio or driver/WaveRT
evidence, and opt-in live checks must never be triggered implicitly. This project does not
require HLK or any code signing.

## Licenses and dependencies

All contributions are accepted under the component license shown by their SPDX headers.
Use `Signed-off-by` (DCO style) and do not add dependencies without updating
`THIRD_PARTY.yml` and the SBOM policy. Keep private device data under `.local/`.
