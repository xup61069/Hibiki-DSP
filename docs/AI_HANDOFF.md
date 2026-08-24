# Hibiki DSP：AI 即時交接路由

本頁只負責把 AI 導向目前任務的權威資料，不複製整合 changelog、產品 baseline 或子系統細節。
新視窗先讀 root `AGENTS.md` 與 `docs/START_HERE.md`；只有需要 repository-wide 狀態或整合判斷時
才查本頁。遇到衝突時依 `START_HERE.md` 的權威順序處理，不依聊天紀錄猜測。

## 先做這五件事

```powershell
git fetch --all --prune
git status --short --branch
gh issue view <issue> --json number,state,labels,assignees,updatedAt
pwsh -File tools/handoff-check.ps1 -Issue <issue>
pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource
```

工作樹不乾淨或 handoff check 失敗時，先在 Issue body 記錄事實；不要直接改 DSP、driver、
永久 ID 或 release 設定。需要 build／target evidence 的切片才另跑 `doctor.ps1 -CheckOnly` 與
`probe-environment.ps1`。

## 上下文載入邊界

- 預設只載入 `AGENTS.md`、`START_HERE.md`、active Issue handoff 與其指定的 Spec／ADR。
- `context-pack.ps1 -NoSource` 是 Issue body 的唯一啟動輸出；它有 48,000 字元與 12,000
  conservative estimated-token 雙上限，不重播全域規則或快照。
- 查 main 能力時用關鍵字讀 [BASELINE](state/BASELINE.md) 的相關段落，不把整份檔案貼進 prompt：

  ```powershell
  rg -n -i "<capability|subsystem|issue>" docs/state/BASELINE.md docs/PROJECT_MAP.md
  ```

- 查最近整合只讀 bounded Git／PR 摘要，不在本頁累積「第幾波」歷史：

  ```powershell
  git log -12 --oneline origin/main
  gh pr list --state merged --limit 12
  ```

- 只有明確 repository-wide 稽核才使用 context pack 的 `-IncludeRepositoryState`，並明確指定
  `-MaxCharacters` 與 `-MaxEstimatedTokens`。同一視窗第二次壓縮或切換里程碑時先 checkpoint，
  再開新視窗接續。

## 多 AI 並行入口

- 完整協定在 `docs/ai/MULTI_AGENT.md`，但 worker 不必在每次啟動全文預載。寫入需要
  maintainer／orchestrator 明確指派、Issue assignee + lifecycle label、非 `main` branch 與
  handoff scope；人類 maintainer 的直接要求算指派，但仍須 materialize Issue 並檢查 overlap。
- 有並行 writer、branch occupancy 或不確定狀態時使用獨立 worktree。首次可重建的
  WIP/reviewable commit push 後立即開 draft PR，不需要空認領 commit。
- worktree 很多時只查目標 branch，不輸出完整 inventory：
  `git worktree list --porcelain | rg -B2 -A1 --fixed-strings "branch refs/heads/<branch>"`。
- 修改前以 Issue handoff 的 `scope_globs`、`shared_paths`、`depends_on` 與語意契約 ownership
  判定衝突；重疊時停止，由 integrator 指定 owner。
- feature AI 只更新自己的 handoff、目標 Spec、tests 與 evidence；全域快照由 integrator 單寫。

## 現在去哪裡看

- 分支的唯一下一步、write scope、依賴與 owner：對應 Issue body handoff block。
- main 已合併、可重跑的能力與限制：[BASELINE](state/BASELINE.md) 的相關段落。
- 子系統位置與 contract 路由：[PROJECT_MAP](PROJECT_MAP.md)。
- 實際命令、環境與 limitation：對應 Issue 的 `evidence/` JSON。
- 整合先後與歷史變更：`git log -12 --oneline origin/main`、bounded merged PR 與各檔案 Git history。

## 依任務載入順序

1. Active Issue handoff：branch、scope、dependencies、Spec／ADR 與唯一 `Next safe action`。
2. `context-pack.ps1 -Issue <issue> -NoSource`：有界的 Issue、Spec／ADR 與 evidence 文件包。
3. 當前 slice 所需的 source、tests 與 evidence；需要 source pack 才移除 `-NoSource`。
4. 只有決策需要時才查 BASELINE／PROJECT_MAP／MULTI_AGENT 的相關段落，不全文預載。

## 不可自行做的事

- 不重生 `config/distribution-profile.yml` 的 endpoint GUID、driver hardware ID、ASIO CLSID、IPC namespace。
- 不把 `.local/`、bin/obj、PE/COFF、簽章檔、金鑰、真實 endpoint/session ID 或私人校正檔加入 Git。
- 不宣稱 vendor ASIO、WASAPI Exclusive、RAW、Atmos/DTS:X 或未經使用者手勢的 Chrome tab capture
  已受 Hibiki 控制。
- 不把「控制命令已入列」或「預設已保存」寫成「已完成引擎／實體音訊套用」。
- 專案不需要 HLK 與任何簽章；不得把 driver 安裝／載入或簽章描述為驗收前置，
  日常 probe 也不可代替實際 driver/WaveRT evidence。

## 交接前最小完成條件

換 AI 或電腦前，只更新自己 Issue body handoff block 的 owner、base commit、已完成內容、驗證、
限制與唯一下一步；建立 WIP commit、push 自己的 branch，並跑與 scope 相符的 gate。Public contract
變更同步更新 Spec、tests 與 evidence。不要把唯一可用狀態留在本頁、聊天或未 push 工作樹。
