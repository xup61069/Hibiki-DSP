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

- [ ] 一個 Issue、唯一非 main branch、單一 active writer；並行／occupied／不確定時使用獨立 worktree
- [ ] 已檢查 open Issue／draft PR 與其他 Issue 的 handoff block，沒有未協調的 scope overlap
- [ ] 超出 scope 或共享整合檔已由 integrator 指定 owner／合併順序
- [ ] branch 未 force-push，且 handoff 的 base/owner/next action 已更新

## Spec / ADR / schema 影響

- [ ] 沒有 public contract 變更
- [ ] 已更新對應 Spec 或建立新的 ADR

## 驗證

依 [AGENTS.md](../AGENTS.md) 第三層執行 always-run checks（scoped handoff-check／docs-check／
source-policy／`git diff --check`）與範圍相關的條件式 gates：

- [ ] always-run checks 全綠（命令與結果記錄在 handoff block）
- [ ] 範圍相關的條件式 gates 已執行或明確標註不適用
- [ ] 已附 evidence manifest 或說明為何不適用
- [ ] 新／更正 evidence 使用 append-only `evidence_format: 2`，完整 `evidence-audit.ps1` 已通過；未覆寫 legacy manifest

## 開源與隱私

- [ ] 沒有 binary、private calibration、endpoint ID、credential 或 ISO 受限內容
- [ ] `THIRD_PARTY.yml`、SPDX 與 NOTICE 已同步（若有依賴變更）
