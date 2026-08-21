# Document conflict policy

AI must stop making changes when two authoritative documents disagree in the same
domain. Record a `DOC-CONFLICT` in the active handoff with both paths, commits,
the observed contradiction and the smallest human decision required.

Authority domains are intentionally separate:

- workflow: `AGENTS.md`
- product behavior: accepted Spec
- architectural rationale: accepted ADR
- merged implementation state: source, tests and evidence
- branch intent: GitHub Issue and active handoff

## Multi-agent scope conflicts

AI must also stop writing the overlapping scope when two active claims use the
same Issue or branch, when their `scope_globs` overlap, or when both require the
same shared integration path. Record both Issues, branches, paths and base
commits in the Issue/PR. The integration coordinator must then name one writer,
split a child Issue or define a merge order before work resumes.

Do not resolve a scope conflict by copying, overwriting or merging an unverified
working tree. Live ownership comes from GitHub Issue/linked draft PR; durable
branch state comes from the corresponding active handoff. Full rules are in
`docs/ai/MULTI_AGENT.md`.
