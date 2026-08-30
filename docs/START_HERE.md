# Hibiki DSP：AI 接手入口

本 repository 才是 Hibiki 的長期記憶。不要依賴上一個 AI、上一台電腦或
未提交的聊天內容。

## Fresh clone 流程

所有 `tools/*.ps1` gates 必須用 PowerShell 7（`pwsh`）執行：腳本為 UTF-8（無 BOM）
並使用 .NET Core API，Windows 內建的 PowerShell 5.1 會把中文解成亂碼且缺少必要
方法，直接跑必失敗。缺少 `pwsh` 時記錄具體 permission／external blocker；未經 maintainer
明確授權不得安裝系統套件。
本機被 Execution Policy 擋下時加 `-ExecutionPolicy Bypass`。多個 gate 另提供
`-SelfTest`（例：`tools/docs-check.ps1 -SelfTest`），可在不碰機器、不寫檔的前提下
驗證 gate 自身邏輯。

1. 只先讀 root `AGENTS.md` 與本檔；Spec、ADR、source、tests、evidence 與全域快照按任務需要載入。
2. 先判斷要求：修正／實作要求必須直接推進到實作、匹配驗證與整合；只有 maintainer 明確要求
   read-only audit 時才只報告。Issue、branch、PR 與 CI 是記錄，不是成果或停止點。
3. 執行 `git fetch --all --prune` 與 `pwsh -File tools/delivery-audit.ps1 -BeforeExplore`；先 drain gate
   指出的既有可交付工作，再取 bounded Issue／PR metadata。除非 maintainer 明確要求規劃
   backlog，不得建立 candidate、TBD、pre-claim 或排隊 Issue。只有 writer 會立即開始時，才建立
   一張 objective、acceptance、owner、branch、base、scope、dependencies 與 verification 都完整的
   execution Issue，再經序列化 claim admission 取得 `claimed`；`claim-pending` 不授予寫入權。
4. 寫入使用非 `main` branch。確認 handoff、HEAD、dependency 與獨占 `scope_globs`，再跑
   `pwsh -File tools/handoff-check.ps1 -Issue <issue>`。有其他 writer、branch occupancy 或不確定狀態
   時使用獨立 worktree；不得操作別人的 worktree 或 branch。
5. 用 `pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource` 載入一次有界任務包，再按 scope
   讀取所需 source／tests／evidence 並實作。只有明確 repository-wide audit 才使用
   `-IncludeRepositoryState` 並同時指定兩種上限；需要 build／target evidence 才跑環境 probe，
   私人資料只寫入 `.local/`。
6. 首次可重建的 WIP/reviewable push 後開一張 draft PR，不做空認領 commit。持續推進到 acceptance、
   fresh exact-head checks 全綠、ready 與 merge；只有具體 safety、permission、scope 或 external
   blocker 才暫停並寫回 handoff。Integrator 先 drain 可安全合併的 green PR，再安排新工作；合併後
   readback target／`main` 與 Issue closed，並依 `MULTI_AGENT.md` 做安全 clean close。
7. 每個寫入切片跑 `AGENTS.md` 的 always-run（含 delivery audit）與範圍相符的 conditional gates。live probe 永遠需
   明確 opt-in；結果只算 user-space evidence，不得冒充 driver/WaveRT、實體音訊或 release 證據。

## Driver 建置邊界

- source、DSP、UI、driver source contract 與一般 CI 不需要任何簽章、Hardware Developer 帳號
  或付費憑證，也不應把這些項目當作開始開發或完成產品的前置。
- 本機或隔離 VM 的 driver 開發可用 WDK build 與 Inf2Cat；安裝或載入 driver 都會改變機器，
  只能由使用者明確同意。專案不以 HLK／WHCP 或 test-signing 作為目標。
- 未簽章 kernel driver 可能被 Secure Boot/HVCI 環境拒絕載入；這是使用者環境的平台限制，
  不是專案的待辦 signing 工作。

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
先講產品變化、使用者影響、確認方式與剩餘缺口，Git 操作只留必要的短版紀錄。三種主要視窗角色、
可直接貼上的 `/goal` 啟動詞與長任務停止條件見 [Codex Goals](ai/CODEX_GOALS.md)。
