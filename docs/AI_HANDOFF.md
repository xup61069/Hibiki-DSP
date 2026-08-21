# Hibiki DSP：下一個 AI 的 90 秒入口

這一頁是交接摘要，不取代 Spec、ADR、source 或 evidence。遇到衝突時，以
`docs/START_HERE.md` 所列權威順序處理，不要依聊天紀錄猜測。

## 先做這四件事

```powershell
git status --short
pwsh -File tools/doctor.ps1 -CheckOnly
pwsh -File tools/handoff-check.ps1
pwsh -File tools/context-pack.ps1 -Issue 0 -NoSource
```

工作樹不是乾淨狀態、handoff check 失敗，或 target toolchain 不符合時，先在
`docs/tasks/active/0.md` 記錄事實；不要直接改 DSP、driver、永久 ID 或 release 設定。

## 現在的真實位置

- `main` 已有可重跑的 C++ user-space contract baseline、C# control model、source-only WinUI
  shell 與 public source policy gates。
- Expert per-App route preset 已保存、會對選取 App 做規則預覽，並透過版本化 command 等待
  engine Ack；這不是已驗證的實體 per-App capture/re-send。
- 本機 Windows 26200／Visual Studio 17 只能當 portable/user-space 證據。driver、WinUI XAML
  preview、簽章與 Windows 11 24H2 hardware soak 必須在鎖定 target 環境重新驗證。
- 本機 Desktop Compatibility Preview 已可由 `tools/build-preview.ps1 -Target DesktopCompat` 建置並通過
  啟動 smoke；它和正式 shell 共用 `EasyControlViewModel`，自帶 .NET runtime、不依賴 Windows App Runtime，
  現在包含場景選擇、路由健康摘要與音量來源／致動器顯示，但不是 XAML、無障礙、driver 或
  release evidence；連線後每秒輪詢一次 bounded ControlStatusSnapshot，命令忙碌時會合併輪詢。
 目前也顯示本機 physical catalog 的 render/capture 數量與預設輸出 metadata；預設不提供實體
  sink 啟用或切換。若明確傳入 `--enable-wasapi-output`，Engine Preview 會在 catalog 找到
  支援的 active default render endpoint 後啟動既有 dedicated shared-mode WASAPI sink worker，
  並把 worker 的 `Pending/Ready/Degraded` 狀態送進 `main-output` route-health card。這只是
  user-space output boundary；沒有 source block 時 worker 只保持安全 silence，不能寫成完整
  physical playback、WaveRT 或 per-App DSP delivery。
- WinUI 與 Desktop Preview 都有 IR phase policy controls（Game/Balanced/Movie/Bypass）與明確的
  0/80/160 ms delay semantics；控制面會把 bounded IR WAV 送入 Engine Preview。
- C++ control-plane 已有 bounded RIFF/WAVE IR importer，支援 Float32/PCM16/24/32、finite/tap/file
  bounds 與 channel-major convolver prepare；`AudioEngineModel::prepare_ir`／`commit_ir`／
  `prepare_ir_clear`／`rollback_ir` 會把它接到固定 output-group graph render，IR 在 Group Master
  與 limiter 前執行；SceneApply 會以同一 transaction 清除舊 IR，見
  `evidence/0000-foundation/ir-graph-attachment-v1.json`。
- `build_ir_phase_kernel_v1` 已補上 control-plane 的 real-cepstrum minimum-phase 與
  source-magnitude causal linear-phase transform；C# `PrepareIrAsync`、Desktop Preview 與 Engine
  Preview 已能透過固定 288-byte `IrPrepareCommand` 完成 WAV→kernel prepare、graph attachment
  commit Ack。這仍不能宣稱已連接實體 sink、WaveRT driver 或完成聲學校正。
- C++ Engine Preview 已可由 `tools/build-engine-preview.ps1` 建置；`tools/engine-preview-smoke.ps1`
  會啟動它並驗證 v1 named-pipe Hello/Ack request correlation 與 ControlStatusSnapshot 回覆。
  `tools/control-model-engine-smoke.ps1` 另外以 C# `EasyControlViewModel` 驗證 −18 dB 音量
  往返、引擎快照讀回、Game One-Tap SceneApply Ack，以及本機 Windows render/capture
  catalog（目前環境 14 筆）的跨程序 snapshot。Engine Preview 預設只枚舉 metadata、保留
  bounded snapshot 與 watcher poll；只有 `--enable-wasapi-output` 才啟動既有 shared-mode
  WASAPI sink worker，且不改 Windows 音量。
  `tools/run-preview.ps1 -Build` 預設使用 `-Ui Auto`：有符合版本的 Windows App Runtime 1.7
  就啟動 WinUICompat，否則自動退回不依賴 Runtime 的 Desktop Compatibility UI；兩者都只
  提供 user-space control host，不代表 WaveRT、實體輸出或 Windows session routing 已完成。
  若要固定 DesktopCompat，加入 `-Ui DesktopCompat`；若要明確啟動現有 shared-mode sink，加入
  `-EnableWasapiOutput`；若要明確驗證 Windows endpoint 音量聯動，使用
  `tools/run-preview.ps1 -Build -EnableSystemVolume`；預設不會寫入系統音量，且
  `tools/engine-preview-smoke.ps1 -EnableSystemVolume -StatusOnly` 只檢查 broker Ready、不送音量命令。
  `tools/engine-preview-smoke.ps1 -EnableWasapiOutput -StatusOnly` 只檢查 sink route state，
  不把 status-only 結果當成實體音訊播放證據。
