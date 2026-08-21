---
schema_version: 2
issue: 123
branch: codex/123-short-slug
target_branch: main
base_commit: 0123456789abcdef0123456789abcdef01234567
status: planned
role: worker
owner: codex-example
updated_at: 2026-01-01T00:00:00+08:00
environment_fingerprint: unknown
scope_globs: ["path/to/subsystem/**", "tests/path/to/task_tests.cpp"]
shared_paths: ["path/to/CMakeLists.txt"]
depends_on: []
specs: [SPEC-0000]
adrs: []
next_safe_action: "Confirm the claim and run the task baseline."
resume_commands: ["git status --short --branch", "pwsh -File tools/handoff-check.ps1 -Issue 123"]
---

# Issue 123 handoff

## Objective

用一段話說明這個 Issue 唯一可驗收的工作切片。

## Acceptance

- 列出可觀察、可測試的完成條件。
- 若有 public contract、license、RT 或 privacy boundary，明確列出。

## Completed

- 尚未開始。

## Known limitations

- 記錄未覆蓋環境、硬體與不可宣稱的能力。

## Last verification

- 尚未執行 baseline。

## Remaining work

1. 只列出本 Issue scope 內的剩餘工作。

## Next safe action

Confirm the claim and run the task baseline.

## Resume commands

```powershell
git status --short --branch
pwsh -File tools/handoff-check.ps1 -Issue 123
```
