---
id: SPEC-0008
status: accepted
owner: hibiki-maintainers
authority: architecture
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["vst-host/**", "tests/**"]
---

# SPEC-0008：VST3 隔離程序與 quarantine

VST3 plugin 不得在 Hibiki RT thread 或主 UI process 內直接執行。control plane
啟動獨立 worker，worker 進入 Windows Job Object；Job 結束時一併回收子程序。

## 啟動與生命週期

- `Vst3SandboxProcess::launch` 只接受明確的 worker executable、plugin path 與
  1–5000 ms watchdog；失敗狀態為 `Quarantined`。
- Windows target 使用 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，worker crash、建立失敗
  或 heartbeat 超時都 quarantine；不自動把同一 plugin 無限重啟。
- `Vst3SandboxLaunchV1.worker_pipe_name` 若提供，supervisor 在建立 process 前建立 named
  pipe server，command line 傳入 `--hibiki-pipe`；`wait_for_worker`、`send_worker_frame`、
  `receive_worker_frame` 只在 control/IPC thread 呼叫，pipe 失敗不得由 audio callback 重試。
- `mark_heartbeat` 與 `poll_watchdog` 只在 control/IPC worker 呼叫，禁止 audio callback
  使用 Win32 handle 或等待。
- `Vst3SandboxLaunchV1` 的 `vst3_class_id`、`vst3_sample_rate`、`vst3_channels` 是 optional
  SDK-worker launch fields；空 class UID 維持原有 passthrough worker。非空時必須是有效
  sample rate 與 2/6/8 channels，supervisor 才會把 module/class/rate/channel 參數傳給
  `hibiki_vst3_sdk_worker`。共用 `validate_vst3_sandbox_launch_v1` 先拒絕不合法設定，不能
  由 child process 才發現格式錯誤。
- `PluginDescriptorV1.lane_token` 是 control plane 配發的 stable non-zero identity；缺少 token
  時 host 直接 quarantine。`PluginHostModel::latency_lane_input()` 只在 plugin 可處理時輸出
  active lane，供 SPEC-0012 的 latency graph commit 使用，避免 PID 或暫時 index 對錯延遲。

## Engine Preview VST3 lane 邊界

Engine Preview 提供 opt-in `--enable-vst3-lane`，把既有 sandbox worker 接進 RT 音訊路徑：
主輸出 graph 的 pre-VST3 tap 由 control loop 讀取（seqlock sequence 去重，同一 snapshot 不會
重複送進 worker），worker 處理後的 Float32 block push 回 lane ring，由 RT graph 的
apply_vst3_lanes 在 render 時 pop。此模式必須同時給 --vst3-module-path 與
--vst3-class-id，缺一即 fail-closed 拒絕啟動；--vst3-worker-path 可覆蓋預設的本地
SDK worker 位置。lane 狀態透過 control status snapshot 的 route slot vst3-lane 曝光：
armed（Pending）→ rendering with processed-block counter（Ready）→ setup 失敗時
Degraded。worker lane session 必須以 sink 的實際 block frames 準備；tap block 超過
prepared 上限時整個 exchange fail-closed 進入 Degraded，不得靜默截斷或丟棄。
`Vst3LaneRingBridgeV1` 的 push/pop 也共享一般 Process block 的 1–4096
frames 上限；即使 caller-owned ring 的實體容量更大，超過上限的 exchange
必須在讀取 sample 或前進 ring index 前拒絕。sample-count 與 ring capacity
算術必須以不溢位的方式驗證，失敗不得改變既有 ring 內容或可用 frame 計數。
WAV source 準備失敗時必須輸出明確錯誤，不得讓下游（例如 VST3 tap）呈現為
「無聲音可處理」的假正常。本邊界屬 user-space engine preview evidence，不等於 driver/WaveRT、實體
音訊 delivery 或第三方 plugin certification。

## Worker IPC frame

`vst3_worker_protocol.hpp` 定義固定 36-byte little-endian frame header：Hello、HelloAck、
Heartbeat、ProcessBlock、ProcessBlockResponse、Shutdown、Error，以及不破壞既有 bytes 的
`ProcessBlockWithParameters`。一般 Process frame 僅接受 2/6/8 聲道、1–4096 frames、exact
interleaved Float32 payload；參數 frame 使用 `[u32 count][u32 reserved][最多 64 個 16-byte
point][Float32]`，每個 parameter 最多 5 點、最多 16 個 parameter，並拒絕 NaN/Inf、非法
offset／normalized 值。codec 不配置、不擁有 payload，適合由 named pipe 或其他 control
transport 在 worker 與 supervisor 間傳遞。真正 VST3 SDK/plugin dispatch 仍必須在 worker
process，不能連入 graph RT。
`Vst3WorkerPipeV1` 提供 Windows overlapped named-pipe server、4-byte bounded length prefix、
connect/read/write timeout 與 caller-owned receive buffer；worker-side 也可用 bounded
`connect_client` 連入 supervisor 建立的 pipe。`hibiki_vst_worker` 目前能回應 Hello、
Heartbeat、受限 ProcessBlock passthrough 與 Shutdown；它是可執行的 transport/可靠性
fixture，不宣稱已載入 VST3 SDK 或第三方 plugin。

