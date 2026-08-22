# Hibiki DSP AI 工作規則

## 先讀什麼

每次開始工作先讀本檔、`docs/START_HERE.md` 與 `docs/AI_HANDOFF.md`，再讀 GitHub Issue body 內的
handoff block、Spec、ADR、source 與 tests。聊天紀錄、AI memory、個人 IDE 規則
都不是專案真值。

## 執行環境（先確認）

- 所有 `tools/*.ps1` gates 以 UTF-8（無 BOM）儲存並使用 .NET Core API；必須用
  PowerShell 7（`pwsh`）執行。Windows 內建的 PowerShell 5.1 會把中文註解解成亂碼、
  缺少 `System.IO.Path`/.NET Core 方法，直接跑必失敗。
- 機器沒有 `pwsh` 時先安裝：`winget install --id Microsoft.PowerShell`。本機第一次
  執行若被 Execution Policy 擋下，加 `-ExecutionPolicy Bypass`。
- 多個 gate 提供 `-SelfTest`：不碰機器、不寫檔即可驗證 gate 自身邏輯（例：
  `pwsh -File tools/docs-check.ps1 -SelfTest`）。CI 的 `Gate self-test sweep` step 會
  探索並逐一執行所有暴露此參數的 gate；低於最低模式數門檻即失敗。

## 多 AI 並行（必遵守）

- 完整協定見 `docs/ai/MULTI_AGENT.md`。每個 AI 工作切片必須各自使用一個 GitHub Issue、
  一個獨立 clone/worktree、一個 branch、Issue body 內的 handoff block 與一個 draft PR。
- 工作由單一 orchestrator 指派；workers 不得自行認領 open Issue。GitHub Issue assignee／
  lifecycle label／linked PR 是即時指派真值；Issue body 的 handoff block 是可重建的分支交接真值。
  沒有指派、handoff block 或明確 write scope 時只能唯讀偵察，不得開始修改。
- 接手前必須以 handoff block 確認 owner，並檢查 `git ls-remote --heads origin` 上的
  branch 佔位與本機 `git worktree list`（先有 branch 不代表沒有未 push 的 edits）。
  新工作切片由 orchestrator 建 Issue 取得號碼後命名 branch；不要預估號碼。
- 同一台機器可能同時跑多個 AI session（同一 Git 身分）。身分相同不等於有權進入別的
  session 的 worktree 或 branch；worktree 隔離是絕對的。回到先前中斷的 slice 時，先
  fetch 並以遠端 HEAD 與 Issue body 的 handoff block 為真值重新確認，不得假設本機工作樹仍是最新；
  發現工作被另一 session 接手完成時，接受遠端 HEAD、獨立重驗，並在 PR body 誠實記錄
  接手事件（先例：PR #24）。
- 禁止兩個仍在執行的 AI 共用 working tree、index、branch 或同一 Issue。需要並行時拆 child Issue；
  交接同一 branch 時必須先由前一個 AI commit、push、停止寫入並更新 owner。
- Issue handoff block 的 `scope_globs` 是該工作切片的獨占 write scope。開始前必須檢查 open Issue、draft PR
  與其他 directory lane；scope 重疊、跨 lane 或會碰共享整合檔時，先由 orchestrator
  指定 owner 與合併順序，不得自行同時修改。
- 會新增/刪除 tracked 檔案的切片，在同一 slice 內執行
  `pwsh -File tools/docs-check.ps1 -WriteCounters` 重生 `build/baseline-counters.json`，
  隨實作一起 commit（#137 起揮發性計數真值在該 JSON：檔案缺失、JSON 畸形、schema drift
  或數值不符皆 fail-closed）。#183 起 PR 對 counters JSON 的編輯一律對照 head 實測嚴格
  驗證，僅 merge base 缺檔的首次引入容忍；未動 `docs/state/BASELINE.md` 與 counters JSON
  的 handoff-only head 在 PR CI 容忍漂移，push/local 保持 strict。
  數字以 `tools/docs-check.ps1` 的錯誤訊息為準。
- `docs/AI_HANDOFF.md`、`docs/state/BASELINE.md`、`docs/PROJECT_MAP.md`、root `README.md` 與
  foundation integration Issue 是整合快照，由 integrator 單寫。feature AI 更新自己的 handoff、
  Spec、tests 與 evidence，不在未合併分支宣稱全域完成狀態。
- 不直接 push `main`，不 force-push 或改寫已發布／被依賴的 branch。每個 PR 只處理一個 Issue；
  跨 lane 先合併 contract/schema，再讓相依工作從新 base 開始或明列 stacked dependency。

## 專案硬限制

