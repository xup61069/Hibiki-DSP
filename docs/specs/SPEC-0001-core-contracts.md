---
id: SPEC-0001
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-25
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
  scene `id` 是 bounded 穩定識別碼（1..31 bytes；字元集為小寫英數加上 `.` `_` `-`，
  首字元必須是小寫英數）。JSON schema、engine catalog validator 與 SceneApply wire
  format 對同一欄位套用相同上限，過長或格式不合法的 id 在檔案載入時即被拒絕。
  執行與 plugin state 不內嵌於 Scene JSON。需要跨版本的 plugin state 時，只能以
  `scene-vst3-state-binding-v1` metadata reference 經 identity／version／migration preflight，
  opaque bytes 仍留在 private caller-owned store。
  `lanes` 是 bounded ID 陣列（最多 32 個；每個名稱 1..64 bytes、非空且不含控制字元），
  JSON schema 與 runtime validator 會一致地拒絕超界或格式不合法的 lane 名稱。
  `ir_reference` 是 bounded UTF-8 calibration label（空值或 8..64 bytes，不含 NUL 與
  C0/C1 控制字元；schema 與 runtime validator 一致拒絕），
  用來比對「同一份已準備的 IR」。它只是 opaque 比對 token：不內嵌 IR samples、檔案路徑或
  equal-loudness 係數，也不代表任何實體播放或 driver 證據。SceneApply 在新場景帶有與 active scene
  完全相同的非空 `ir_reference` 且 output group 不變時，可保留已 commit 的 IR attachment；
  其他情況（不同 label、空 label、無 active attachment 或 IR 交易進行中）維持原本在同一個
  control transaction 內 detach 的 fail-closed 行為。
- `output_group` 在 graph compile 時進入 fixed-size RT snapshot；physical sink worker 可按
  群組呼叫指定 render，不得在 audio thread 以 `std::string` 或 map 查路由。
- `GraphConfig v1` 的 `sample_format` 欄位選擇 render 樣式格式：`0` = float32
  （既有預設）、`1` = float64；未知值在 validate_graph、compile_rt_snapshot 與 RT
  process 入口一律 fail-closed。`process_graph_f64`／`process_graph_for_output_group_f64`
  接受 interleaved double input，以 double 累加並寫出 interleaved double output；
  所有 graph process entry point 在初始化 caller-owned output 或計算 lane stride 前，
  必須先以不溢位的方式驗證 `frames * channels`；graph 支援的 lane/output 聲道上限為
  8，無法由 `size_t` 表示的 frame geometry 一律 fail-closed。這是 user-space
  buffer-safety boundary，不新增實體 endpoint 或 driver delivery 保證。若 float graph
  entry point 帶入 latency bank，還必須在初始化 caller-owned output 前拒絕超過
  `kLaneLatencyMaxFramesV1`（4096）的 block，因為 plugin-latency compensation 的
  fixed scratch 只支援這個 frame 上限；f64 entry point 不帶入此 float latency bank，
  不受這項額外上限影響。
  float32 API、snapshot 配置與 JSON fixture 行為不變。v1 f64 邊界不含 plugin latency
  bank（其 ring 為 float32）；需要 plugin 延遲補償時，呼叫端必須先在上游 double domain
  完成後再進入此路徑。此格式僅描述 user-space engine 內部累加精度，不是 WASAPI、
  driver、WaveRT 或 ASIO 的 64-bit delivery 證據。
- `AudioEngineModel::process_f64` 與 `process_output_group_f64` 是 model 級 bounded
  double entry points：沿用同一份已 commit 的 immutable graph 與 Group Master 邊界；
  無 active graph 或 sample format 非 0/1 時 fail-closed。與低階 `process_graph_f64`
  的差別在於 model 級入口自動套 Group Master ramp；v1 不呼叫最後一級 TruePeakLimiter，
  呼叫端必須自行確保 double output 的 peak safety。Group Master 的 control state 由
  既有 volume notification API 管理；volume_state() 只回傳目前 reconciled snapshot，
  不改變音訊狀態。