`Vst3SandboxProcess::handshake_worker` 與 `process_worker_block` 是 supervisor 的唯一
worker exchange API：前者驗證 HelloAck、request ID 與零 payload，後者在 bounded
control/IPC thread 建立一般或參數化 ProcessBlock、送出後驗證 response type、request ID、
聲道／frame shape、payload 與有限輸出，再交給 caller-owned output。任何 send/receive/
protocol/plugin error 都回傳明確結果，並在已驗證的輸出範圍先填靜音；這些方法不得從 RT
callback 呼叫，也不會自動重啟 quarantine worker。參數 frame 仍沿用最多 64 點的既有
protocol limit，SDK worker 才會把它轉成 `IParameterChanges`。

`Vst3WorkerLaneSessionV1` 將這個 exchange 與 stable `lane_token` 及 latency projection 接起來。
它要求成功 handshake 後才進入 `Ready`，每次成功 block 必須連續銜接 `block_start`。worker
錯誤會把 lane 置為 `Degraded`，不自動重啟或重送未知結果。這是 control/IPC session boundary，
不是 RT graph plugin callback；跨版本 plugin state persistence 仍由更上層規格負責。

`PluginHostModel` 的 `prepare_worker_session`、`handshake_worker` 與
`process_worker_block` 是目前 host model 的接線點：只有 trusted/certified、same-channel、
有 stable lane token 的 descriptor 才能建立 session；握手或 block exchange 失敗會沿用
host 的 `Quarantined` 狀態並 detach lane。這仍是 source-level control contract，沒有把
任何第三方 plugin binary、SDK checkout 或實體 endpoint 納入公開建置。

`vst3_bus_layout.hpp` 定義 optional multi-bus preflight：最多 8 個 input/output audio bus、
總聲道最多 32，Main input/output 必須各存在且位於 index 0；input 可標示 Auxiliary 或
Sidechain，output 不得標示 Sidechain，所有未使用槽位必須完全為零。帶有 explicit layout
的 `PluginDescriptorV1` 必須通過 schema、角色、聲道與 main-layout exact match 才能進入
`Running`，否則直接 quarantine。這個契約先保護 admission 與未來 worker ABI；目前 pinned
SDK processor／worker 仍只實作一個 Main input/output bus，因此 side-chain/multi-bus 的
實際 plugin process 仍是後續 gate，不可由 validator 反推已完成。

`vst3_worker_protocol.hpp` 現在提供 versioned multi-bus/side-chain worker frame 契約：
`ProcessBlockMultiBus`／`ProcessBlockMultiBusResponse` 的 payload 是 self-describing
fixed prefix（schema version、input/output bus count、16 reserved bytes）+ 16 條 8-byte
bus records（與 `Vst3AudioBusV1` 同構）+ bus-ordered interleaved Float32 samples
（active inputs 依槽位順序，然後 active outputs）。內嵌 layout 必須通過
`validate_vst3_bus_layout_v1` 全部規則（含 Main input/output 必在 index 0、output 不得
Sidechain、總聲道上限 32），reserved bytes 必須為零，frame 上限為 512 frames，且最壞情況
payload 仍在既有 `kVst3WorkerMaxPayloadBytesV1` 預算內（static_assert 保證）。codec 不配置、
不鎖、不等待；NaN/Inf、layout 違規、geometry mismatch 一律 fail-closed。
`vst3_worker_multibus_bus_samples_v1` 提供已驗證 block 內的 per-bus sample slicing。
這仍是 wire/control-plane 契約：SDK processor 與 worker executable 尚未 dispatch 多 bus
plugin processing，不能由本契約反推 side-chain/multi-bus 實際 plugin process 已完成。

