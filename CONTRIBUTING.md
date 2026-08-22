# Contributing to Hibiki DSP

Hibiki is developed as a public monorepo where human contributors and AI agents follow the
same issue-scoped workflow. The canonical rules live in a small set of documents; this file
points at them instead of restating them so it cannot drift.

## Read first

1. `AGENTS.md` — hard limits, authority order, and the required gate commands.
2. `docs/START_HERE.md` — fresh-clone flow and live-check boundaries.
3. `docs/AI_HANDOFF.md` and `docs/ai/MULTI_AGENT.md` — the multi-contributor protocol.
4. `docs/specs/INDEX.md` and `docs/adr/` for the area you intend to touch.

## Claiming work

- One GitHub Issue equals one isolated worktree, one branch named `codex/<issue>-<slug>`,
  one handoff block embedded in the Issue body, and one draft PR. Lifecycle is expressed
  by labels (`claimed` = in progress, `in-review` = ready for review) and issue state.
- Live ownership is the Issue assignee plus lifecycle label plus linked draft PR.
  Declare `scope_globs` and `shared_paths` in the Issue body handoff block before
  editing anything, and stay inside that scope.
- Never push to `main`, never force-push published branches, and never modify another lane's
  branch or worktree.

## Before requesting review

- If your slice adds or removes tracked files, refresh the verification-summary counters in
  `docs/state/BASELINE.md` in the same commit series; `tools/docs-check.ps1` fails closed on
  drift between claimed numbers and the measured tree.
- Run at minimum `tools/handoff-check.ps1 -Issue <n>`, the full `tools/handoff-check.ps1`,
  `tools/docs-check.ps1`, `tools/source-policy.ps1`, `tools/source-only-ci-check.ps1` and
  `git diff --check`; add the gates listed in `AGENTS.md` that match your scope.
- GitHub Actions must finish green before review is requested; queued or in-progress runs do
  not count as passing.

## Evidence honesty

Record what was actually executed in anonymous evidence under `evidence/`, and keep
limitation statements explicit: user-space probes are not physical-audio, driver/WaveRT,
HLK or Microsoft-signing evidence, and opt-in live checks must never be triggered implicitly.

## Licenses and dependencies

All contributions are accepted under the component license shown by their SPDX headers.
Use `Signed-off-by` (DCO style) and do not add dependencies without updating
`THIRD_PARTY.yml` and the SBOM policy. Keep private device data under `.local/`.
