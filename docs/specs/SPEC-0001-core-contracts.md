---
id: SPEC-0001
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0001, ADR-0002]
source_globs: ["src/**", "schemas/**", "config/distribution-profile.yml"]
---

# SPEC-0001：Core contracts

## 成功條件

任何 App、ASIO client、瀏覽器 tab 或 input 都能以 stable Scene contract 指向
Lane、output group、channel map、DSP chain、reported plugin latency、latency mode 與安全策略；裝置替換不會
重生 endpoint、ASIO 或 IPC identity。

## 介面

- `SceneProfile v1`：lane routing、DSP、output group、automation、calibration reference；
  `automation_timeline_ids` 只保存 bounded VST3 timeline snapshot 的穩定引用，實際 worker
  執行與 plugin state 不內嵌於 Scene JSON。需要跨版本的 plugin state 時，只能以
  `scene-vst3-state-binding-v1` metadata reference 經 identity／version／migration preflight，
  opaque bytes 仍留在 private caller-owned store。
- `output_group` 在 graph compile 時進入 fixed-size RT snapshot；physical sink worker 可按
  群組呼叫指定 render，不得在 audio thread 以 `std::string` 或 map 查路由。
- `IrPhasePolicy v1`：minimum/mixed/linear/bypass 模式與 0..1 strength；只描述可驗證的
  額外延遲預算，不攜帶未授權 IR 或 ISO 係數。
- `AudioSessionDescriptor v1`：endpoint/session-instance identity、lane/output group、gain
  owner 與 per-session makeup dB；JSON schema 是跨語言／跨 AI 的欄位真值。
- `OutputGroupVolumeState v1`：requested/effective/safety dB、mute、generation、origin、actuator。
- `DistributionProfile v1`：driver hardware ID、endpoint GUID、ASIO CLSID、IPC namespace、schema version。
- Easy Scene factory：Game／Movie／Voice／Studio 先生成合法的 Scene、Graph 與 loudness
  defaults；Expert UI 可在此基礎上修改並重新走 Validate → Prepare → Commit。
- UI/engine/driver control plane 使用 versioned named-pipe framing；目前 user-space 提供
  `IpcFrameV1` little-endian envelope、payload 上限 1 MiB、request ID 與明確 decode errors。
  C++ `IpcNamedPipeServerV1` 以 4-byte little-endian length prefix、bounded overlapped I/O、
  單一 control callback 與 local-only pipe 實作 transport；C# `NamedPipeControlClientV1`
  使用同一 logical pipe name 與 request correlation。payload schema 可在後續以 Protobuf
  或等價固定編碼替換，但 version、message type 與 Validate/Prepare/Commit 語意不可破壞。
- `VolumeNotification` v1 payload 固定為 16 bytes：Q16.16 dB、mute、三個 reserved bytes
  與 uint64 generation；C++ `control_payloads.hpp` 與 C# `ControlPayloadsV1` 必須保持相同
  little-endian bytes，超出 -144..12 dB 或 reserved 非零一律拒絕。
- C++ `decode_control_command_v1` 只接受 Hello、VolumeNotification、GraphCommit 與
  GraphRollback、SceneApply、DeviceSwitch request；Ack/Error 只能作 response，未知或 GraphPrepare 未定義
  payload 一律回 Error，避免 UI 任意注入未驗證 graph。SceneApply payload 固定 64 bytes，
  以兩段 length-prefixed printable UTF-8（scene ID、output group）及 zero padding 表示。
- `handle_control_frame_v1` 是 pipe worker 到 host control queue 的唯一 typed adapter；sink
  必須自行 enqueue／排程，不能在 pipe callback 直接跑 RT DSP 或等待 UI/COM。
- `ControlCommandQueueV1` 是目前的固定 64-slot SPSC handoff；滿載時丟棄新命令並增加
  dropped counter，consumer 才能呼叫 AudioEngine／graph transaction。這個 queue 不保證
  multi-producer；若未來有第二個 control producer，必須先建立新的版本化協定。
