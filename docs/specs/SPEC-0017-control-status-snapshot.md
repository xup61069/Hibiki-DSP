---
id: SPEC-0017
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/include/hibiki/control_status.hpp", "src/hub/src/control_status.cpp", "src/hub/include/hibiki/control_service.hpp", "src/hub/src/control_service.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "apps/engine-preview/engine_preview.cpp", "apps/control-model/IpcProtocol.cs", "apps/control-model/EasyControlViewModel.cs", "tests/**"]
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

Windows control runtime 會在 worker/control thread 將同一預設 render endpoint 的
`WindowsAudioSessionRouteCoordinatorV1::snapshot()` 轉成保守路由健康資訊：session graph
準備完成才顯示 control-plane `Ready`，process-loopback 仍是 `Pending` 或 `Unavailable`，
並在說明中明示 physical delivery 未驗證。這條路徑不在 RT 或 COM notification callback
中執行，也不把 session enumeration 當成 per-App 重送。

Engine Preview 的 `main-output` route 預設只描述 physical catalog，不啟動 sink。只有明確
傳入 `--enable-wasapi-output` 時，control thread 才從 catalog 的 active default render
descriptor 建立 `WasapiOutputConfigV1` 並啟動既有 dedicated shared-mode sink worker；worker
狀態映射為 `Pending/Ready/Degraded`。這條 opt-in 只證明 user-space WASAPI output boundary，
沒有 graph source block 時 sink 維持安全 silence，不得把 route `Ready` 說成完整播放、WaveRT
driver 或 per-App DSP delivery。

只有同時傳入 `--enable-wasapi-output --enable-test-tone` 時，Engine Preview 才會建立最小 `main`
graph，並在 control thread 以 bounded 440 Hz、約 -20 dBFS sine block 經 graph、limiter 與 WASAPI
handoff 送出。sink snapshot 的 `rendered_blocks` 大於零後，`main-output` detail 會顯示
`test tone rendering.`。此旗標永遠是 opt-in；它只證明 user-space 可聽路徑到 WASAPI sink worker，
不構成真實裝置 delivery 或 WaveRT driver 證據。

## 失敗／fallback

- unknown type、錯誤長度、非零 padding、非法 UTF-8、重複 route ID、過期 sequence 或 unsafe
  volume 一律拒絕。
- status store 沒有 snapshot 時回 Error；UI 保留上一個狀態並顯示控制狀態暫不可用。
- status snapshot 只描述 control-plane truth，不代表 physical per-App audio delivery、
  Chrome tabCapture、WaveRT driver 或 process-loopback runtime 已可用。
- WASAPI opt-in 沒有支援的 active endpoint、格式不符、worker bind/start 失敗或 device
  invalidation 時，`main-output` 不得回報 `Ready`；應保持 `Pending`／`Degraded` 並 fail-closed。

## 驗收

1. C++／C# payload round-trip、Q16.16 volume、route flags、reserved bytes、duplicate IDs 與
   stale store publish 都通過 contract/control-model checks。
2. named-pipe handler 能以 request ID 回覆 ControlStatusSnapshot；未掛載 store 回 Error。
3. ViewModel 對完整快照採 atomic-style validation；malformed／stale snapshot 不改變可見狀態。
4. local Windows probe 驗證 default endpoint session route summary 會進入 status snapshot，
   route IDs 固定、說明不洩漏 endpoint/session identity，且 `physical delivery unverified`
   邊界保留。
5. Engine Preview normal path 維持 sink disabled；`--enable-wasapi-output -StatusOnly` 只在
   worker 回報 endpoint-ready 時呈現 `main-output=Ready`，且 smoke 不輸出 endpoint ID。
   `--enable-wasapi-output --enable-test-tone` 則只在 sink 回報 rendered block 後顯示
   `test tone rendering.`；此結果限於 user-space WASAPI path。
6. public repository 不包含編譯後 payload、driver、endpoint identity 或私人 calibration。

## 聆聽劑量指示器（UI 層附註）

ViewModel 在每次確認音量安全狀態後，把 EffectiveVolumeDb 樣本交給 UI 層的
`ListeningDoseModelV1` 累積。模型以「0 dBFS 滿刻度 ≈ 94 dBA」的保守換算推估耳側
音量，並以 85 dBA 基準與 3 dB 交換率折算自啟動起的劑量百分比；樣本間隔超過五分鐘時，
靜默區間不計入任何劑量（睡眠或暫停不會被算成持續暴露）。此指示器是未經校正的
本機參考值：不是音訊測量、耳機 SPL 校準、ISO 226 或聽力醫學證據；它不修改 IPC
schema、狀態快照格式或音訊路徑，且只存在於 UI 層記憶體。
