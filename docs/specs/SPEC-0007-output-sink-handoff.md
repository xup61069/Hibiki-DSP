---
id: SPEC-0007
status: accepted
owner: hibiki-maintainers
authority: architecture
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/output*", "src/hub/include/hibiki/output*", "src/hub/src/output*"]
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
- 真實 WaveRT endpoint、硬體 clock fixture、DPC/拔插 soak 與 WHCP/HLK 證據不在本機
  contract test 中，必須在 Windows 11 24H2+ test machine 完成。
