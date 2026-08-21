# Hibiki DSP AI 工作規則

## 先讀什麼

每次開始工作先讀本檔、`docs/START_HERE.md` 與 `docs/AI_HANDOFF.md`，再讀 GitHub Issue 指定的
handoff、Spec、ADR、source 與 tests。聊天紀錄、AI memory、個人 IDE 規則
都不是專案真值。

## 專案硬限制

- 目標：Windows 11 24H2+ x64；C++20 即時核心、C# WinUI 3 UI。
- RT audio thread 不配置、不取得 mutex、不等待、不呼叫 COM/UI/檔案系統。
- driver 與 GPL user-space 只能透過版本化 IPC；不得把 MS-PL driver 靜態或動態連入 GPL engine。
- 廠商 ASIO、WASAPI Exclusive、RAW 路徑不可宣稱受 Hibiki 控制。
- 不提交 EXE、DLL、SYS、MSI、MSIX、VST3、PE/COFF、簽章憑證或私密金鑰。
- 真實裝置 ID、校正檔、序號、私人路徑放 `.local/`，不得進 Git。
- 不反編譯或繞過閉源軟體保護；只用開源程式、官方文件與合法 black-box 觀察。
- ISO 226 授權文件、掃描、完整表格與受限資料不可放入 repo、Issue、prompt 或 RAG。

## 真值與文件契約

- 產品行為看 accepted Spec；架構取捨看 accepted ADR；已完成能力看 source、tests、evidence 與 `docs/state/BASELINE.md`。
- 修改 public API、schema、DSP 順序、安全規則或建置方式時，同一 PR 必須更新對應文件與 evidence。
- Accepted ADR 不可改寫；新決策建立新 ADR 並標示 supersedes。
- 每個 Issue 使用一份 `docs/tasks/active/<issue>.md` handoff；換 AI 或電腦前建立 WIP commit、push branch、更新下一個安全動作。

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