- `IrPhasePolicy v1`：minimum/mixed/linear/bypass 模式與 0..1 strength；只描述可驗證的
  額外延遲預算，不攜帶未授權 IR 或 equal-loudness 係數。
- `AudioSessionDescriptor v1`：endpoint/session-instance identity、lane/output group、gain
  owner 與 per-session makeup dB；JSON schema 是跨語言／跨 AI 的欄位真值。
  `display_name`／`app_id`／`lane_id` 是有界 optional labels：schema 允許空字串，但最長
  256 字元，與 runtime `kMaxLabelLength` 一致。所有文字欄位（identity、display_name、
  app_id、lane_id、output_group）拒絕控制字元與非可印 UTF-8；schema 使用 anchored pattern，與 runtime
  `AudioSessionRegistry::valid()` 使用相同的 fail-closed 契約。
- `OutputGroupVolumeState v1`：requested/effective/safety dB、mute、generation、origin、actuator。
- `DistributionProfile v1`：driver hardware ID、endpoint GUID、ASIO CLSID、IPC namespace、schema version。
  `config/distribution-profile.yml` 是唯一 canonical source；`tools/distribution-check.ps1` 以
  bounded YAML subset 解析並對 root/platform/identities 做 duplicate、unknown-key、indentation、
  schema/platform、GUID uniqueness 與 stable-identity fail-closed 檢查。這個 source-only gate
  不會重生 identity，也不是 runtime consumer、installer 或 driver evidence。
- Easy Scene factory：Game／Movie／Voice／Studio 先生成合法的 Scene、Graph 與 loudness
  defaults；Expert UI 可在此基礎上修改並重新走 Validate → Prepare → Commit。
- UI/engine/driver control plane 使用 versioned named-pipe framing；目前 user-space 提供
  `IpcFrameV1` little-endian envelope、payload 上限 1 MiB、request ID 與明確 decode errors。
  C++ `IpcNamedPipeServerV1` 以 4-byte little-endian length prefix、bounded overlapped I/O、
  單一 control callback 與 local-only pipe 實作 transport。canonical 單一擁有者服務可要求
  first-instance ownership：`start` 必須同步建立第一個 server handle，取得失敗時回 false，
  後續 server-side recreate 也保留同一 ownership；`stop` 必須在 worker 觀察停止前重複取消
  當下註冊的 server handle I/O，涵蓋 connected idle 與兩次連線之間的空檔，不得讓關閉流程
  固定等待一個完整 idle timeout。Engine Preview 的 canonical control pipe
  採用此 fail-closed 邊界。C# `NamedPipeControlClientV1`
  使用同一 logical pipe name 與 request correlation。payload schema 可在後續以 Protobuf
  或等價固定編碼替換，但 version、message type 與 Validate/Prepare/Commit 語意不可破壞。
- `VolumeNotification` v1 payload 固定為 16 bytes：Q16.16 dB、mute、三個 reserved bytes
  與 uint64 generation；C++ `control_payloads.hpp` 與 C# `ControlPayloadsV1` 必須保持相同
  little-endian bytes，超出 -144..12 dB 或 reserved 非零一律拒絕。
- C++ `decode_control_command_v1` 只接受 Hello、VolumeNotification、GraphCommit 與
GraphRollback、SceneApply、DeviceSwitch、DeviceCatalogRequest、ControlStatusRequest、
SessionCatalogRequest、SessionVolumeCommand、SessionRouteCommand、SessionRouteRuleCommand、
IrPrepareCommand、SceneCatalogCommand 與 EqVisualSnapshotRequest request；Ack/Error、
DeviceCatalogSnapshot、ControlStatusSnapshot、SessionCatalogSnapshot 與 EqVisualSnapshot
只能作 response，未知或 GraphPrepare 未定義
payload 一律回 Error，避免 UI 任意注入未驗證 graph。SceneApply payload 固定 64 bytes，
以兩段 length-prefixed printable UTF-8（scene ID、output group）及 zero padding 表示。
- `handle_control_frame_v1` 是 pipe worker 到 host control queue 的唯一 typed adapter；sink
  必須自行 enqueue／排程，不能在 pipe callback 直接跑 RT DSP 或等待 UI/COM。
