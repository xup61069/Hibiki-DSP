## 產品結果

- 使用者得到／修好什麼：
- 已知限制：

## Execution linkage

Closes #

- Owner:
- Head / target branch:
- `scope_globs` / `shared_paths`:
- Dependencies:

- [ ] 一張完整 execution Issue、唯一非 `main` branch、單一 writer、one PR per branch
- [ ] Handoff、assignee 與 `claimed`／`in-review` lifecycle 一致，沒有未協調 scope overlap
- [ ] 並行、occupied 或不確定時使用隔離 worktree；未修改或 force-push 別人的工作

## Acceptance 與驗證

- Acceptance 結果：
- Exact head SHA：
- 執行命令與結果：

- [ ] `AGENTS.md` always-run checks 全綠，scope-triggered gates 已執行或說明不適用
- [ ] Required hosted checks 對上述 exact head fresh green；queued、in-progress 或舊 head 不算
- [ ] Public contract 已同步 Spec／tests／evidence，或確認不適用
- [ ] Review 接受後立即轉 ready；draft 不停放在 green 狀態

若尚未能 ready／merge，只能填一項具體 safety、permission、scope 或 external blocker：

- Blocker（無則填 `none`）：

## Evidence、授權與隱私

- [ ] Source／user-space 證據未冒充實體音訊、driver/WaveRT、硬體或 release 證據
- [ ] 沒有 binary、credential、private calibration、真實裝置 ID 或 equal-loudness 受限內容
- [ ] 新／更正 evidence 使用 append-only `evidence_format: 2`；依賴變更同步 SPDX／NOTICE／SBOM

## Integrator clean close

- [ ] 合併後已 readback target／`main` 與 Issue closed，清除 lifecycle／assignee residue，且只依安全
  predicate 清理已完成 refs／worktrees（詳見 `docs/ai/MULTI_AGENT.md`）
