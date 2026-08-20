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
