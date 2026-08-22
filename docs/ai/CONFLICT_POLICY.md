# Document conflict policy

Workers must stop making changes when two authoritative documents disagree in the same
domain. Record a `DOC-CONFLICT` in the Issue-body handoff block with both paths, commits,
the observed contradiction and the smallest human decision required; the orchestrator
coordinates the decision.

Authority domains are intentionally separate:

- workflow: `AGENTS.md`
- product behavior: accepted Spec
- architectural rationale: accepted ADR
- merged implementation state: source, tests and evidence
- branch intent: GitHub Issue and active handoff

## Multi-agent scope and lane conflicts

Workers must also stop writing the overlapping scope when two assigned slices use the
same directory lane (`driver/`, `tools/`, `apps/`, `docs/`, `extensions/`,
`vst-host/`, `src/`), Issue or branch, when their `scope_globs` overlap, or when both
require the same shared integration path. Record both Issues, branches, paths and base
commits in the affected Issue/PR. The orchestrator—not whoever commits first—must then
name one writer, split child Issues across separate lanes or define a merge order before
work resumes. Workers do not self-arbitrate cross-lane conflicts.

Do not resolve a scope conflict by copying, overwriting or merging an unverified
working tree. Assignment ownership comes from the orchestrator-assigned GitHub Issue
assignee/lifecycle label/linked draft PR; durable branch state comes from the corresponding
issue-body handoff block. Full rules are in `docs/ai/MULTI_AGENT.md`.
