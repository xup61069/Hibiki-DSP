---
id: SPEC-0022
status: accepted
owner: hibiki-maintainers
authority: control-plane
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/include/hibiki/session_command_queue.hpp", "src/hub/src/session_command_queue.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "src/hub/include/hibiki/engine_control.hpp", "src/hub/src/engine_control.cpp", "tests/unit/contract_tests.cpp"]
---

# SPEC-0022：Session 命令的 COM worker 佇列

## 成功條件

EngineControl／pipe 控制執行緒可以安全接受 `SessionVolumeCommand`、`SessionRouteCommand` 與
`SessionRouteRuleCommand`，而不直接呼叫 `IAudioSessionControl`、`ISimpleAudioVolume` 或
Windows endpoint COM。命令只會進入固定容量的 in-process SPSC queue；擁有
`WindowsAudioSessionWatcher` 的 COM worker 在 `refresh_now`／`poll_and_refresh` 後取出命令，
重新驗證目前 catalog，再執行 volume 或 route graph transaction。

## Queue 契約

- `SessionCommandQueueV1` 固定 64 slots，producer 是 EngineControl worker，consumer 是
  Windows COM worker。
- 每個 slot 是固定大小的 `SessionCommandWorkItemV1`，不含 mutex、condition variable、
  COM pointer 或 raw session ID。
- queue storage 在 runtime 建立時一次配置到 heap；producer／consumer 路徑不配置、不釋放，
  因此新增較大的 route-rule payload 不會把 64 slots 壓到呼叫者 stack。
- `try_push` 在滿載時回傳 false 並遞增 `dropped`；不阻塞、不覆蓋尚未消費的命令。
- runtime stop 先停止 pipe producer，再 reset queue；新的 runtime 不會繼承前一個 endpoint
  binding 的 pending command。
- EngineControl 的 `Applied` 語意是「已成功入列」，不是「Windows 已完成 readback」；實際
  套用仍須由下一個 worker refresh／catalog/status snapshot 觀察。

## 驗證與失敗

- 入列前驗證 running、非零 handle／catalog sequence、最新 sequence、dB Q16.16 範圍、mute、
  bounded printable UTF-8 lane/output。錯誤或 queue 滿載都回傳 false，UI 保留原本安全狀態。
- Worker 取出後仍由 coordinator 驗證 generation-scoped handle 與 registry membership；refresh
  使 handle 過期時命令只被消費並 fail closed，不改 graph、volume 或 catalog。
- route 成功後才發布 route status 與新 catalog；同一批中使用舊 sequence 的後續 route 命令會
  被拒絕，要求 UI 重新取得 catalog，而不是套用到錯的 App。
- `drain_session_commands` 只允許在 `WindowsControlRuntimeV1::start` 所在 worker thread
  執行；非 owner 呼叫回傳 0 且不觸碰 COM。

## 即時與相容性邊界

此 queue 不是 RT audio callback 路徑；RT graph 仍只讀 immutable graph／volume snapshot。
Vendor ASIO、WASAPI Exclusive、RAW 與 Chrome 單分頁 tabCapture 仍不因 queue 存在而變成
可攔截。target Windows 11 24H2 的 active-session readback、route delivery、Audio Service
restart 與五來源同步播放另列為硬體／平台驗收。

## 驗收

1. CTest 覆蓋 volume/route/route-rule work item FIFO、64-slot capacity、drop counter、reset 與 empty
   queue。
2. EngineControl handler 從非 COM 執行緒只入列，不回傳 `RPC_E_WRONG_THREAD`，且不發生
   COM call；Windows worker drain 再執行實際 coordinator 方法。
3. source-only gates、docs/source policy 與 Windows build 必須維持通過；不提交編譯物。
