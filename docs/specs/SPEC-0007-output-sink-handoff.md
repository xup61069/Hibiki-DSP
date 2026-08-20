---
id: SPEC-0007
status: accepted
owner: hibiki-maintainers
authority: architecture
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/**output*", "src/hub/include/hibiki/output*", "src/hub/src/output*"]
---

# SPEC-0007：多輸出 sink 交接與時鐘連續性

每個 physical sink 由獨立 ring/SRC pipeline 處理。新 sink 必須先完成 format、clock
與 buffer prepare，再進入交叉淡化；切換失敗時保留舊 sink，不重啟全域 graph。

## Crossfade

- `OutputCrossfade` 由 control plane 以 2/6/8 聲道、44.1/48/96/192 kHz 與 1–200 ms
  duration prepare；預設交接 duration 為 30 ms。
- audio-side `process` 不配置、不鎖、不呼叫 COM，使用 equal-power sin/cos 權重；完成後
  僅輸出新 sink。buffer 不足、null pointer 或未開始都回傳 failure，不寫部分結果。

## Clock 與回復

- crossfade 與 `OutputSinkModel` 的 persistent SRC 可連續串接；clock drift 只由 control
  plane 更新 ratio，audio thread 讀 immutable snapshot。
- USB/HDMI/Bluetooth 拔插或 Audio Service invalidation 由 `DeviceRecoveryCoordinator`
  進入 safe-start；不得回到 0 dB、100% 或未驗證的舊 endpoint。
- `WindowsWasapiSinkWorkerV1` 將 COM/WASAPI 完整限制在單一 dedicated sink worker apartment：
  graph/ASIO/TabCapture producer 只呼叫 bounded SPSC `submit`，worker 在 endpoint event 後
  pop block，空 queue 補 silence，並透過 `OutputSinkModel` 的 persistent SRC 處理已排程的
  clock observation。`observe_clock` 只寫入 atomic latest-request，實際 SRC 更新在 worker
  套用；graph RT 不呼叫 COM、等待或配置。
- worker snapshot 必須可觀察 `endpoint_ready`、`degraded`、dropped/submitted/rendered blocks、
  `source_step` 與 `drift_ppm`，讓 UI 在實體 endpoint 未綁定時顯示 detached，而不是靜默宣稱
  已輸出。
- 真實 WaveRT endpoint、硬體 clock fixture、DPC/拔插 soak 與 WHCP/HLK 證據不在本機
  contract test 中，必須在 Windows 11 24H2+ test machine 完成。

## Multi-sink fan-out

`OutputFanoutPlanV1` 將同一個 graph block 複製到最多 8 個同聲道 layout 的 sink。所有 enabled
sink 的 pointer／capacity 在第一次寫入前一次驗證；任何容量不足或 plan 無效都 fail-closed，
不會只更新部分 sink。每個 sink 後續仍由自己的 ring、clock drift 與 SRC worker 處理；fan-out
本身不碰 COM、裝置或 physical endpoint。