- Engine Preview 另有獨立的 `--enable-session-routing` opt-in：它在 COM worker 綁定
  `IAudioSessionManager2`，發布 bounded App/session catalog，並把 App 音量、lane/output 與
  route-rule 命令送入固定 queue；`-EnableSessionRouting` 可與系統音量旗標同時使用。這是
  控制面與 Windows session volume 的可重跑邊界，不是實體 per-App capture/re-send 或 DSP
  delivery 證據；Desktop Preview 只顯示安全摘要與明確的 delivery unverified 警示。
- ISO 226 只保留合法 formula/derived boundary；禁止把受限標準文件、完整表格、掃圖或其內容
  放進 source、Issue、prompt、RAG、fixture 或 evidence。
- `evidence/0000-foundation/winui-compat-preview-v1.json` records the local Microsoft App Runtime
  1.7 installation and the successful `WinUICompat -SmokeTest`; it is only a compatibility launch
  proof and must not be promoted to formal XAML/accessibility evidence.
- `evidence/0000-foundation/control-model-engine-ir-clear-v1.json` records three consecutive
  session-routing control-model runs, including IR prepare → Scene IR clear and bounded temporary
  fixture cleanup. It is user-space reliability evidence only; it does not prove physical playback.

## 唯一下一步

在 Windows 11 24H2+ x64、Visual Studio 2026、SDK/WDK 10.0.28000.2526 的乾淨機器上，先完成
source-only WinUI XAML build 與 accessibility smoke evidence；成功後才進行第一個 loadable
WaveRT endpoint 的 WDK build/signability 工作。不要先做 Microsoft signing、Gumroad 上傳、
發佈 binary 或宣稱 consumer preview。

目標機器可用 `pwsh -File tools/build-preview.ps1 -Target WinUI` 產生不追蹤的本機 UI preview；
這不是 installer，也不代表虛擬 driver 已完成。

非 target 機器要看同一控制模型，可用 `pwsh -File tools/build-preview.ps1 -Target DesktopCompat`；輸出
只在 ignored 的 `.local/preview/DesktopCompat/`，不可加入 Git 或發布。若要直接打開可連線的本機
  預覽，使用 `pwsh -File tools/run-preview.ps1 -Build`；它會先啟動 user-space Engine Preview，再
  開啟自帶 .NET runtime 的桌面 UI，關閉 UI 後引擎會一併停止。若已安裝 Windows App Runtime
  1.7 x64，可用 `pwsh -File tools/build-preview.ps1 -Target WinUICompat -SmokeTest` 驗證
  WinUI 相容殼；它仍不是正式 XAML/accessibility evidence。沒有 Runtime 時使用 DesktopCompat。
Windows 使用者也可雙擊 repository 根目錄的 `Start-HibikiPreview.cmd`；這只是上述命令的來源入口，
不會把任何編譯物加入 Git。
若需要明確啟動 shared-mode WASAPI sink，使用同一層的 `Start-HibikiPreview-Wasapi.cmd`；它仍是
user-space opt-in，不代表 driver 或完整播放驗收。

## 必讀順序

1. [AGENTS.md](../AGENTS.md)：硬限制與每次必跑命令。
2. [START_HERE.md](START_HERE.md)：fresh clone、權威順序與資料邊界。
3. [active Issue 0 handoff](tasks/active/0.md)：已完成、最後驗證、風險與下一步。
4. [baseline](state/BASELINE.md)：main 已合併能力與限制。
5. 對應的 [Spec index](specs/INDEX.md) 與 [evidence](../evidence/0000-foundation/)。

## 不可自行做的事

- 不重生 `config/distribution-profile.yml` 的 endpoint GUID、driver hardware ID、ASIO CLSID、IPC namespace。
- 不把 `.local/`、`bin/`、`obj/`、PE/COFF、簽章檔、金鑰、真實 endpoint/session ID 或私人校正檔加入 Git。
- 不宣稱 vendor ASIO、WASAPI Exclusive、RAW、Atmos/DTS:X 或未經使用者手勢的 Chrome tab capture 已受 Hibiki 控制。
- `PhysicalDeviceCatalog` 的 capture default 允許存在，但只有 Active render 才能被 UI 選取；
  catalog readiness 不等於 physical sink 已開啟。
- 不把「控制命令已入列」或「預設已保存」寫成「已完成引擎／實體音訊套用」；session
  routing smoke 只代表 catalog／queue／Windows session 邊界可用。

## 交接前最小完成條件

每一個後續 AI 在換機前必須：更新 active handoff 的 base commit/驗證/限制/下一步、建立 WIP
commit、push branch，並跑與改動範圍相符的 gate。所有 public contract 變更都要同步更新
Spec、tests、evidence 與 `docs/state/BASELINE.md`。
