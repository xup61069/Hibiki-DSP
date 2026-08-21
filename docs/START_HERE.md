# Hibiki DSP：AI 接手入口

本 repository 才是 Hibiki 的長期記憶。不要依賴上一個 AI、上一台電腦或
未提交的聊天內容。

## Fresh clone 流程

1. 讀 root `AGENTS.md`、本檔、`docs/AI_HANDOFF.md` 與 `docs/PROJECT_MAP.md`。
2. 確認 branch、HEAD、working tree 與 dependency lock。
3. 執行 `pwsh -File tools/doctor.ps1 -CheckOnly` 與 `pwsh -File tools/handoff-check.ps1`。
4. 執行 `pwsh -File tools/probe-environment.ps1`，環境資料只寫入 `.local/`。
5. 找到要處理的 GitHub Issue，讀 `docs/tasks/active/<issue>.md`。
6. 讀 handoff 指定的 Spec、ADR、source、tests 與 evidence。
7. 先用最小 context pack 複製交接內容（目前 foundation Issue 0：
   `pwsh -File tools/context-pack.ps1 -Issue 0 -NoSource`），再執行 handoff 的 baseline
   smoke test；結果不一致時先標記 stale/conflict。需要完整 source context 時移除
   `-NoSource`，不要把與該 Issue 無關的聊天內容帶入新工作階段。
8. 修改後執行 `tools/verify.ps1`、`tools/handoff-check.ps1`、`tools/docs-check.ps1` 與
   `tools/source-policy.ps1`、`tools/source-only-ci-check.ps1`；若改動 extension、installer 或 control model，再執行
   `tools/extension-check.ps1`、`tools/installer-check.ps1`、`tools/control-model-check.ps1`、
   `tools/winui-shell-check.ps1`。
   Windows 主機若要驗證 worker-owned endpoint enumeration，可額外執行
   `pwsh -File tools/live-device-catalog-check.ps1`；它是 opt-in，只輸出數量、sequence、
   payload 大小與 wire 結果，不會把真實 endpoint ID 寫入 repository。
   若要驗證實際 shared-mode sink 與 30 ms 無聲 handoff，可額外執行
   `pwsh -File tools/live-wasapi-handoff-check.ps1`；它只輸出 mix format 與 aggregate
   worker counters，沒有可用 endpoint 時會回報 `wasapi=unavailable`。
   若要驗證 Windows endpoint volume 的實際讀回、短暫衰減與恢復，可額外執行
   `pwsh -File tools/live-system-volume-check.ps1 -WriteTest`；只有明確旗標才會改變本機音量，
   probe 會啟動 Engine Preview 並經 named pipe 驗證 write-through，結束前恢復原值，仍不等於
   driver/WaveRT/HLK evidence。`-DirectBroker` 只供隔離 broker 除錯。
   若要驗證單一 App/session volume 的實際 handle 讀回、短暫衰減與恢復，可額外執行
   `pwsh -File tools/live-session-volume-check.ps1 -WriteTest`；它只建立本 probe 的無聲
   shared-mode session，結束前恢復原值，仍不等於實體 per-App capture/re-send 或 DSP delivery。
   同一 probe 也會驗證 route command 與 route-rule Upsert/Remove 的 bounded catalog 狀態變化；
   這是 user-space graph transaction evidence，不是實體 per-App delivery。
   若要驗證 process-level loopback source，可額外執行
   `pwsh -File tools/live-process-loopback-check.ps1`；它只輸出匿名格式與 frame aggregate，
   沒有可用 runtime 時會回報 `loopback=unavailable`，不等於 tabCapture 或實體 per-App routing。
   任何 identity/config 變更都必須再執行 `tools/distribution-check.ps1`；改動 driver source
   boundary 時也執行 `tools/driver-source-check.ps1` 與
   `tools/driver-signability-check.ps1`。若有目標 WDK 編出的 package，可用
   `tools/driver-signability-check.ps1 -PackageRoot <package> -RequireInf2Cat` 產生
   Inf2Cat signability evidence；fresh clone 沒有 SYS 時，預設命令只驗證 source boundary，
   不會假裝完成 `.sys`／CAT 或 Microsoft signing。

## 文件權威順序

產品行為看 accepted Spec；架構理由看 accepted ADR；實際完成狀態看 source、
tests 與 evidence；main 的合併狀態看 `docs/state/BASELINE.md`；分支工作看
Issue 與 handoff。兩份權威文件衝突時停止修改，建立 `DOC-CONFLICT`，不要猜。

## 換 AI／換電腦

更新 active handoff，記錄 base commit、環境 fingerprint、已完成內容、失敗測試、
剩餘工作、`Next safe action` 與最多五個 resume commands。建立 WIP commit 並 push
branch。真實裝置資料與 calibration 留在 `.local/`，只提交 schema 和匿名 fixture。

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
  checklist 已接入；supervisor UI timeline 編輯、side-chain/multi-bus 與 certification 仍待完成。
  `extensions/` 已有 HIBT decoder、loopback bridge、bounded capture queue 與
  graph-lane adapter；Virtual Mic 有 bounded normalized-LMS/gate baseline，但正式 AEC/NS model
  provenance 與 signed capture driver 仍待完成。process-loopback 是 process-level source，
  不能取代 Chrome 單分頁 MV3 tabCapture 或實體 per-App 重送。
