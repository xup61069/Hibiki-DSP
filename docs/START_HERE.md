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
2. 執行 `git fetch --all --prune`，檢查 open Issue／PR 與其他 Issue 的 handoff block；選擇未被認領且
   `scope_globs` 不重疊的 Issue。Issue 0 只保留給 foundation integration，不再承載新 feature。
3. 每個 AI 在 repository 外建立自己的 clone/worktree 與 branch；禁止在另一個仍在工作的 AI
   所使用的 working tree 執行 `checkout`、`switch`、branch rename 或 rebase。
4. Orchestrator 在 GitHub Issue body 補齊 handoff block（owner、branch、base commit、
   `scope_globs`、shared paths、dependencies）、指派 assignee 並加上 lifecycle label
   （`claimed` 進行中／`in-review` 待審）；worker 從指派 base 的最新遠端 HEAD 建立隔離
   worktree 後即可開始。首次可審閱的 commit push 後就開 draft PR，不要建立空的認領 commit。
5. 確認 branch、HEAD、working tree 與 dependency lock，再執行
   `pwsh -File tools/handoff-check.ps1 -Issue <issue>`。有 scope 或文件衝突時停止寫入，交由
   integration coordinator 切分或排序。
6. 執行 `pwsh -File tools/doctor.ps1 -CheckOnly` 與 `pwsh -File tools/probe-environment.ps1`；
   環境資料只寫入 `.local/`。
7. 讀 handoff block 指定的 Spec、ADR、source、tests 與 evidence。
8. 先用最小 context pack 複製交接內容：
`pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource`。Foundation integration Issue 只是 whole-repository
   foundation 例外；其他工作不得用 foundation Issue 取代自己的 handoff block。再執行 handoff 的 baseline
   smoke test；結果不一致時先標記 stale/conflict。需要完整 source context 時移除
   `-NoSource`，不要把與該 Issue 無關的聊天內容帶入新工作階段。
9. 修改中定期把可建置的 WIP commit push 到自己的 branch，並同步 handoff block 的已完成內容、
   限制與下一個安全動作；不得靠未 push 的工作樹或聊天紀錄交接。
10. 修改後執行 AGENTS.md 第三層的核心 gates（verify／handoff-check／docs-check／
   source-policy／source-only-ci-check），再依觸發條件加跑條件式 gates：UI 用
   build-preview 與 winui-shell-check，engine 整合用 engine preview 系列，extensions 用
   extension-check，installer/distribution identity 用 installer-check 與
   distribution-check，driver boundary 用 driver-source-check（driver 簽章屬 release 階段）。
   live-* probes 一律是明確 opt-in：預設只輸出匿名資料且不改變機器狀態；帶 `-WriteTest`
   的變體會短暫改變本機音量並在結束前恢復。任何 live probe 的結果都只能算 user-space
   evidence，不得宣稱 driver/WaveRT/HLK/Microsoft signing 或實體音訊 delivery 已完成；
   各 probe 的詳細邊界說明見各工具腳本開頭註解。

## 文件權威順序

產品行為看 accepted Spec；架構理由看 accepted ADR；實際完成狀態看 source、
tests 與 evidence；main 的合併狀態看 `docs/state/BASELINE.md`；分支工作看
Issue body handoff block。兩份權威文件衝突時停止修改，建立 `DOC-CONFLICT`，不要猜。

## 換 AI／換電腦

只更新自己 Issue body 的 handoff block，記錄 owner、scope、base commit、環境 fingerprint、已完成內容、失敗測試、
剩餘工作、`Next safe action` 與 resume commands。建立 WIP commit 並 push
branch，確認前一個 AI 停止寫入後才把 owner 交給下一個 AI。真實裝置資料與 calibration
留在 `.local/`，只提交 schema 和匿名 fixture。

## 尚未完成的主要區域

- `driver/`：固定四端點的 SYSVAD-derived WaveRT/KS 虛擬端點與 Windows volume nodes；
  endpoint topology/channel mask catalog 已有 portable MS-PL contract，PortCls wiring 與簽章仍待完成。
- `src/`：即時 graph、Matrix、per-output-group plugin latency compensation、跨 block lane delay
  bank、bounded multi-sink fan-out、ISO fit、program-aware RMS/K-weighted proxy、scene safety、
  device switch、ASIO/外部 Lane block API、WASAPI Float32 output boundary 與 bounded per-App
  session route rules、per-output-group volume bank 與 bounded custom Scene catalog；bounded
  calibration PEQ compiler 已加入，Windows process-loopback 亦已有官方
  `ActivateAudioInterfaceAsync` 的 worker-owned Float32 source boundary；真實端點、含音訊
  程序與 Audio Service restart soak 仍待目標環境。
- `apps/`：已有 UI-independent control model 與 source-only WinUI 3 Easy/Expert shell；
  現已包含有效音量／安全上限／來源／致動器投影、ControlStatusSnapshot 原子套用，
  以及 session、process loopback、瀏覽器單分頁、direct bypass 的保守路由健康卡片；
  仍需在鎖定 Windows App SDK 的目標環境編譯、做視覺／無障礙驗證，並把保守狀態
  逐步替換成已驗證的實體端點／引擎 delivery 狀態。
- 目前最新 accepted control-plane 契約為 `SPEC-0024`：Expert per-App 路由預設的 bounded
 catalog、原子保存與 ViewModel 命令邊界；它建立在 `SPEC-0023` 的固定 480-byte
 Upsert/Remove/Clear wire 與候選交易上。接手者先讀兩份 Spec、`docs/state/BASELINE.md` 與
 `evidence/0000-foundation/expert-route-rule-presets-v1.json`。
- `asio/`：預設為 stream model；需要本機 pinned ASIO SDK 時可開啟 optional native COM
  transport（不進 public CI，也不提交 DLL）。`vst-host/` 已有 supervisor、frame codec、
  named-pipe boundary、source-only passthrough worker、可選的 pinned VST3 SDK factory catalog
  與單一主 bus worker-side SDK processor/optional worker executable；bounded parameter timeline、
  latency graph commit、RT compensation、private identity/version-checked state、explicit
   migration handler/registry、Scene binding、EngineControl preflight gate 與第三方 state review
   checklist 已接入；supervisor 端 headless timeline 編輯交易、canonical JSON 持久化、
   per-timeline 檔案儲存、store→scheduler 同步與排程器內省亦已完成（Issues #14/#22/#35/
   #63/#72/#78）；supervisor UI 編輯介面、side-chain/multi-bus worker process 與
   certification 仍待完成。
  `extensions/` 已有 HIBT decoder、loopback bridge、bounded capture queue 與
  graph-lane adapter；Virtual Mic 有 bounded normalized-LMS/gate baseline，但正式 AEC/NS model
  provenance 與 signed capture driver 仍待完成。process-loopback 是 process-level source，
  不能取代 Chrome 單分頁 MV3 tabCapture 或實體 per-App 重送。
