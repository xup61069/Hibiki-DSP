# Hibiki DSP：AI 接手入口

本 repository 才是 Hibiki 的長期記憶。不要依賴上一個 AI、上一台電腦或
未提交的聊天內容。

## Fresh clone 流程

所有 `tools/*.ps1` gates 必須用 PowerShell 7（`pwsh`）執行：腳本為 UTF-8（無 BOM）
並使用 .NET Core API，Windows 內建的 PowerShell 5.1 會把中文解成亂碼且缺少必要
方法，直接跑必失敗。沒有 `pwsh` 先 `winget install --id Microsoft.PowerShell`；
本機被 Execution Policy 擋下時加 `-ExecutionPolicy Bypass`。多個 gate 另提供
`-SelfTest`（例：`tools/docs-check.ps1 -SelfTest`），可在不碰機器、不寫檔的前提下
驗證 gate 自身邏輯。

1. 讀 root `AGENTS.md`、本檔、`docs/AI_HANDOFF.md`、`docs/ai/MULTI_AGENT.md` 與
   `docs/PROJECT_MAP.md`。
2. 執行 `git fetch --all --prune`，檢查 open Issue／PR、handoff block、遠端 branches 與本機
   worktrees。唯讀偵察可繼續；寫入必須有 maintainer／orchestrator 明確指派，不得自行挑選
   backlog。人類 maintainer 對目前 session 的直接要求算指派，active orchestrator 可在確認
   `scope_globs` 不重疊後建立或正式 claim Issue。
3. 寫入一律使用非 `main` branch。有其他 writer、branch 已被 worktree 佔用或 occupancy 不確定
   時，在 repository 外建立獨立 clone/worktree；確認只有單一 writer 時仍建議隔離。禁止在別的
   session 工作樹執行 `checkout`、`switch`、branch rename、reset、clean 或 rebase。
4. Orchestrator（人類或被指定的 active session）在 GitHub Issue body 補齊 handoff block（owner、branch、base commit、
   `scope_globs`、shared paths、dependencies）、指派 assignee 並加上 lifecycle label
   （`claimed` 進行中／`in-review` 待審）；worker 從指派 base 的最新遠端 HEAD 建立 branch，
   並依第 3 步決定 workspace isolation 後即可開始。首次可審閱的 commit push 後就開 draft PR，
   不要建立空的認領 commit。
5. 確認 branch、HEAD、working tree 與 dependency lock，再執行
   `pwsh -File tools/handoff-check.ps1 -Issue <issue>`。有 scope 或文件衝突時停止寫入，交由
   integration coordinator 切分或排序。
6. 需要 build、toolchain 或 target-machine evidence 時才執行
   `pwsh -File tools/doctor.ps1 -CheckOnly` 與 `pwsh -File tools/probe-environment.ps1`；
   文件／流程小改不必為了形式探測機器，任何環境資料只寫入 `.local/`。
7. 讀 handoff block 指定的 Spec、ADR、source、tests 與 evidence。
8. 先用最小 context pack 複製交接內容：
`pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource`。Foundation integration Issue 只是 whole-repository
   foundation 例外；其他工作不得用 foundation Issue 取代自己的 handoff block。再執行 handoff 的 baseline
   smoke test；結果不一致時先標記 stale/conflict。需要完整 source context 時移除
   `-NoSource`，不要把與該 Issue 無關的聊天內容帶入新工作階段。
9. 修改中定期把可建置的 WIP commit push 到自己的 branch，並同步 handoff block 的已完成內容、
   限制與下一個安全動作；不得靠未 push 的工作樹或聊天紀錄交接。
10. 修改後執行 AGENTS.md 第三層的核心檢查（scoped handoff-check、docs-check、source-policy、
   `git diff --check`），再依觸發條件加跑條件式 gates：需要 build evidence 才跑 doctor；
   C/C++、CMake、schema、contract 或 tests 跑 verify；workflow/release policy 跑
   source-only-ci-check；UI、engine、extensions、installer、distribution 與 driver 各跑表列 gate。
   live-* probes 一律是明確 opt-in：預設只輸出匿名資料且不改變機器狀態；帶 `-WriteTest`
   的變體會短暫改變本機音量並在結束前恢復。任何 live probe 的結果都只能算 user-space
   evidence，不得宣稱 driver/WaveRT/HLK/Microsoft signing 或實體音訊 delivery 已完成；
   各 probe 的詳細邊界說明見各工具腳本開頭註解。

## Driver 簽章分階段

- source、DSP、UI、driver source contract 與一般 CI 不需要 Microsoft Hardware Developer
  帳號，也不應把帳號或正式憑證當作開始開發的前置。
- 本機或隔離 VM 的 driver 開發可用 WDK build、Inf2Cat 與 self-signed test-signing；啟用
  TESTSIGNING、匯入憑證或安裝 driver 都會改變機器，只能由使用者明確同意。詳見
  [Microsoft test-signing 指南](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/test-signing)。
- 一般使用者環境的 kernel driver 發布才需要 Microsoft 簽章與 Hardware Program；正式路線依
  release Spec 執行 HLK／WHCP、Secure Boot／HVCI 與 installer 驗收。官方要求見
  [driver signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing) 與
  [Hardware Program registration](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/hardware-program-register)。

## 文件權威順序

產品行為看 accepted Spec；架構理由看 accepted ADR；實際完成狀態看 source、
tests 與 evidence；main 的合併狀態看 `docs/state/BASELINE.md`；分支工作看
Issue body handoff block。兩份權威文件衝突時停止修改，建立 `DOC-CONFLICT`，不要猜。

## 換 AI／換電腦

只更新自己 Issue body 的 handoff block，記錄 owner、scope、base commit、環境 fingerprint、已完成內容、失敗測試、
剩餘工作、`Next safe action` 與 resume commands。建立 WIP commit 並 push
branch，確認前一個 AI 停止寫入後才把 owner 交給下一個 AI。真實裝置資料與 calibration
留在 `.local/`，只提交 schema 和匿名 fixture。

## 目前能力與缺口去哪裡看

- main 已合併、可重跑的能力與限制：`docs/state/BASELINE.md`。
- 子系統位置與目前 contract：`docs/PROJECT_MAP.md`。
- 分支的唯一下一步與 write scope：對應 Issue body handoff block。
- 實際執行過的命令、環境與 limitation：`evidence/`；本入口不再複製會快速過期的功能清單。

## 開 Codex 視窗與看進度

建議一個視窗只負責一個可驗證成果；不同視窗並行時不得共用 write scope。給 maintainer 的回報
先講產品變化、使用者影響、確認方式與剩餘缺口，Git 操作只留必要的短版紀錄。五種主要視窗角色、
可直接貼上的 `/goal` 啟動詞與長任務停止條件見 [Codex Goals](ai/CODEX_GOALS.md)。
