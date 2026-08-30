# Hibiki DSP：AI 即時交接路由

本頁只負責把 AI 導向目前任務的權威資料，不複製整合 changelog、產品 baseline 或子系統細節。
新視窗先讀 root `AGENTS.md` 與 `docs/START_HERE.md`；只有需要 repository-wide 狀態或整合判斷時
才查本頁。遇到衝突時依 `START_HERE.md` 的權威順序處理，不依聊天紀錄猜測。

## 先判斷交付模式

- Maintainer 要求修正／實作：直接推進到實作、匹配驗證與整合；不得改選唯讀 audit。
- Maintainer 明確要求 read-only audit：只檢查並在目前 task 回報；除非另有明確 backlog 規劃要求，
  不建立 Issue queue。
- Issue、branch、PR 與 CI 都是協作／稽核記錄，不是產品成果或停止點。

寫入任務開始時執行：

```powershell
git fetch --all --prune
git status --short --branch
gh issue view <issue> --json number,state,labels,assignees,updatedAt
pwsh -File tools/handoff-check.ps1 -Issue <issue>
pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource
```

工作樹不乾淨或 handoff check 失敗時，先釐清 ownership；需要 build／target evidence 才另跑
環境 probe。

## 上下文載入邊界

- 預設只載入 `AGENTS.md`、`START_HERE.md`、active Issue handoff 與其指定的 Spec／ADR。
- `context-pack.ps1 -NoSource` 是 Issue body 的唯一啟動輸出；它有 48,000 字元與 12,000
  conservative estimated-token 雙上限，不重播全域規則或快照。
- 查 main 能力時用關鍵字讀 [BASELINE](state/BASELINE.md) 的相關段落，不把整份檔案貼進 prompt：

  ```powershell
  rg -n -i "<capability|subsystem|issue>" docs/state/BASELINE.md docs/PROJECT_MAP.md
  ```

- 查最近整合只讀 bounded Git／PR 摘要：

  ```powershell
  git log -12 --oneline origin/main
  gh pr list --state merged --limit 12
  ```

- 只有明確 repository-wide 稽核才使用 context pack 的 `-IncludeRepositoryState`，並明確指定
  `-MaxCharacters` 與 `-MaxEstimatedTokens`。

## 多 AI 並行入口

- 完整協定在 `docs/ai/MULTI_AGENT.md`，但 worker 不必在每次啟動全文預載。寫入需要
  明確指派、完整 execution Issue、Issue assignee + lifecycle label、非 `main` branch 與獨占 scope。
- 除非 maintainer 明確要求規劃 backlog，不建立 candidate、TBD、pre-claim 或排隊 Issue。只有
  writer 會立即開始時才建立一張完整 Issue，並經序列化 admission 取得寫入權。
- 有並行 writer、branch occupancy 或不確定狀態時使用獨立 worktree。首次可重建的
  WIP/reviewable commit push 後立即開 draft PR，不需要空認領 commit。Draft 必須一路推進到
  acceptance、fresh exact-head green、ready 與 merge，或記錄具體 safety／permission／scope／external
  blocker；integrator 先 drain 可安全合併的 green PR。
- 修改前以 Issue handoff 的 `scope_globs`、`shared_paths`、`depends_on` 與語意契約 ownership
  判定衝突；重疊時停止，由 integrator 指定 owner。
- feature AI 只更新自己的 handoff、目標 Spec、tests 與 evidence；全域快照由 integrator 單寫。

## 依任務載入順序

1. Active Issue handoff：branch、scope、dependencies、Spec／ADR 與唯一 `Next safe action`。
2. `context-pack.ps1 -Issue <issue> -NoSource`：有界的 Issue、Spec／ADR 與 evidence 文件包。
3. 當前 slice 所需的 source、tests 與 evidence；需要 source pack 才移除 `-NoSource`。
4. main 能力、子系統路由或整合決策需要時，才查 BASELINE／PROJECT_MAP／MULTI_AGENT 的相關段落。

## 不可自行做的事

- 不重生 `config/distribution-profile.yml` 的 endpoint GUID、driver hardware ID、ASIO CLSID、IPC namespace。
- 不把 `.local/`、bin/obj、PE/COFF、簽章檔、金鑰、真實 endpoint/session ID 或私人校正檔加入 Git。
- 不宣稱 vendor ASIO、WASAPI Exclusive、RAW、Atmos/DTS:X 或未經使用者手勢的 Chrome tab capture
  已受 Hibiki 控制。
- 不把「控制命令已入列」或「預設已保存」寫成「已完成引擎／實體音訊套用」。
- 專案不需要 HLK 與任何簽章；不得把 driver 安裝／載入或簽章描述為驗收前置，
  日常 probe 也不可代替實際 driver/WaveRT evidence。

## 交接前最小完成條件

正常終點是 merge、target／`main` 與 Issue closed readback，以及依 `MULTI_AGENT.md` 完成安全 clean
close；交接只用於具體 blocker 或更換 writer。原 writer 更新 handoff 的 owner、HEAD、
已完成內容、驗證、限制與唯一下一步，建立可重建 WIP commit、push 並停寫；新 writer readback 後
才修改。Public contract 變更同步更新 Spec、tests 與 evidence，不把唯一狀態留在聊天或工作樹。
