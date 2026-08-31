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
Snapshot sequence 必須非零；零值保留給未初始化／無 freshness 狀態，C++／C# encode 與
decode 都必須在任何 visible-state replacement 前拒絕它。這個規則只描述 user-space
control-plane snapshot 的有效性，不代表 physical audio、driver 或 WaveRT delivery。
Route entry 保存 bounded printable UTF-8 ID／名稱／說明、`Ready/Pending/Degraded/Bypassed/Unavailable`
狀態與 requires-user-action flag。所有 padding 必須為零，ID 不可重複，effective dB 不可高於
requested 或 safety ceiling。v1 的 wire 上限固定為 ID 1..31 bytes、名稱 1..63 bytes、說明
1..119 bytes；上限以 UTF-8 encoded bytes 計算，不是 C# UTF-16 code units，且 isolated
surrogate、C0/C1 control、DEL 與非法 UTF-8 一律拒絕。

`ExpertSurfaceModel.TryApplyRouteHealth` 仍可在 control-model 內保留最多 16 張 route cards，
這是刻意保留給本地診斷聚合與未來狀態投影的 control-plane 容量，不會改變 v1 snapshot 的
8-entry wire 上限。第 9..16 張卡片只能代表本地模型狀態，不能被宣稱可裝入單一 v1
`ControlStatusSnapshot`；若要傳輸，必須由較新的版本化協定另行定義。

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
driver 或 per-App DSP delivery。啟用後 main-output 的 name/detail 前綴目前綁定 default
render endpoint 的 bounded display name（例如「主輸出 — Speakers」與
`Speakers: shared-mode WASAPI sink ready...`）；display name 由 physical device catalog
提供並在 route entry 容量內截斷，smoke 與診斷輸出仍不得包含 endpoint ID。

WAV source route 的 `frames=` 進度只計算實際複製進目前 bounded block 的 source frames；
loop wrap 從檔案尾端回到 frame zero 時仍增加正確的 copied-frame 數，不得以 reset 後的
unsigned frame subtraction 產生下溢。此為 user-space 狀態診斷，不代表 physical delivery。

device-free offline WAV render 以 128-frame bounded block 處理完整 decoded source；每個 block
的 lane view 必須從目前 source-frame offset 開始，mono 擴展後亦同。output cursor 與 input
cursor 必須同步前進，不得把第一個 block 的 pointer 重複傳給後續 graph calls。

WAV source 進入重採樣路徑時，只有在 bounded polyphase conversion 產生足夠且全部為 finite
的輸出後，才可將精確的 `nominal * channel_count` samples commit 回 decoded source；
更新 sample rate metadata 前不得只 resize destination 而遺留原始 prefix 或 zero tail。
這份已 commit 的 sample buffer 同時供 live WASAPI source 與 device-free offline render 使用，
same-rate source 不經過此轉換。

明確要求的 WAV source 若檔案、sink、格式或 graph setup 失敗，route health 為
`Unavailable` 且 `requires-user-action` 為 true；已完成 setup 但尚未成功 render 第一個 block
才是 `Pending`，成功 render 後才是 `Ready`。這些狀態只描述 Engine Preview 的 user-space
生命週期，不代表 physical audio delivery。

Process Loopback、Driver Stream Loopback、browser-tab 與 WAV source route 只有在目前
WASAPI handoff 為 `Synced`，且 active sink worker 同時回報 running、endpoint-ready 且未
degraded 時，才可把成功 render 的 source 顯示為 `Ready`。handoff 或 active worker 進入
`Degraded` 時，source route 也必須 fail-closed 為 `Degraded`；worker 停止或尚未
endpoint-ready 則保留 `Pending`。`rendered_blocks` 等累積計數只作診斷總量，不作目前
sink liveness 證據。

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
靜默區間不計入任何劑量（睡眠或暫停不會被算成持續暴露）。累積視窗以樣本自帶時區的
本地日期為準，跨日（當地午夜）後自動從下一個有效樣本重新開始；前一日劑量不會計入
新的一天。最近一筆有效樣本為靜音時，UI 顯示「劑量暫停累積」提示，讓使用者分辨
刻意靜音與本來就安全的音量；恢復未靜音樣本後提示清空並從新時間錨點繼續。此指示器是未經校正的
本機參考值：不是音訊測量、耳機 SPL 校準、等響度（equal-loudness）測量證據或聽力醫學證據；它不修改 IPC
schema、狀態快照格式或音訊路徑，且只存在於 UI 層記憶體。