- 目標：Windows 11 24H2+ x64；C++20 即時核心、C# WinUI 3 UI。
- RT audio thread 不配置、不取得 mutex、不等待、不呼叫 COM/UI/檔案系統。
- driver 與 GPL user-space 只能透過版本化 IPC；不得把 MS-PL driver 靜態或動態連入 GPL engine。
- 廠商 ASIO、WASAPI Exclusive、RAW 路徑不可宣稱受 Hibiki 控制。
- 不提交 EXE、DLL、SYS、MSI、MSIX、VST3、PE/COFF、簽章憑證或私密金鑰。
- 真實裝置 ID、校正檔、序號、私人路徑放 `.local/`，不得進 Git。
- 不反編譯或繞過閉源軟體保護；只用開源程式、官方文件與只有合法 black-box 觀察。
- ISO 226 授權文件、掃描、完整表格與受限資料不可放入 repo、Issue、prompt 或 RAG。

## 真值與文件契約

- 產品行為看 accepted Spec；架構取捨看 accepted ADR；已完成能力看 source、tests、evidence 與 `docs/state/BASELINE.md`。
- 修改 public API、schema、DSP 需序、安全規則或建置方式時，同一 PR 必須更新對應文件與 evidence。
- Accepted ADR 不可改寫；新決策建立新 ADR 並標示 supersedes。
- 每個 Issue 在 body 中維護 `<!-- hibiki:handoff-v1 -->` handoff block；換 AI 或電腦前建立 WIP commit、push branch、更新下一個安全動作。

## 必跑命令

```powershell
pwsh -File tools/doctor.ps1 -CheckOnly
pwsh -File tools/handoff-check.ps1
pwsh -File tools/build-preview.ps1 -Target DesktopCompat
pwsh -File tools/probe-environment.ps1
pwsh -File tools/verify.ps1
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
pwsh -File tools/source-only-ci-check.ps1
pwsh -File tools/extension-check.ps1
pwsh -File tools/installer-check.ps1
pwsh -File tools/control-model-check.ps1
pwsh -File tools/build-engine-preview.ps1
pwsh -File tools/engine-preview-smoke.ps1
pwsh -File tools/control-model-engine-smoke.ps1
pwsh -File tools/winui-shell-check.ps1
pwsh -File tools/distribution-check.ps1
pwsh -File tools/driver-source-check.ps1
pwsh -File tools/driver-signability-check.ps1
```

在鎖定的 Windows 11 24H2+/VS 2026/SDK-WDK 機器，將 Compatibility Preview 改為
`pwsh -File tools/build-preview.ps1 -Target WinUI`，以取得正式 XAML build evidence；
Desktop Compatibility Preview 只能驗證本機 ViewModel/啟動 smoke，不得代替 XAML、無障礙、driver 或發行驗收。

Windows endpoint enumeration 是額外的 opt-in live check：
`pwsh -File tools/live-device-catalog-check.ps1`。它只產生 `.local/` 暫存輸出；不得把
真實 endpoint ID、friendly name、簽章檔或任何編譯產物提交到 Git。

Shared-mode WASAPI handoff 也是額外的 opt-in live check：
`pwsh -File tools/live-wasapi-handoff-check.ps1`。它只送靜音 block，輸出 mix format
與 aggregate worker counters；沒有可用 endpoint 時只能記錄 `wasapi=unavailable`，不能
把 user-space probe 當成已完成的 WaveRT／HLK／Microsoft signing 驗收。

Windows 系統音量的真實讀回／寫入／恢復也是額外的 opt-in live check：
`pwsh -File tools/live-system-volume-check.ps1 -WriteTest`。預設會啟動 Engine Preview，從
named pipe 送出音量命令，暫時衰減約 3 dB，直接讀回 Windows endpoint 並恢復原值；它不輸出
endpoint identity，但會改變本機音量，無法把這個 user-space write-through probe 當成 driver、
WaveRT 或 HLK evidence。只有除錯 broker 時才加 `-DirectBroker`。

Windows 單一 App/session 音量的真實讀回／寫入／恢復也是額外的 opt-in live check：
`pwsh -File tools/live-session-volume-check.ps1 -WriteTest`。預設會啟動 Engine Preview，建立本
probe 的無聲 shared-mode session，以 generation-scoped catalog handle 經 IPC/control queue/COM
worker 暫時衰減約 3 dB、讀回並恢復原值；不輸出 session/endpoint identity，也不等於實體
per-App capture、重送或 DSP delivery evidence。只有除錯 coordinator 時才加 `-DirectCoordinator`。

Windows App/session enumeration 也有 opt-in check：
`pwsh -File tools/live-audio-session-check.ps1`。只輸出 session／active 數量與固定 identity
語意，不輸出 PID、session ID、endpoint ID 或顯示名稱；它不等於每個 App 已完成實際
Lane routing 或 DSP delivery。
Process-level loopback source 也有 opt-in check：
`pwsh -File tools/live-process-loopback-check.ps1`。它只輸出匿名格式與 frame aggregate；
若本機 Audio Service 不提供 process-loopback，`loopback=unavailable` 仍只能算 source
compile evidence，不得當成 Chrome tabCapture、實體 per-App routing 或 signed driver evidence。

遇到環境差異先記錄 fingerprint 並更新 handoff，不要自行重生
`config/distribution-profile.yml` 裡的 endpoint、ASIO、IPC GUID。
