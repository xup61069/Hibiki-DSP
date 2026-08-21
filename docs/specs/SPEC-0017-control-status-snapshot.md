---
id: SPEC-0017
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/include/hibiki/control_status.hpp", "src/hub/src/control_status.cpp", "src/hub/include/hibiki/control_service.hpp", "src/hub/src/control_service.cpp", "apps/control-model/IpcProtocol.cs", "apps/control-model/EasyControlViewModel.cs", "tests/**"]
---

# SPEC-0017：控制狀態快照 IPC

## 成功條件

控制 worker 可以用一個 bounded、版本化 request/reply，將目前 output-group 音量安全狀態
與來源路由健康狀態送到 UI。UI 收到完整且通過驗證的快照後才替換狀態；未收到快照、快照
過期或格式錯誤時，保留上一個安全狀態，不把預設卡片說成已連線。

## Wire contract

`ControlStatusRequest`（type 13）為空 payload；`ControlStatusSnapshot`（type 12）使用
little-endian fixed header + 最多 8 個 224-byte route entries。Header 保存 snapshot sequence、
requested／safety ceiling／effective Q16.16 dB、mute、origin、actuator 與 volume generation。
Route entry 保存 bounded UTF-8 ID／名稱／說明、`Ready/Pending/Degraded/Bypassed/Unavailable`
狀態與 requires-user-action flag。所有 padding 必須為零，ID 不可重複，effective dB 不可高於
requested 或 safety ceiling。

## 執行緒與資料流

`ControlStatusSnapshotStoreV1` 只由 control／worker 使用，以完整 payload 原子替換並由 named
pipe reply 複製；不在 RT、COM callback 或 UI callback 中配置或等待。`ControlPlaneHostV1`
可選掛載 status store；未掛載時 ControlStatusRequest 回 Error。C# `EasyControlViewModel`
在 Hello／device catalog 後以 bounded serialized request 取得狀態，先用暫存 Expert model
驗證 routes，再一次套用 volume 與 route snapshot。

## 失敗／fallback

- unknown type、錯誤長度、非零 padding、非法 UTF-8、重複 route ID、過期 sequence 或 unsafe
  volume 一律拒絕。
- status store 沒有 snapshot 時回 Error；UI 保留上一個狀態並顯示控制狀態暫不可用。
- status snapshot 只描述 control-plane truth，不代表 physical per-App audio delivery、
  Chrome tabCapture、signed driver 或 process-loopback runtime 已可用。

## 驗收

1. C++／C# payload round-trip、Q16.16 volume、route flags、reserved bytes、duplicate IDs 與
   stale store publish 都通過 contract/control-model checks。
2. named-pipe handler 能以 request ID 回覆 ControlStatusSnapshot；未掛載 store 回 Error。
3. ViewModel 對完整快照採 atomic-style validation；malformed／stale snapshot 不改變可見狀態。
4. public repository 不包含編譯後 payload、driver、endpoint identity 或私人 calibration。
