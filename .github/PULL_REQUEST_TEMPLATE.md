## 變更摘要

## 對應 Issue

Closes #

## 多 AI ownership

- Owner:
- Handoff: Issue body handoff block (linked issue)
- Head branch:
- Target branch: `main`
- `scope_globs`:
- `shared_paths`:
- `depends_on`:

- [ ] 一個 Issue、獨立 worktree、唯一 branch、單一 active writer
- [ ] 已檢查 open Issue／draft PR 與其他 Issue 的 handoff block，沒有未協調的 scope overlap
- [ ] 超出 scope 或共享整合檔已由 integrator 指定 owner／合併順序
- [ ] branch 未 force-push，且 handoff 的 base/owner/next action 已更新

## Spec / ADR / schema 影響

- [ ] 沒有 public contract 變更
- [ ] 已更新對應 Spec 或建立新的 ADR

## 驗證

- [ ] `pwsh -File tools/verify.ps1`
- [ ] `pwsh -File tools/docs-check.ps1`
- [ ] `pwsh -File tools/source-policy.ps1`
- [ ] `pwsh -File tools/source-only-ci-check.ps1`
- [ ] 已附 evidence manifest 或說明為何不適用

## 開源與隱私

- [ ] 沒有 binary、private calibration、endpoint ID、credential 或 ISO 受限內容
- [ ] `THIRD_PARTY.yml`、SPDX 與 NOTICE 已同步（若有依賴變更）
