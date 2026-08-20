---
id: SPEC-0006
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/**", "tests/**"]
---

# SPEC-0006：場景安全音量控制

場景可以針對自己的 lane 使用壓縮、EQ 與 makeup gain，但系統 Group Master
仍由 Windows dB canonical state 控制。安全控制器只產生控制面 action，不能在
audio callback 直接呼叫 COM、寫 registry 或配置記憶體。

## 開始與基準

- `begin(scene, current)` 保存 `min(requestedDb, safetyCeilingDb)` 作為基準。
- 只有 `schema_version=1`、limiter `-24..0 dBTP` 與有限 volume state 才接受。
- `auto_attenuate=false` 的場景不自動改 Group Master。

## 過峰衰減

- `observe_peak` 只接受有限的 true-peak dBTP；超過 scene limiter 加 0.5 dB
  hysteresis 才產生 action。
- 每次最多衰減 3 dB，兩次 action 至少相隔 100 ms；action 使用 `Safety`
  origin，由 VolumeBroker 帶固定 event-context GUID 寫回 Windows。
- 偵測到使用者把 Windows 音量改離最近一次 controller target 超過 0.25 dB
  後，當前場景停止自動回寫，不覆蓋使用者意圖。

## 結束與恢復

- 若場景期間沒有手動改音量，`end` 只把音量恢復到進場基準，origin 為
  `Scene`；安全 ceiling 仍優先。
- 若使用者手動改過音量，`end` 不產生 restore action。裝置重連／Audio Service
  重啟仍由既有 recovery safe-start policy 處理，不能恢復到 0 dB 或 100%。

## 不在本規格

內容感知 loudness（BS.1770/LUFS）、ISO 226 曲線、limiter DSP 與實際
`IAudioEndpointVolume` 寫入各由既有 volume／ISO／Windows adapter 契約負責。
