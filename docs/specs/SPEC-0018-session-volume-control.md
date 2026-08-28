---
id: SPEC-0018
status: accepted
owner: hibiki-maintainers
authority: platform-boundary
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/include/hibiki/windows_audio_session_route.hpp", "src/hub/src/windows_audio_session_route.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "src/hub/include/hibiki/windows_audio_session_watcher.hpp", "src/hub/src/windows_audio_session_watcher.cpp", "tests/unit/contract_tests.cpp"]
---

# SPEC-0018：Windows per-session volume control boundary

## 成功條件

在 worker/control thread 上，Hibiki 可以針對目前已枚舉的 Windows audio session 讀寫
`ISimpleAudioVolume` 的 dB 等效音量與 mute；呼叫不進入 RT、不接受未枚舉或過期 session，
並且任何失敗都保留既有狀態。這是 per-session gain 的平台邊界，不宣稱已完成實體
per-App 重送、Chrome tabCapture 或 vendor ASIO 攔截。

## Identity 與介面

`session_instance_id` 是 Windows `IAudioSessionControl2::GetSessionInstanceIdentifier`
回傳的 ephemeral key，只能在目前 endpoint 的已提交 `AudioSessionRegistry` 中使用；它不是
SceneProfile identity，也不可寫入永久 preset。PID 只能作為顯示／診斷輔助，不是 control key。

`WindowsAudioSessionRouteCoordinatorV1` 提供：

- `write_session_volume(id, requestedDb, mute, eventContext)`：只接受 finite、−144 至
  0 dB（Windows `ISimpleAudioVolume` 的 scalar 上限），且必須先在 registry 找到相同
  session instance；成功後由 watcher 呼叫
  `ISimpleAudioVolume::SetMasterVolume`／`SetMute`。
- `read_session_volume(id, requestedDb, mute)`：同樣要求已綁定且已枚舉，成功才寫入輸出
  引數；失敗不修改 caller state。

`WindowsControlRuntimeV1` 只轉送上述 control-plane API；COM 呼叫必須在擁有 watcher 的
worker thread 執行。未來 UI／IPC 若要暴露 session 控制，必須先建立新的 bounded ephemeral
session catalog／handle contract，不得把 raw instance ID 放進永久設定。

## 失敗與安全

- 未綁定 endpoint 回 `E_UNEXPECTED`；空字串、超長 ID、非有限、低於 −144 dB 或高於
  0 dB 回
  `E_INVALIDARG`。
- 不在目前 registry 的 ID 回 `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`，避免對新 endpoint
  或已消失的 session 寫入。
- event-context GUID 由 caller 提供，沿用 WindowsVolumeLink 的來源識別規則；callback
  不在此 API 中等待、配置或回呼 UI。
- registry refresh 後若 session instance 消失，所有後續讀寫必須 fail closed；新的
  session 必須重新枚舉並重新建立 ephemeral handle。

## 驗收

1. CTest 驗證 runtime／coordinator 未綁定時 read/write fail closed，並保留 caller 輸出值。
2. Windows source build 驗證 coordinator 只在 registry 命中後轉送 watcher；watcher 的
   COM 呼叫仍在 worker/control boundary，沒有 RT dependency。
3. Live session probe 只輸出 aggregate counts；不得輸出 endpoint ID、PID、display name
   或 raw session instance ID。真實 session volume read/write soak 要在目標 Windows 24H2
   machine 以明確測試 session 執行，未完成前不得宣稱 per-App control 已驗收。
