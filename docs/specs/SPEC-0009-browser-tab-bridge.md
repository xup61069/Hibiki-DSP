---
id: SPEC-0009
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["extensions/**", "tools/extension-check.ps1"]
---

# SPEC-0009：瀏覽器單分頁音訊 bridge

瀏覽器只能在使用者點擊 popup 後呼叫 `tabCapture`。MV3 offscreen document 維持
stream，AudioWorklet 將音訊轉成 `HIBT` packet；Windows process routing 不得宣稱
能靜默取得 Chrome/Edge 單一分頁。

## HIBT packet

固定 little-endian header：`magic='HIBT'`、`version=1`、`channels`、`frames`、
`sampleRate`，後接 interleaved Float32。receiver 必須拒絕未知版本、channels>8、
不支援 sample rate、payload overflow 或 header/實際長度不一致；任何拒絕不得讓
瀏覽器本地回放中斷。

## Native bridge boundary

The repository now includes `hibiki_tab_bridge_contract`, a portable decoder
that validates all fields and rejects non-finite samples before exposing a
non-owning packet view.

目前 extension 只連 `ws://127.0.0.1:17842/v1/tab` 的 loopback receiver；bridge 未啟動
時丟棄送出 packet。receiver 已限制 localhost、WebSocket frame 大小、mask、ping/close
與 decoder 驗證。`TabCaptureQueueV1` 將 validated packet 複製到四格固定 SPSC ring，
控制執行緒可用 `enqueue_tab_capture_packet_v1` 作 callback，RT lane 再以 caller-owned
buffer pop；滿載會回報 dropped blocks，不阻塞 WebSocket。`process_tab_capture_lane_v1`
會把一個 queue block 送入 `AudioEngineModel::process_lane_block`，因此沿用同一份
immutable graph、lane mapping 與 Group Master；adapter 不配置、不等待，也不擁有音訊
buffer。降噪模型 provenance、權限提示與斷線重連 policy 仍必須在獨立 source component
完成後，才能宣稱「單分頁掛降噪」。
