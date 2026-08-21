---
id: SPEC-0021
status: accepted
owner: hibiki-maintainers
authority: control-plane
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/include/hibiki/control_payloads.hpp", "src/hub/src/control_payloads.cpp", "src/hub/include/hibiki/ipc.hpp", "src/hub/src/ipc.cpp", "src/hub/include/hibiki/engine_control.hpp", "src/hub/src/engine_control.cpp", "src/hub/include/hibiki/windows_audio_session_route.hpp", "src/hub/src/windows_audio_session_route.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "apps/control-model/IpcProtocol.cs", "apps/control-model/EasyControlViewModel.cs", "tests/unit/contract_tests.cpp", "apps/control-model-check/Program.cs"]
---

# SPEC-0021：以暫時 handle 套用 per-App lane/output route

## 成功條件

目前 catalog 的 App 可以送出 lane ID 與 output group 要求。命令只帶 generation-scoped
handle、catalog sequence 及 bounded printable UTF-8 目標；coordinator 驗證 handle／sequence
後，以候選 `AudioSessionRegistry` 建立新的 immutable route graph，成功才 commit。任何
未知、過期、未綁定、非法 label 或 graph build 失敗都保留原 graph。

這是 route graph control boundary，不等於已完成實體 process-loopback source、Chrome
tabCapture、vendor ASIO 攔截或 Windows 端點重送。實際交付仍由 SPEC-0013／0016 與硬體驗收
負責。

## Wire v1

`SessionRouteCommand` 固定 128 bytes little-endian：handle 0..7、catalog sequence 8..15、
lane/output 長度 16/17、reserved 18..19、lane UTF-8 20..67、output UTF-8 68..115、尾端
reserved 116..127。lane 與 output 各最多 48 bytes、不可為空；所有未使用 bytes 必須為零。
decoder 先完整驗證，不得部分寫入控制命令。

## Transaction 與生命週期

coordinator 先複製目前 registry，依 handle 指向的 session identity 套用 lane/output，重新
計算 active routed count 與 route graph；只有候選 graph 成功才交換 registry／graph。成功後
generation 遞增，舊 handles 立即失效，runtime 發布新的 route status 與 SessionCatalogSnapshot。
這個 transaction 不在 RT callback 執行，也不保存 raw session ID 到 Scene 或 profile。

## UI／失敗行為

- C# ViewModel 只能從目前 `SessionCatalog` 建立 route command，刷新後 handle 消失會清除選取。
- EngineControlWorker 透過 `SessionRouteHandlerFnV1` 將命令交給 Windows worker；沒有 handler
  時回報 Failed，不假裝路由已套用。
- command 失敗保留既有 catalog、volume、graph 與輸出；成功後 UI 必須等待下一個 catalog／
  status snapshot 才把新 generation 顯示為可用。

## 驗收

1. C++／C# fixed 128-byte round-trip、padding／UTF-8／capacity rejection、request correlation
   與 EngineControl callback 通過。
2. Windows coordinator/runtime unbound route command fail closed；target Windows 24H2 需另以
   真實 active session 驗證 commit／rollback、process-loopback delivery、拔插與 Audio Service
   restart。
3. 本規格只承諾 graph candidate boundary；五來源同播與每個 App 實際輸出仍不得標記為已驗收。
