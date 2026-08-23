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

## Multi-agent scope and semantic conflicts

Workers must also stop writing the overlapping scope when two assigned slices use the
same Issue or branch, when their `scope_globs` overlap, when both change the same public
contract semantics, or when both require the same shared integration path. Directory lanes
are routing hints only; two non-overlapping slices in `docs/` or `tools/` are not conflicting
merely because their first path segment matches. Record both Issues, branches, paths and base
commits in the affected Issue/PR. The orchestrator—not whoever commits first—must then name
one writer, split child Issues or define a merge order before work resumes.

Do not resolve a scope conflict by copying, overwriting or merging an unverified
working tree. Assignment ownership comes from an explicitly assigned GitHub Issue,
assignee/lifecycle label and handoff block; the linked draft PR appears after the first
WIP/reviewable commit. Durable branch state comes from the corresponding
issue-body handoff block. Full rules are in `docs/ai/MULTI_AGENT.md`.