- `EngineControlWorkerV1` 是目前的單一 consumer：它將 `SceneApply` 解析為四個受控 Easy
  preset，執行 `prepare_graph` → `commit_graph`，失敗則 rollback；`VolumeNotification`
  同樣在 control worker 套用，pipe callback 只負責 validate、enqueue、回 ACK。
- `DeviceSwitch` v1 使用固定 288-byte little-endian payload：uint16 endpoint ID bytes、
  260-byte zero-padded UTF-8 endpoint ID、channels、sample rate、buffer frames 與 catalog
  sequence。control worker 只能交給明確註冊的 device-switch handler；handler 必須先透過
  `PhysicalDeviceCatalogV1`／`DeviceRecoveryCoordinator` 驗證，再排程 sink handoff。沒有
  handler 或 payload／catalog 驗證失敗時 fail-closed，不會假裝裝置已切換。
- `EngineControlWorkerV1::set_scene_preflight` 可注入一個 control-plane-only gate，讓 VST3
  state coordinator、校正資料或安全策略在 graph Prepare 前驗證；gate 失敗會保留既有
  Scene、revision 與 active graph，不會部分套用。
- `AudioSessionRegistry` 以 `endpoint_id + session_instance_id` 作為唯一 session key；PID
  只作顯示／診斷用途。OS metadata refresh 不得覆蓋使用者已選 lane、output group 或 gain
  owner，避免同一 process 的多個 Chrome tab／session 互相串音。
- `SessionRouteGraphBuilderV1` 是 registry 到 immutable graph 的唯一控制面轉換點；每個
  active bound session 生成一個 lane，並在 Validate → Prepare → Commit 後才可進 RT。
- Windows `IAudioSessionManager2` adapter 的 `OnSessionCreated` callback 只遞增 sequence；
  worker 才呼叫 enumerator、讀取 instance/session ID、PID、display name 與 active state，
  再 upsert registry。這個邊界禁止在 OS callback 裡 QueryInterface、分配或改寫 graph。
- Session volume 的 canonical control 以 dB 表示，worker 呼叫 `ISimpleAudioVolume` 時才轉
  成 0–1 scalar，所有寫入帶 event-context GUID 並 read-back；session API 只適用 shared-mode，
  exclusive／vendor ASIO 仍標示 bypass。

## 不變條件

- RT thread 不 allocation、lock、wait 或呼叫 UI/COM/filesystem。
- graph change 必須 Validate → Prepare → Commit；失敗回復 last-known-good。
- output device change 必須與 30 ms equal-power handoff 同一交易；`OutputHandoffCoordinatorV1`
  在 crossfade 尚未完成時禁止 commit，新 sink 失敗時保留舊 sink。實體 endpoint soak 仍是
  環境驗收項目。
- Group Master 只能套用一次；LFE 不重複套用 ISO。
- Strict Direct 是獨立 bit-perfect Scene，不能偷偷混入 DSP 或 Windows gain。
- `AudioEngineModel` 的 control plane 只在 pending snapshot 做 Validate → Prepare → Commit；
  RT `process` 只讀 active immutable snapshot，再對整個 Group Master 套用一次。Volume
  control object 不會由 RT 讀取；引擎只透過單一 release/acquire 64-bit word（Q16.16
  effective-dB + mute bit）聯動 Windows，避免 control worker 與 audio thread 形成 data race
  或讀到不一致的 dB/mute pair。
- Group Master 的 RT ramp 以 dB-domain 執行：一般 target 8 ms、mute 5 ms、unmute 15 ms；
  sample rate 由 control plane 設定（預設 48 kHz），ramp state 由唯一 audio thread 擁有，
  不配置、不鎖、不等待。
- 最後一級以固定 8-channel bounded inter-sample peak guard 對非 Strict Direct graph 套用
  −1 dBTP ceiling；非有限 sample 先 fail-safe 變成 0，Strict Direct 不套 limiter。

## 相容性與驗收

Schema 使用明確 `schema_version`；新增欄位必須向後相容，破壞性變更建立新版本與 migration。用
合成 fixture 測試 2.0、5.1、7.1 與裝置切換。
