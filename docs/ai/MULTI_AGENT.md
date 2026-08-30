# Hibiki DSP 多 AI 並行開發協定

本檔只定義多 writer 的 ownership、交接與整合。產品真值與驗證門檻仍以 `AGENTS.md`、
`docs/START_HERE.md`、accepted Spec／ADR、source／tests／evidence 為準。

## Execution-first 契約

- Maintainer 要求修正／實作時，預設工作是實作、匹配驗證並整合到 `main`。Issue、branch、PR 與
  CI 只是協作／稽核記錄，不是成果或停止點。
- 除非 maintainer 明確要求規劃 backlog，AI 不得建立 candidate、TBD、pre-claim 或排隊 Issue。
  只有 writer 會立即開始時，才建立一張欄位完整的 execution Issue。
- Draft PR 必須持續推進到 acceptance、fresh exact-head checks 全綠、ready 與 merge；只有具體
  safety、permission、scope 或 external blocker 才能暫停，且 blocker 必須寫回 handoff。
- Integrator 每輪先 drain 可安全合併的 green PR，再發現或安排新工作。

## Ownership 不變式

每個可獨立驗收的寫入切片一對一擁有：

1. 一張 execution Issue，含 objective、acceptance、owner、branch、base、`scope_globs`、
   `shared_paths`、dependencies、verification 與唯一 `Next safe action`。
2. 一個 `<agent>/<issue>-<slug>` 非 `main` branch。
3. Issue body 內一個 `<!-- hibiki:handoff-v1 -->` block、唯一 assignee，以及一個 lifecycle label：
   `claimed`（實作中）或 `in-review`（待整合）。
4. 首次可重建的 WIP/reviewable push 後建立的一張 draft PR；不做空認領 commit。
5. 其他 writer 活躍、branch 已被 worktree 佔用或 occupancy 不確定時，使用獨立 clone／worktree。

兩個 writer 不得共用 Issue、working tree、index、branch、handoff 或獨占 scope。不得修改、build、
cleanup、reset、rebase 或 force-push 別人的 worktree／branch。

## 指派與開始

1. Human maintainer 的直接要求或 designated orchestrator 的指派才授權寫入；worker 不自行挑選
   backlog。Read-only audit 不需要 claim，也不得自行建立修復 Issue。
2. Orchestrator 只有在 writer 會立即開始時才建立完整 Issue，先 fresh-read active Issue／PR、
   `scope_globs` 與語意契約 ownership，確認不重疊。
3. 正式 claim 由 `.github/workflows/claim-admission.yml` 序列化：驗證完整 handoff、owner、branch、
   base、scope 與 overlap，原子保留 branch，完成全域 audit/readback 後才設定 assignee 與
   `claimed`。`claim-pending` 只可作為 workflow 的短暫內部狀態，不授予寫入權；失敗須 rollback。
4. Worker 從 handoff base／target 的 fresh remote HEAD 建立 branch 與必要的隔離 worktree，確認
   HEAD、dependency、乾淨狀態，再執行：

   ```powershell
   pwsh -File tools/handoff-check.ps1 -Issue <issue>
   pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource
   ```

5. Gate 通過後立即實作。首次可重建 push 後開 draft PR，並用 closing keyword 連回唯一 Issue。

若需確認本機 branch occupancy，只做 bounded 查詢：

```powershell
git worktree list --porcelain | rg -B2 -A1 --fixed-strings "branch refs/heads/<branch>"
```

接手時原 owner 必須先 commit、push、更新 handoff 並停寫；新 owner readback 遠端 HEAD、handoff 與
乾淨 worktree 後才修改。Worktree 實際路徑屬本機資訊，不寫入 repository。

## Scope 與整合 ownership

`scope_globs` 是獨占預告 write-set；`shared_paths` 只表示需要協調，不授予寫入權。範圍擴張前先
更新 handoff 並重新做 overlap admission。目錄 lane 只是路由提示；檔案 glob 與 public contract
語意 ownership 才是衝突依據。

下列全域快照只由 active integrator 修改：

- `docs/AI_HANDOFF.md`
- `docs/state/BASELINE.md`
- `docs/PROJECT_MAP.md`
- root `README.md`
- foundation integration Issue body

Workflow、dependency lock、distribution identity、root／subsystem `CMakeLists.txt`、聚合測試與 Spec index
是高衝突 shared paths，必須由 integrator 指定單一 writer。永久 ID、public IPC／schema、DSP 順序、
安全規則與 license boundary 即使檔案不重疊，也同時只能有一個 owner。

跨 lane 功能優先先合併最小 contract／schema／Spec；stacked PR 必須明列 `target_branch` 與
`depends_on`，上游不得 force-push。Feature writer 維護自己的 handoff、目標 Spec、tests 與匿名
evidence；integrator 合併時才更新全域快照。未合併 branch 的結果不得寫成 main 能力。

## 驗證、review 與 merge

- 每個寫入切片跑 `AGENTS.md` 的 always-run 與範圍相符的 conditional gates。Public contract 同步
  更新 Spec、tests 與 evidence；user-space/source evidence 不得冒充實體音訊或 driver/WaveRT 證據。
- Draft PR 未達 acceptance 就繼續修；達成後以目前 head SHA 重跑 required checks。舊 head、queued、
  in-progress 或 branch-only green 都不能當作 fresh exact-head green。
- Fresh exact-head green 且 review 接受後，將 Issue lifecycle 改為 `in-review`、PR 轉 ready；
  integrator 確認 scope、closing Issue linkage、one-PR-per-branch 與 mergeability 後合併。
- 合併後 readback target／`main` 與 Issue closed，記錄 merge SHA／evidence，移除 closed Issue 的
  `claimed`／`claim-pending`／`in-review` 與 assignee residue；只依安全 predicate 用
  `tools/clean-closed-issue-residue.ps1` 清理已完成 refs／worktrees。Merge 前不得回報完成。
- 若遇到具體 safety、permission、scope 或 external blocker，保留 draft，將事實、已完成驗證與
  唯一下一步寫回 handoff；一般待辦、尚未嘗試或等待例行 CI 不算 blocker。

## Integrator 順序

1. Fresh-read open PR 的 head、review、checks、draft 與 linked Issue 狀態。
2. 先處理已 acceptance、fresh exact-head green 且可安全合併的 PR，完成 ready、merge、main readback
   與 Issue closure。
3. 再解除可處理的 dependency、scope 或 review 阻擋。
4. 最後才發現新工作；只有 writer 立即開始時建立一張完整 execution Issue 並 admission。

## 衝突與停止條件

Issue／branch／scope 重複、跨出授權 scope、shared path owner 不明、accepted 文件互相矛盾，或
base／handoff／PR／remote HEAD 不一致時，立即停止重疊寫入。把 paths、owners、commits、觀察與
最小決定記錄到 active handoff；由 integrator 切 scope、指定 owner 或排序，不複製或覆蓋別人的 WIP。

`main` 應由 GitHub ruleset 強制 PR、required checks 與禁止 force-push／deletion；實際狀態以 GitHub
readback 為準，不以本檔宣稱已啟用。
