---
id: SPEC-0019
status: accepted
owner: hibiki-maintainers
authority: control-plane
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/include/hibiki/session_catalog.hpp", "src/hub/src/session_catalog.cpp", "src/hub/include/hibiki/windows_audio_session_route.hpp", "src/hub/src/windows_audio_session_route.cpp", "src/hub/include/hibiki/windows_device_catalog.hpp", "src/hub/src/windows_device_catalog.cpp", "src/hub/include/hibiki/control_service.hpp", "src/hub/src/control_service.cpp", "apps/control-model/SessionCatalogModel.cs", "apps/control-model/IpcProtocol.cs", "apps/control-model/EasyControlViewModel.cs", "tests/unit/contract_tests.cpp", "apps/control-model-check/Program.cs"]
---

# SPEC-0019：App 工作階段 catalog 與暫時 handle 邊界

## 成功條件

控制面可以列出目前已枚舉的 Windows audio session，讓 Easy／Expert UI 選擇 App、
顯示路由摘要與 per-session volume 可用性；線上資料不得洩漏 raw endpoint ID、PID 或
Windows session-instance identifier。每次路由 generation 變更都產生新一批暫時 handle，
舊 handle 不可跨 refresh 使用。

本規格只建立「可安全選取」的 catalog，不宣稱已完成 per-App 重送、Chrome tabCapture、
vendor ASIO 攔截或實體音量控制。per-session `ISimpleAudioVolume` 的 worker API 仍受
SPEC-0018 約束。

AudioSessionDescriptorV1 的 `output_group` 上限為 64 bytes（與 scene graph 和 volume
bank 的 canonical bound 一致）；超過上限的 descriptor 會在 schema 驗證與
AudioSessionRegistry::upsert()/bind() 時 fail-closed。session-route wire command 的
48-byte 上限是更窄的傳輸層邊界。

## Wire v1

`SessionCatalogSnapshot` 固定 little-endian header 24 bytes、entry 256 bytes、最多 32 筆。
header 帶 `sequence` 與目前 route `generation`；entry 帶 64-bit ephemeral `handle`、active、
route state、volume availability、量化 dB／mute 及 bounded UTF-8 的名稱、App、Lane、output
摘要。所有 reserved bytes 必須為零、handle 不可重複、字串超限／控制字元／非法 UTF-8
會拒絕整個 frame；Windows metadata 過長時 runtime 以空字串 fallback，不把不可信文字
帶進 UI。

`SessionCatalogRequest` 使用空 payload。control host 沒有已發布 snapshot 時回 Error，
不可假裝成功回傳空清單。request／reply 必須保留 request ID。

## Handle 與生命週期

runtime 以 `(generation << 32) | (registry index + 1)` 產生 handle。generation 為零、
index 超過 catalog capacity 或 refresh 失敗時不發布 snapshot。handle 只適用於回覆中的
sequence／generation；永久 Scene、profile、calibration 與 log 不得保存它。任何後續寫入
命令必須重新以目前 catalog 驗證 handle，不能由 UI 自行還原 raw identity。

## UI／失敗行為

- C# decoder 先完整驗證 frame，再以單次 immutable list swap 更新 `SessionCatalog`。
- sequence 倒退、錯誤型別、格式錯誤或 Error reply 都保留上一份可見清單並顯示繁中降級
  狀態；不清除音訊 lane，也不改變 RT graph。
- catalog refresh 與 control status、device catalog 共用 serialized control gate；COM／
  Windows session enumeration 不得在 UI thread 或 RT thread 執行。
- `DisplayName` 僅使用安全名稱或 App fallback；Accessible summary 不包含 PID、endpoint
  ID 或 raw session-instance ID。

## 驗收

1. C++／C# codec round-trip、reserved／duplicate／invalid UTF-8／capacity guard 與
   request correlation 通過；store 只接受嚴格遞增 sequence。
2. runtime live probe 只輸出 aggregate catalog count、generation、active count 與 bound
   狀態；不輸出私人 session identity。
3. ViewModel 可原子套用 snapshot，拒絕 stale／malformed frame 並保留先前清單；未連線
   refresh fail closed。
4. 五來源實體同播、每個 App 的實際重送、Chrome tabCapture 與 session volume soak 仍需
   目標 Windows 11 24H2 測試機另立 evidence；在完成前不得宣稱 per-App UX 已驗收。