- `ControlPlaneHostV1` 是 host 的組合入口：它擁有 named-pipe server 與 64-slot queue，將
  `handle_control_frame_v1` 綁定到同一個 context；`start_with_queue` 只把命令排入 queue，
  `EngineControlWorkerV1` 仍必須在自己的 control thread drain。若沒有 snapshot store，
  `DeviceCatalogRequest` 必須回 Error；host stop 先停止 pipe worker，再清除 callback context。
- Windows `WindowsControlRuntimeV1` 進一步把 endpoint catalog service 與 `ControlPlaneHostV1`
  綁在同一個非 RT runtime；`start` 只負責 COM enumerator/service 與 pipe 綁定，
  `refresh_now`／`poll_and_refresh` 由 COM-initialized worker 呼叫，engine control thread
  透過 `command_queue()` 消費命令。任何未綁定或停止狀態都回傳 `E_UNEXPECTED`／false。
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
- `DeviceCatalogSnapshot` v1 是 engine → UI 的 bounded response/broadcast：16-byte header、
  每筆 416-byte entry、最多 32 筆；它不是 audio command，C++ command decoder 不會把它
  排入 RT queue。UI 可送出空 payload 的 `DeviceCatalogRequest`，control service 由明確
  snapshot-reply provider 回傳 snapshot；沒有 provider 時回 Error。UI worker 必須驗證
  snapshot 後 atomic replace catalog，過期快照保留舊值。
- `EngineControlWorkerV1::set_scene_preflight` 可注入一個 control-plane-only gate，讓 VST3
  state coordinator、校正資料或安全策略在 graph Prepare 前驗證；gate 失敗會保留既有
  Scene、revision 與 active graph，不會部分套用。
- `AudioSessionRegistry` 以 `endpoint_id + session_instance_id` 作為唯一 session key；PID
  只作顯示／診斷用途。OS metadata refresh 不得覆蓋使用者已選 lane、output group 或 gain
  owner，避免同一 process 的多個 Chrome tab／session 互相串音。
- `SessionRouteGraphBuilderV1` 是 registry 到 immutable graph 的唯一控制面轉換點；每個
  active bound session 生成一個 lane，並在 Validate → Prepare → Commit 後才可進 RT。
- Windows `IAudioSessionManager2` adapter 的 `OnSessionCreated` callback 只做 bounded、
  reference-counted control-pointer retention 與 sequence signaling；這個 retention 只把
  callback 收到的 control ownership 交給 worker 做後續 enumeration，不是 sample delivery
  或 session-volume write success。worker 才呼叫 enumerator、讀取 instance/session ID、PID、
  display name 與 active state，再 upsert registry。這個邊界禁止在 OS callback 裡
  QueryInterface、讀取 metadata、分配、等待或改寫 graph；RT audio thread 同樣不得配置、
  等待或呼叫 COM/UI/檔案系統。
- Session volume 的 canonical control 以 dB 表示，worker 呼叫 `ISimpleAudioVolume` 時才轉
  成 0–1 scalar，所有寫入帶 event-context GUID 並 read-back；session API 只適用 shared-mode，
  exclusive／vendor ASIO 仍標示 bypass。

## 不變條件

- RT thread 不 allocation、lock、wait 或呼叫 UI/COM/filesystem。
- graph change 必須 Validate → Prepare → Commit；失敗回復 last-known-good。
- output device change 必須與 30 ms equal-power handoff 同一交易；`OutputHandoffCoordinatorV1`
  在 crossfade 尚未完成時禁止 commit，新 sink 失敗時保留舊 sink。實體 endpoint soak 仍是
  環境驗收項目。
- Group Master 只能套用一次；LFE 不重複套用 equal-loudness。
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
`AudioSessionDescriptor v1` 的 schema-instance 驗收必須載入 repository 內的實際 schema，確認
六個文字欄位對 U+0000–U+001F、U+007F–U+009F 在開頭、中間、結尾及 controls-only 位置皆
fail-closed，同時保留 printable UTF-8 與 optional 空字串；只跑 runtime CTest 或只檢查 pattern
文字不算 schema/runtime parity evidence。
