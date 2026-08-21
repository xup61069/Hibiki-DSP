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
- 本機 Windows 22631／Visual Studio 17 只能當 portable/user-space 證據。driver、WinUI XAML
  preview、簽章與 Windows 11 24H2 hardware soak 必須在鎖定 target 環境重新驗證。
- 本機 Desktop Compatibility Preview 已可由 `tools/build-preview.ps1 -Target DesktopCompat` 建置並通過
  啟動 smoke；它和正式 shell 共用 `EasyControlViewModel`，自帶 .NET runtime、不依賴 Windows App Runtime，
  現在包含場景選擇、路由健康摘要與音量來源／致動器顯示，但不是 XAML、無障礙、driver 或
  release evidence；連線後每秒輪詢一次 bounded ControlStatusSnapshot，命令忙碌時會合併輪詢。
- WinUI 與 Desktop Preview 都有 IR phase policy controls（Game/Balanced/Movie/Bypass）與明確的
  0/80/160 ms delay semantics；這是 UI contract，尚未送出 FIR coefficients。
- C++ control-plane 已有 bounded RIFF/WAVE IR importer，支援 Float32/PCM16/24/32、finite/tap/file
  bounds 與 channel-major convolver prepare；它仍不做 FIR phase kernel derivation、graph commit 或
  physical sink playback，見 `evidence/0000-foundation/ir-wav-decoder-v1.json`。
- C++ Engine Preview 已可由 `tools/build-engine-preview.ps1` 建置；`tools/engine-preview-smoke.ps1`
  會啟動它並驗證 v1 named-pipe Hello/Ack request correlation 與 ControlStatusSnapshot 回覆。
  `tools/control-model-engine-smoke.ps1` 另外以 C# `EasyControlViewModel` 驗證 −18 dB 音量
  往返、引擎快照讀回與 Game One-Tap SceneApply Ack。
  `tools/run-preview.ps1 -Build` 會把 Engine Preview 與不依賴 Windows App Runtime 的 Desktop
  Compatibility UI 一起啟動；它只提供 user-space control host，不代表 WaveRT、實體輸出或
  Windows session routing 已完成。
- ISO 226 只保留合法 formula/derived boundary；禁止把受限標準文件、完整表格、掃圖或其內容
  放進 source、Issue、prompt、RAG、fixture 或 evidence。

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
開啟自帶 .NET runtime 的桌面 UI，關閉 UI 後引擎會一併停止。不要直接執行需要 Windows App
Runtime 的 `WinUICompat` 輸出。

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
- 不把「控制命令已入列」或「預設已保存」寫成「已完成引擎／實體音訊套用」。

## 交接前最小完成條件

每一個後續 AI 在換機前必須：更新 active handoff 的 base commit/驗證/限制/下一步、建立 WIP
commit、push branch，並跑與改動範圍相符的 gate。所有 public contract 變更都要同步更新
Spec、tests、evidence 與 `docs/state/BASELINE.md`。
