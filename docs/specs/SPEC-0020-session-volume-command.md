---
id: SPEC-0020
status: accepted
owner: hibiki-maintainers
authority: control-plane
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/include/hibiki/control_payloads.hpp", "src/hub/src/control_payloads.cpp", "src/hub/include/hibiki/ipc.hpp", "src/hub/src/ipc.cpp", "src/hub/include/hibiki/engine_control.hpp", "src/hub/src/engine_control.cpp", "src/hub/include/hibiki/session_command_queue.hpp", "src/hub/src/session_command_queue.cpp", "src/hub/include/hibiki/windows_audio_session_route.hpp", "src/hub/src/windows_audio_session_route.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "apps/control-model/IpcProtocol.cs", "apps/control-model/EasyControlViewModel.cs", "tests/unit/contract_tests.cpp", "apps/control-model-check/Program.cs"]
---

# SPEC-0020：以暫時 handle 套用 per-App session volume

## 成功條件

控制面可以把 UI 選取的 App 工作階段音量要求送到 worker；命令只包含 catalog
generation-scoped handle、catalog sequence、Q16.16 dB 與 mute，不包含 raw Windows
session-instance ID、PID 或 endpoint ID。未綁定、已過期、未知 handle 或不符合目前
catalog sequence 的要求必須 fail closed，不改變任何 caller state。

這是 Windows `ISimpleAudioVolume` 的控制邊界；它不宣稱完成 physical per-App re-render、
Chrome tabCapture、vendor ASIO 攔截或對所有 exclusive stream 生效。實際 COM 呼叫只能在
擁有 `WindowsAudioSessionWatcher` 的 worker thread 進行。

## Wire v1

`SessionVolumeCommand` 固定 24 bytes little-endian：

| offset | bytes | 欄位 |
| --- | ---: | --- |
| 0 | 8 | generation-scoped `handle` |
| 8 | 4 | requested dB Q16.16，−144…0 |
| 12 | 1 | mute，0/1 |
| 13 | 3 | reserved，必須為零 |
| 16 | 8 | `catalog_sequence` |

decoder 先驗證完整長度、reserved bytes、有限且在 −144…0 dB 的 dB 值、非零 handle／sequence；失敗不得部分
寫入 `ControlCommandV1`。pipe handler 只負責驗證與送入 control queue，EngineControlWorker
再呼叫明確的 `SessionVolumeHandlerFnV1`。Windows runtime handler 只把命令送入固定容量的
`SessionCommandQueueV1`；`Applied` 表示已入列，不表示 COM readback 已完成。沒有 handler、
queue 滿載或 worker 未啟動時回報 Failed，不假裝已套用。

## Handle／sequence 驗證

Windows coordinator 解出 handle 的 generation 與 registry index，兩者必須同時符合目前
refresh；runtime 另要求 command sequence 等於已發布 `SessionCatalogSnapshotStoreV1` 的
最新 sequence。任何 device/session refresh 都會使舊 sequence 或 generation 失效。事件
context 固定使用 `WindowsVolumeEventContextsV1::session()`，避免 Windows volume callback
回授迴圈。

## UI／失敗行為

- C# `ControlPayloadsV1` 與 C++ codec 使用同一 24-byte known layout；Expert 的選取 App 控制
  會把目前 catalog 的 dB/mute 同步到 bounded slider／mute 控件，再以目前 sequence 建命令。
- `EasyControlViewModel` 只能由目前 `SessionCatalog` 選取 handle，Build command 前再次
  驗證；清單刷新後若 handle 消失，選取自動清除。
- dB 越界、未知／stale handle、未連線或 worker 失敗都保留現有可見狀態；不清空 catalog、
  不修改 RT graph、不把錯誤回寫成成功。
- Windows runtime adapter 可由非 COM 的 EngineControl 執行緒入列；只有 `start` 所在的 COM
  worker 會在 refresh 後 drain，避免 EngineControl／pipe callback 越權呼叫 COM。直接呼叫舊的
  同步 read/write API 仍維持 wrong-thread fail-closed。

## 驗收

1. C++／C# round-trip、reserved／range／zero identity rejection、request correlation 與
   EngineControl handler callback 通過。
2. Windows coordinator/runtime unbound handle read/write fail closed；target Windows 24H2
   需另以明確測試 session 完成 readback、mute、stale sequence 與 Audio Service recovery。
3. 本規格不把 per-App 實體路由或同時五來源播放列為本機 source-only gate；那些仍是硬體／
   driver／第三方應用整合驗收。
