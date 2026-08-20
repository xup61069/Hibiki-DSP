# Hibiki DSP baseline

## 已完成（有 commit 與 evidence）

- 公開 monorepo 文件、component license map 與 source-only paid-release policy。
- AI 接手規則、fresh-clone 流程與 source-only policy。
- `OutputGroupVolumeState` 與 ISO compensation public C++ boundary 的初始骨架。
- Scene graph、device-switch transaction 與初始 CMake/CTest 驗證入口。
- Immutable RT graph snapshot、2/6/8 聲道 mapping、IPC frame codec、ASIO stream model、VST
  quarantine model 與 MV3 tab-capture source prototype。
- Caller-owned output ring buffer、clock-drift estimator、bounded linear SRC prototype、
  Apache C driver ABI 與 portable driver validator。
- Source-only PowerShell installer bootstrapper with manifest/hash dry-run gate.
- Easy Scene factory、AcousticAnchor phon mapping、PEQ/APO/CamillaDSP/REW exporters 與 WAV IR
  serializer。
- `AudioEngineModel` facade connecting graph transaction, Windows volume notification and RT
  processing with one Group Master gain.
- Windows-only `IAudioEndpointVolume` broker with non-blocking callback snapshot, dB/mute
  read-back and event-context write path; no physical endpoint was exercised on this machine.
- Windows-only `IMMNotificationClient` watcher with bounded default/add/remove/property event
  snapshots; worker-side rebind and Audio Service recovery are still pending.

## 尚未開始

- 可載入的 WaveRT/SYSVAD-derived driver。
- 真正的 WaveRT-backed RT engine、Steinberg ASIO DLL、out-of-process VST3 SDK host、WinUI 3 UI、
  native browser bridge、persistent SRC state、physical sink clock integration 與 signed package
  delivery。
- ISO 226:2023 合法係數來源與正式 conformance oracle。
- Microsoft driver signing、Gumroad release artifact 與 production installer。

目前開發機是 Windows build 22631、VS 17／SDK 10.0.26100.0，低於鎖定的 driver 目標
Windows 26100+、VS 2026／SDK-WDK 10.0.28000.2526；因此 user-space tests 可通過，但
不能把本機結果當成 driver-target evidence。

## 最近驗證

初始 foundation evidence 已寫入 `evidence/0000-foundation/initial.json`，目前對應最新
engine baseline commit `60e6385`；新 AI 接手時仍必須確認 working tree 與該 scope 是否一致。

目前驗證摘要：`verify.ps1` 的 1 個 CTest 通過；`docs-check.ps1` 的 40 個必要入口與
5 份 Spec 通過；`source-policy.ps1` 掃描 125 個路徑且無 blocked binary/secret；
`extension-check.ps1`、`installer-check.ps1`、`control-model-check.ps1` 與
`distribution-check.ps1` 通過；16 個 JSON 檔案均可解析。
