---
id: SPEC-0016
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/include/hibiki/windows_process_loopback.hpp", "src/hub/src/windows_process_loopback.cpp", "src/hub/include/hibiki/windows_process_loopback_lane.hpp", "src/hub/src/windows_process_loopback_lane.cpp", "tests/**"]
---

# SPEC-0016：Windows process-loopback capture boundary

## 成功條件

在 Windows 10 20H1 以上的支援環境，control-plane worker 可用官方
`ActivateAudioInterfaceAsync` 建立指定 process（可選含子程序樹）的 shared-mode loopback
capture。成功後只輸出 caller-owned Float32 interleaved block，並回報實際 sample rate、聲道數、
buffer period 與累計 frame；停止、timeout、格式不符或 WASAPI 失效都進入明確狀態，不能把
未捕獲的資料標成已送入 Lane。

這是 process-level source，目的是讓每個 Windows App/session 有一條可驗證的 capture 邊界；
它不等於 Chrome／Edge 單分頁，單分頁仍必須由使用者點擊 MV3 `tabCapture` 擴充功能提供，
也不等於把原生 process audio 靜默重導到任意實體 endpoint。

## 介面與執行緒

- `WindowsProcessLoopbackSourceV1::start`、`stop` 與 `snapshot` 只在 owning worker/control
  thread 呼叫；該 thread 必須先初始化 COM。
- `read` 非阻塞，一次最多處理一個 WASAPI packet；`frames_read==0` 的成功代表目前沒有
  packet。caller 必須提供足夠容量；不足時 packet 被明確丟棄並增加 `dropped_frames`。
- RT graph 不呼叫 COM、事件等待、配置或 process ID 查詢；它只接收已驗證、caller-owned 的
  Float32 block，並沿用既有 Lane／Group Master／limiter transaction。
- process ID 只作即時 activation target，不進 Scene/Profile 或永久 identity；持久路由仍使用
  session instance／endpoint 與規則 store 的語意。
- `process_windows_process_loopback_lane_v1` 只在 source snapshot 仍為 `Running` 且格式未變時
  把一個 caller-owned block 送進現有 Lane graph；`to_wasapi` 只是重用既有 handoff，不會在
  adapter 內偷偷建立或切換實體 endpoint。

## 失敗／fallback

- `process_id==0`、非 Float32 mix format、requested format 不匹配、activation timeout 或
  `IAudioClient`／`IAudioCaptureClient` 任何 HRESULT 失敗都回報 `Degraded` 與最後錯誤碼。
- source 失效時不重試、不自動改 Windows Master，也不把舊 block 重播；上層可依裝置／session
  generation 重新建立 source。
- process loopback 無法提供 Chrome tab identity；瀏覽器 tab bridge 的使用者手勢、權限與
  extension session ID 必須另行驗收。

## 安全與相容性

- activation 使用 Windows 官方 `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`，不依賴未公開
  per-App routing COM 介面，不反編譯或繞過其他程式保護。
- source 不保存 endpoint ID、顯示名稱、PID 或 session ID 到公開 evidence；只可記錄匿名狀態、
  frame count 與格式。
- v1 只接受 shared-mode Float32；其他格式、WASAPI Exclusive、vendor ASIO 與 RAW 路徑
  仍明示為繞過 Hibiki 或需另建 adapter。

## 驗收

1. contract test 驗證零 process ID fail-closed、Degraded snapshot、stop 不恢復成 Ready，
   且無效 source 不可讀取 block。
2. `pwsh -File tools/live-process-loopback-check.ps1` 會在本機建立短暫 render tone，嘗試以
   current process 作為 include-tree target，只輸出匿名格式／frame aggregate；沒有可用的
   process-loopback runtime 時回報 `loopback=unavailable` 並保留 source-only 結果。
3. 目標 Windows 11 24H2 clean machine 以有音訊的測試程序啟動 source，驗證 include／exclude
   process tree、實際 Float32 frame、buffer overflow drop、停止與 Audio Service restart
   recovery；本機沒有注入的 process-loopback fixture 時只能記錄 source compile，不得宣稱完成。
4. process-loopback block 進入 Lane 後，使用既有 graph／Group Master／safety／WASAPI handoff
   驗證單次增益、錯誤聲道 fail-closed 與與其他來源不串音。
