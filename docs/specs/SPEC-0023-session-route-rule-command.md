---
id: SPEC-0023
status: accepted
owner: hibiki-maintainers
authority: control-plane
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/include/hibiki/control_payloads.hpp", "src/hub/src/control_payloads.cpp", "src/hub/include/hibiki/ipc.hpp", "src/hub/src/ipc.cpp", "src/hub/include/hibiki/engine_control.hpp", "src/hub/src/engine_control.cpp", "src/hub/include/hibiki/session_command_queue.hpp", "src/hub/src/session_command_queue.cpp", "src/hub/include/hibiki/windows_audio_session_route.hpp", "src/hub/src/windows_audio_session_route.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "apps/control-model/IpcProtocol.cs", "tests/unit/contract_tests.cpp", "apps/control-model-check/Program.cs", "apps/engine-preview/engine_preview.cpp", "tools/engine-preview-smoke.ps1"]
---

# SPEC-0023：per-App 路由規則命令與候選交易

## 成功條件

控制面可以以固定 wire 命令建立、移除或清除 per-App route rule。規則依 app ID／display
name 匹配，指定 lane、output group、優先級、啟用狀態、makeup gain 與 gain owner；規則
變更先套用到候選 `SessionRouteRuleStoreV1`，只有候選 session registry 與 route graph
都成功建立才 commit。任何非法文字、過期 catalog、歧義匹配、未綁定或 graph 失敗都保留
原規則與原 graph。

## Wire v1

`SessionRouteRuleCommand` 為 480 bytes little-endian：schema 0..3、priority 4..7、
makeup gain Q16.16 8..11、operation 12、enabled 13、gain owner 14、reserved 15、
catalog sequence 16..23；五個長度位於 24..28，reserved 29..31。rule ID 位於 32..95
（最多 64 bytes），app ID 96..223、display name 224..351（各最多 128 bytes），lane
352..415、output group 416..479（各最多 64 bytes）。未使用 bytes 必須為零；文字為
strict printable UTF-8。

operation `Upsert=1` 必須有 rule ID、至少一個 matcher、lane 與 output；`Remove=2` 只
允許 rule ID；`Clear=3` 不允許任何文字。catalog sequence 必須非零，gain 為 -144..12 dB。

## 交易與 queue

EngineControl 只將固定 `SessionCommandWorkItemV1::RouteRule` 入列；`Applied` 代表入列成功。
Windows COM worker drain 時複製目前規則、套用單一操作並呼叫 `set_rules_and_refresh`。候選
失敗時 watcher 仍指向舊規則，registry／graph／catalog 不變；成功後一次交換規則、registry
與 graph、發布 route status／catalog 並使舊 sequence 失效。

## UI／失敗行為

一般 UI 不直接顯示 rule wire 細節；Expert 模式可建立「遊戲／App → lane/output」預設，並
在 catalog refresh 後顯示實際套用結果。queue 滿載、worker 停止或規則衝突時 fail closed，
保留原 lane、output、volume 與安全限制，不把命令誤報為已套用。

## 相容性邊界

此規則只定義控制面匹配與候選 graph 邊界，不宣稱 vendor ASIO、WASAPI Exclusive、RAW 或
Chrome 單分頁可被靜默攔截；實體 process-loopback、tabCapture、Windows 24H2 active-session
delivery 仍須平台驗收。

Engine Preview 以 `--enable-session-routing` 作為明確的 opt-in boundary：只有帶旗標時才綁定
`IAudioSessionManager2`、發布 bounded catalog，並由 COM worker drain fixed session command queue。
未帶旗標時不枚舉或操作 Windows session；帶旗標也只證明 catalog、handle/sequence validation
與 Windows session volume／候選 graph 控制面，不能把 status 或 Ack 寫成實體 per-App delivery。

## 驗收

1. C++／C# 480-byte round-trip、三種 operation、padding、UTF-8、gain／sequence 與容量
   rejection 通過。
2. IPC type 18、EngineControl callback、SPSC route-rule FIFO 與 queue 滿載 fail closed 通過。
3. Windows unbound runtime command fail closed；真實 active session 的候選 commit／rollback、
   route delivery、拔插與 Audio Service restart 另列硬體驗收，未在 portable contract test 宣稱完成。