`vst3_sdk_catalog.hpp` 提供 optional control-plane bridge，使用 `THIRD_PARTY.yml` 鎖定的
Steinberg SDK 3.8.1 build 84 與 submodule commits，掃描 module factory class metadata。
SDK checkout 由開發者在 `.local/` 提供，public monorepo 不 vendor SDK；catalog 不執行 plugin、
不進 RT thread，也不等同 certification。其 optional target 已在本機以 MSVC 編譯通過。

`vst3_sdk_processor.hpp` 提供同一 optional target 的 worker-side processing adapter。它只
接受 catalog 回傳的 class UID，初始化一個主 input/output audio bus，支援 1/2/5.1/7.1
speaker arrangement，將 caller-owned interleaved block 轉成固定 4096-frame planar scratch，
再交給 `IAudioProcessor::process`，並回報 plugin latency。`process` 不配置、不等待，遇到
NaN/Inf、格式不符或 plugin error 會清零輸出；但 plugin 本身仍是不受信任程式，必須留在
VST3 sandbox worker，不能直接掛進 Hibiki RT graph。此 adapter 沒有參數自動化、side-chain、
多 bus、state persistence 或 latency compensation policy。

SDK adapter 的 control API 另接受最多 16 個 parameter IDs、每個 ID 最多 5 個 sample-accurate
points，將 normalized `[0,1]` 值轉成官方 `IParameterChanges`；非法 offset、值域或超限事件
會在交給 plugin 前拒絕。`ProcessBlockWithParameters` 已由 frame codec 與 optional SDK
worker 解碼並交給 adapter。

`LatencyAlignmentPlanV1` 會在 control plane 取所有 active lane 的 reported latency，將每個
lane 的補償量設為 `maximum_latency - lane_latency`，上限 16,384 samples。
`FixedDelayLineV1` 使用固定 8 聲道 ring、最多 4,096 frames/block，RT `process` 不配置、不鎖、
不等待，遇到非有限輸入會清零並 reset。這個 primitive 已通過 impulse／NaN contract test。
`LatencyGraphCommitV1`／`LatencyGraphCommitterV1` 現在提供固定容量的 lane token、revision
綁定與 Validate → Prepare → Commit/Rollback 交易；它把 plugin-reported latency 變成可供 graph
準備延遲線的 immutable control snapshot。`LaneLatencyBankV1` 已在 graph Prepare 時配置、在
Commit 時和 snapshot 一起交換，RT mixer 會跨 block 套用補償；仍不能把這個 bounded path 誤稱
為第三方 plugin certification 或實體 sink 端對端驗收。

當本機提供 pinned SDK 時，`hibiki_vst3_sdk_worker` 會把該 adapter 接到既有 named-pipe
worker frame：啟動參數固定包含 pipe、module、class UID、sample rate 與 2/6/8 channels；
Hello/Heartbeat 沿用既有 frame，ProcessBlock 以 caller-owned packet 驗證後交給 SDK，成功
回傳 ProcessBlockResponse，plugin 或格式錯誤回傳 Error。這個 target 不會在一般 CI 或
public source-only checkout 自動生成，且仍不提供第三方 plugin binary。

`Vst3PluginStateStoreV1` 是私有 state boundary：每筆最多 1 MiB、最多 16 筆，要求 state ID
（1..64 bytes，`[a-z0-9][a-z0-9._-]*`）、plugin ID（1..128 bytes，
`[a-z0-9][a-z0-9._-]*`）、32-hex class UID、非零 module SHA-256 與明確 state version；
restore 必須
完全匹配 identity/version，destination 不足或 mismatch 會 fail-closed。
`restore_with_migration` 只在 identity 完全匹配且呼叫端明確提供 handler 時處理版本差異；
沒有 handler、handler 回報錯誤、或輸出超過 1 MiB 都拒絕，Hibiki 不解讀第三方 opaque bytes。
`schemas/vst3-plugin-state-v1.schema.json` 只描述 metadata，`storage` 固定為
`private-caller-owned`，`migration_policy` 明示 `identity-exact-or-explicit-handler`；實際
opaque bytes 不得進 GitHub、Issue、AI context pack 或 release artifact。

在本機提供 pinned VST3 SDK 時，`Vst3SdkProcessorV1::save_state/load_state` 以 bounded
`IBStream` 呼叫 component `getState/setState`，同樣限制 1 MiB，並將 overflow、plugin
error、destination 不足與 allocation failure 分開回報。它只建立 worker-side SDK adapter；
尚未接到正式 Scene migration registry、plugin UI editor 或第三方 compatibility certification；
目前已有固定 16-rule `Vst3PluginStateMigrationRegistryV1` primitive，但 handler 仍由受信任的
control-plane caller 注入，未提供自動探測或遠端下載。

`Vst3SceneStateCoordinatorV1` 將 Scene ID（1..31 bytes，與引擎 catalog 一致；不同於 state ID 的 1..64 bytes）、state ID、
plugin identity 與 target state version
綁定到最多 16 筆固定容量 reference。Scene 啟用前會 inspect 私有 state、檢查 byte bound，
並要求 exact version 或 registry 中唯一的 source→target rule；`restore` 只寫入 caller-owned
buffer，絕不把 opaque bytes 放入 Scene JSON、GitHub 或 AI context。其 metadata contract 是
`schemas/scene-vst3-state-binding-v1.schema.json`。
`preflight_scene_vst3_state_v1` 可直接交給 `EngineControlWorkerV1::set_scene_preflight`；沒有
Scene state bindings 時維持 Easy Scene 向後相容，存在 binding 時則在 graph Prepare 前強制
通過 coordinator。

第三方 plugin state 不得只因為 handler 能執行就自動准入。每一個明確的 source→target
handler 都必須依 `docs/VST3_STATE_COMPATIBILITY_REVIEW.md` 完成 identity、版本、容量、
失敗回復、隱私與非 RT 執行檢查；未審查或無法證明 redistribution 權利的 plugin 維持
quarantined，不得進入 trusted/certified 或 Low Latency Lane。

`Vst3SandboxDiagnosticV1` 提供固定 schema version、sandbox state、有限的 reason enum 與
worker pipe ready/connected 布林值，作為 control-plane 的去敏化事故摘要。它不保留或輸出
worker/plugin path、PID、handle、command line、raw exception、endpoint identity 或 opaque
plugin bytes；它不是 crash dump capture，也不會取代 production worker policy。
`Vst3CrashReportStoreV1`（vst3_crash_report.hpp）是版本化 bounded crash report
capture/redaction 契約：entry 只含 schema version、非零 UTC epoch、固定 reason enum、
exit code、uptime ms 與 32-byte module SHA-256 digest；原始路徑、PID、handle、
command line 與 opaque plugin bytes 一律不進 entry。store 是固定 16 筆 oldest-first
ring，滿溢淘汰最舊，invalid entry（schema version 不符、時間戳為零、未知 reason、
全零 digest）直接拒收且不改變既有內容。serialize 只輸出固定鍵組 canonical JSON
（上限 64 KiB）；parse 是嚴格 fail-closed tokenizer，未知或重複鍵、未知 reason 拼法、
越界數值、非 hex 或錯誤長度 digest、截斷或尾隨內容一律拒絕，目的地只在整份文件驗證
通過後才替換；serialize→parse→serialize 往返逐位元組穩定（含負時間戳）。內建小型
SHA-256 以空字串與 "abc" 已知向量驗證，僅供 redaction digest 使用。這是 user-space control-plane
evidence 契約：不擷取 minidump、不解 symbol、不連接 production worker policy。

`Vst3SandboxProcessV1`（vst3_sandbox.hpp）把 sandbox 生命週期事件橋接進該 store：
worker 以非零碼退出、watchdog timeout、pipe 收送失敗與 handshake/exchange protocol
error 各記錄一筆去敏化 entry；stop()/quarantine() 內的強制終止失敗也記錄一筆
job_object_failure entry。單一生命週期事件最多產生一筆 entry：第一個被記錄的原因佔用
事件槽位，後續同事件原因不得重複或覆蓋。capture instant 使用系統 UTC 時鐘，uptime 只使用注入的
單調時鐘，兩個時鐘不得混用。module digest 在 launch 時由 plugin path 位元組一次性算出，
path 本身不留存。setup 失敗（worker 執行檔不存在等）在 process 存在前即結束，不產生任何
entry。bridge 屬 user-space control-plane 觀測契約，不代表 minidump capture 或 production
crash policy；強制終止失敗的可觀測性不宣稱提升 kernel containment 強度。

## 尚未完成的邊界

plugin scan 的 factory metadata catalog、單一主 bus SDK dispatch adapter、multi-bus/side-chain
admission validator、bounded parameter frame、latency alignment primitive、latency graph commit
與 optional worker executable 已有 bridge；仍未完成第三方 plugin certification、第三方 plugin
compatibility review、side-chain/multi-bus 的實際 worker process、
完整 crash dump capture/redaction pipeline 與 production worker policy。目前
supervisor、named pipe、passthrough worker、catalog、bounded SDK processor 與 handshake/process
exchange 提供可測試的 process containment/metadata/automation boundary，不能宣稱已完成第三方
VST3 host。
