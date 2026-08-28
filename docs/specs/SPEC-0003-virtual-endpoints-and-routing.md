---
id: SPEC-0003
status: draft
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-25
review_after_days: 14
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["driver/**", "sdk/**", "apps/**", "asio/**", "extensions/**", "src/**"]
---

# SPEC-0003：虛擬端點、Lane 與路由

## 目的

提供固定的 Hibiki Main、Low Latency、Surround 與 Virtual Mic 端點。每個 Windows
App、Hibiki ASIO client、瀏覽器分頁與輸入裝置都是獨立 Lane，可選輸出、聲道映射、
延遲模式、DSP chain 與 Scene。

## 目前可實作的契約

- Graph 變更必須經過 `Validate → Prepare → Commit`；任何失敗都保留目前 graph。
- 端點、ASIO CLSID、IPC namespace 只能讀取 `config/distribution-profile.yml`，不得由
  runtime 或 AI 重新產生。
- v1 只承諾 LPCM 2.0、5.1、7.1；Atmos／DTS:X object codec 不在範圍。
- Vendor ASIO、WASAPI Exclusive 與 RAW path 顯示 bypass，不宣稱受 Hibiki 攔截。
- Chrome／Edge 單分頁擷取必須由使用者點擊 MV3 extension 啟動；Windows process routing
  不得假裝能靜默辨識 tab。
- user-space output prototype 使用 caller-owned interleaved ring buffer、bounded clock-drift
  ratio（±500 ppm）與無配置多相位 SRC；真正 sink 必須保留相同的 no-allocation boundary，並
  在實體時鐘 fixture 上替換為持續相位／高品質 filter。
- `sdk/include/hibiki/driver_control_v1.h` 定義 Apache-2.0 C ABI；`driver/` 的 validator
  保持 MS-PL，檢查固定 header、Q16.16 dB、格式與 endpoint GUID 邊界。這不是可載入的
  WaveRT driver，也不能替代 WDK build 與實機驗證。
- `DeviceRecoveryCoordinator` 是 worker-side 的 platform-neutral recovery state machine：
  `IMMNotificationClient` snapshot 只能透過單調 sequence 投遞，失效、拔除、format change
  或 Audio Service restart 進入 `RebindPending`，再以 `begin → prepare → commit/rollback`
  交易換端點。恢復前使用 safe-start dB 並保持 mute，不能回到 0 dB／100%。
- `PhysicalDeviceCatalogV1` 是 watcher 與切換 worker 之間的固定 32 筆 control-plane
  catalog；它驗證 endpoint identity、顯示名稱、LPCM format、Active 狀態與每個 flow 唯一
  default。它只決定 endpoint 是否可選，不取代 `DeviceSwitchTransaction` 的暖機、交叉淡化、
  commit／rollback，也不把真實私人 endpoint ID 寫進 Scene 或 repository。
- `driver/include/hibiki/wavert_endpoint_state_v1.h` 與其 MS-PL C 實作是 WDK adapter 的
  第一個可測試控制核心：格式、Q16.16 dB、safety ceiling、mute、generation 與 actuator
  都在 driver 邊界驗證；event-context GUID 會在任何 state mutation 前完整驗證，錯誤
  請求不會留下半套 volume/mute/generation；它仍不是完整 PortCls miniport 或可載入 `.sys`。
- `driver/include/hibiki/endpoint_topology_v1.h` 與 `src/endpoint_topology.c` 固定四個 endpoint
  的方向、聲道數、Windows channel mask、預設 buffer、取樣率 flags 與 distribution GUID：
  Main=stereo render、Low Latency=stereo/64-frame render、Surround=7.1 render、Virtual Mic=stereo
  capture。未來 SYSVAD topology 必須消費此 catalog，不得只由 channel count 推斷排列。
- `driver/inf/HibikiVirtualAudio.inf` 固定 Root\HibikiDSP hardware ID、四個 endpoint GUID
  與 service/package 邊界；source-only `tools/driver-source-check.ps1` 會先移除
  `;` 註解，再只在預期的 INF section 內驗證 Version、NTamd64 install mapping、CopyFiles、
  service 與 ServiceBinary；缺 section、錯置或只有註解的 directive 一律 fail closed，並拒絕
  GPL/private payload 與 traversal path。專案不需要 HLK 或任何簽章；
  真正 PortCls/SYSVAD topology 與實機驗證仍是 driver 完成度待辦。
- `driver/include/hibiki/wavert_stream_v1.h` 與 `src/wavert_stream.c` 提供可由未來 pin
  callback 掛接的 portable WaveRT data-path core：caller-owned Float32 frame ring、完整
  block overrun reject、underrun silence fallback 與 dropped/underrun counters；它限制在
  2/6/8 channels、44.1/48/96/192 kHz、2–16 periods，且不配置、不等待。WDK miniport 仍
  必須補上 interlocked publication、KS pin wiring、實體 endpoint 與 delivery 驗證。
- `driver/wdk/hibiki_stream_adapter.cpp` 示範 WDK-only 的 pin callback 邊界：以 spin lock
  保護 ring、submit render block、讀取時以 silence fallback 填滿 underrun，並提供 reset；
  `HibikiWaveRtPinInitializeEndpointV1` 與 `HibikiWaveRtBuildFormatV1` 只能以固定
  `endpoint_topology_v1` 的 render geometry、channel mask 與取樣率建立 pin；它仍需在正式
  SYSVAD/PortCls 專案中編譯，不能單獨宣稱可載入 driver。
- `HibikiWaveRtPinInitializeCaptureEndpointV1` 與 `HibikiWaveRtBuildFormatEndpointV1` 以同一
  topology catalog 覆蓋 Virtual Mic capture pin 與格式，避免 capture 端點另造聲道／取樣率
  契約；仍需正式 PortCls wiring 與實機 capture delivery 驗證。
- `HibikiPropertyContextInitializeEndpointV1` 同樣以 topology 的 endpoint GUID、聲道數與
  取樣率初始化每個 WDK property context，避免 miniport 另造一套 identity；它仍只是
  MS-PL source boundary，未提供可載入 `.sys`，也不代表實機 PnP start 已驗收。
- `HibikiPropertyHandlerVolumeV1`／`HibikiPropertyHandlerMuteV1` 對
  `KSPROPERTY_TYPE_BASICSUPPORT` 回報 `KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET`，並在
  Value／Instance buffer 不足時先回報所需大小；這只是 WDK source boundary，尚未替代
  目標 WDK build 與實機 `.sys` 驗收。
- `sdk/include/hibiki/driver_control_transport_v1.h` 定義固定 136-byte little-endian
  `endpoint-state`／`volume-notification` control packet；所有欄位以明確 offset 編碼，
  不依賴 C struct padding，並在 driver/user-space boundary 驗證 GUID、LPCM 格式、Q16.16
  dB、mute、generation 與 actuator。`request_id` 是 driver/user-space correlation 欄位，
  必須非零；零值保留給未初始化／無 correlation 狀態，encode、validate 與 decode 一律
  fail closed。GPL engine 的 `DriverVolumeLinkV1` 只透過此 Apache
  ABI 解碼，套用 requested dB/mute 到 canonical output-group safety path，並可忽略已登記
  event-context 防止回授迴圈；這仍是 user-space/control-plane evidence，不是 loadable
  WaveRT、PortCls 或實體音訊 evidence。
- 同一 transport 另提供固定 16-byte little-endian header-only Hello/Ack/Error framing，
  以 request ID 建立 driver/user-space correlation；v1 不攜帶自由格式錯誤文字或未界定
  payload，避免把 kernel IPC 變成隱含的變長配置協定。`schemas/driver-control-v1.schema.json`
  以條件式規則強制同一 split：Hello／Ack／Error 只允許 `schema_version`、`message_type` 與
  `request_id`；volume-notification 與 endpoint-state 必須攜帶完整 state 欄位組，
  且兩者的 correlation `request_id` 同樣必須非零
  （非空 GUID、LPCM 格式、Q16.16 dB、mute、非零 generation、actuator）。schema 的
  `endpoint_guid` 必須至少一個字元，與 GPL engine 拒絕空 endpoint GUID 的行為一致；
  兩個 GUID 字串欄位都排除 C0/C1 控制字元與 DEL，使 persisted schema 驗證與 bounded
  NUL-terminated wire/bridge identity 處理維持 fail-closed parity；
  `event_context_guid` 允許空字串，代表「沒有要忽略的 event context」。
- `sdk/include/hibiki/driver_stream_transport_v1.h` 與 `sdk/src/driver_stream_transport_v1.c`
  定義 driver→engine 的固定 80-byte header＋interleaved Float32 packet；C ABI 提供
  allocation-free encode/validate/payload view，`decode_driver_stream_packet_v1` 會複製到
  caller-owned lane storage 並拒絕非有限 sample。`sequence` 與 `generation` 是 freshness／
  lifecycle 欄位，兩者必須非零；encode 與 validate 在接受 payload 前拒絕零值，但保留
  `UINT64_MAX` 為有效值。packet span 必須等於 header 宣告長度。這是 user-space packet
  boundary 的 stale-state 防護，不是實體 driver 或 WaveRT evidence。
- `AudioEngineModel::process_driver_stream_packet` 只接受 render packet，要求 packet endpoint
  GUID、sample rate 與 engine、channel count 與 active lane 全部相同，通過後沿用
  `process_lane_block` 的 immutable graph／Group Master／limiter 路徑；capture packet、錯誤
  endpoint、NaN 或格式不符都不會進 graph。
- `AudioEngineModel::encode_driver_stream_packet_from_lane` 會把 caller-owned lane block 沿用同一條
  graph／Group Master／limiter 路徑處理，再以固定 80-byte header＋interleaved Float32 的 v1 ABI
  編成 outbound render packet；lane、格式、freshness 或非有限 sample 不符時 fail-closed，不會
  發出 partial packet。這是 user-space outbound encode evidence；真正送進 WaveRT ring 仍需要
  kernel-mode IPC/shared-memory wiring，不能宣稱已完成實體 WaveRT delivery 驗收。
- `sdk/include/hibiki/driver_stream_ring_v1.h` 與 `sdk/src/driver_stream_ring_v1.c` 提供
  固定 layout 的 SPSC multi-slot shared-memory ring，用來搬運整個 v1 driver stream packet；
  RT push/pop 路徑不配置、不取得 mutex、不等待，僅以 interlocked sequence 發布與消費。
  格式不符、非有限 sample、零 sequence/generation 或非零 reserved 欄位一律 fail-closed；
  ring 滿載時拒絕且不寫入 partial slot，underrun 回報 silence flag 並累計計數。binary layout
  穩定，可供未來跨 process 或 kernel mapping 重用。contract tests 另外證明：ring pop
  的完整 packet 可直接通過 engine render gate；engine outbound encode 經 ring 往返後
  逐位元組一致且 validate 通過；underrun 或損毀 packet 則 fail-closed。這些都是
  user-space contract evidence；實體 WaveRT delivery 驗收尚未涵蓋。
- Engine Preview 提供 opt-in `--enable-driver-loopback`（搭配
  `--enable-wasapi-output`）：bounded stereo sine 先經 `encode_driver_stream_packet_from_lane`
  編成完整 v1 packet，發布到 in-process `driver_stream_ring_v1`，pop 後由
  `process_driver_stream_packet_to_wasapi` 再次 validate 並送進既有 WASAPI handoff；
  status route `driver-loopback` 只有在 sink 回報 rendered blocks 後才顯示 Ready，並誠實列出
  encode/push/pop/deliver 失敗與 ring overrun/underrun 計數。此模式與 test tone、tab bridge、
  process delivery 互斥，且 ring 僅存在於 preview 行程內：它是 user-space packet-chain
  evidence，不是 kernel IPC、WaveRT delivery 或 driver 行為證據。
- Engine Preview 也提供 opt-in `--enable-wav-source`（搭配
  `--enable-wasapi-output` 與 `--wav-source-path`）：preview 會先以既有 v1 WAV decoder
  fail-closed 驗證 Float32 PCM 與聲道數；檔案取樣率與 prepared sink 不同時，在控制面以
  bounded polyphase resampler（0.25x–4.0x）離線轉換整個解碼緩衝區並對齊名目長度，
  status detail 誠實標示 `resampled <file>-><sink>`；RT render path 維持純拷貝、無配置。
  轉換後把 bounded decoded block 接進
  user-space graph 與 WASAPI handoff；可另用 `--enable-wav-loop` 重播。status route
  `wav-source` 只有在 sink 回報 rendered blocks 後才顯示 Ready，並回報 bounded
  `frames=rendered/total` 進度與 `failed` 區塊計數；連續失敗達上限後停止排程並保留
  honest detail。此模式與 test tone、tab bridge、driver loopback 互斥；它是本機檔案播放
  的 user-space graph evidence，不是 endpoint policy、實體 driver 或 WaveRT delivery 驗收。
  WAV 來源解碼以 64 MiB 位元組上限為界（`kMaxSourceWavFramesV1` 僅防整數溢位）；
  4096-tap 即時卷積核心上限只約束 IR kernel 載入，不套用於播放來源檔。
- Engine Preview 另提供 opt-in `--render-offline <output.wav>`（搭配
  `--enable-wav-source` 與 `--wav-source-path`）：在同一行程內完成 decode（必要時以同一
  bounded resampler 轉換到 48 kHz）、commit 同一套 Studio graph，再以 bounded blocks 驅動
  `process_output_group` 渲染整段訊號，最後匯出 Float32 立體聲 WAV。此模式完全不觸碰
  WASAPI、driver loopback 或任何音訊端點，因此可在沒有喇叭或音效裝置的機器重現；
  它是裝置無關的 user-space graph evidence，不是 driver、WaveRT delivery 或實機播放
  驗收。離線渲染與所有即時播放模式互斥。
- `PersistentPolyphaseResampler` 是 clock-drift/SRC baseline：固定容量 8 phase × 16 tap
  polyphase FIR bank 支援最多 8 聲道與 0.25x–4.0x source step，保留跨 block phase 與
  bounded history，ratio 變更不重置 stream，invalid input fail-closed 且 RT path 不配置。
  它仍是 user-space contract evidence，不是真實裝置 clock soak 或實體音訊播放驗收。
- `OutputSinkModel` 將 `ClockDriftEstimator` 的 `sink/source` ratio 接到每個 persistent SRC
  的 effective source step（`base_step / ratio`）；clock observation 在 control side，音訊
  process 只讀已設定的 immutable pipeline state，後續仍需真實 USB/HDMI/Bluetooth clock fixture。
- `OutputHandoffCoordinatorV1` 將 `DeviceSwitchTransaction` 與 30 ms `OutputCrossfade` 綁定：
  begin/prepare 後只能在 fade 完成時 commit，任何 prepare 或 buffer failure 都可 rollback
  到原 active endpoint；這是 user-space handoff contract，不是實體 driver soak 證據。
- 原生 ASIO transport 使用 Apache-2.0 固定 layout `hibiki_asio_transport_v1`，由 Engine
  control plane 建立合法的 `Local\\HibikiDSP_v1_asio` named mapping；ASIO DLL 只在 host callback
  完成後把八聲道 Float32 block 寫入 SPSC ring。Engine 端 `AsioTransportConsumerV1` 在 RT lane
  以 caller-owned buffer pop，禁止配置與等待。mapping 不存在、格式不符或 ring 滿載時，ASIO
  仍可運作但 UI 必須顯示 detached／dropped blocks，不能宣稱已套用 Hibiki graph。shared-memory
  region 的 reserved header field 必須為零；push／pop 對非零值 fail closed，不消費或發布
  ring slot，也不改 caller-owned output counters。這是 user-space SPSC contract，不是 vendor
  ASIO、實體 sink 或實體 driver evidence。
- `AudioEngineModel::process_asio_transport` 將一個已 pop 的 ASIO block 暫時置入指定 Lane，
  走現有 immutable graph 與唯一 Group Master，再寫入 caller-owned output；完成後還原 caller
  的 Lane view。它只證明 user-space graph data path，不代表已連接實體 sink、WaveRT endpoint
  或 vendor ASIO。
- `AudioEngineModel::process_asio_transport_to_wasapi` 在同一個 caller-owned block 完成 graph、
  Group Master 與 limiter 後，將結果單次提交到雙 worker WASAPI handoff；因此 Hibiki ASIO
  client 不需要另寫一套音量／裝置切換邏輯。ASIO ring、graph 或 sink 任一邊界失敗都 fail-closed，
  不會標記為已播放；目前仍缺真實 driver／endpoint delivery evidence。
- `process_driver_stream_packet_to_wasapi` 與共用 `process_lane_block_to_wasapi` 讓合法的
  driver render packet 也走完全相同的 lane graph、Group Master、limiter 與 sink handoff；
  endpoint GUID、sample rate、channel layout 或 sink state 任一不符即拒絕，避免 driver、ASIO
  兩條路徑產生不同音量／校正語意。
- `AudioEngineModel::process_lane_block` 是同一條 caller-owned block API，讓 TabCapture、
  Virtual Mic 或其他已驗證來源共用 Lane／Group Master 行為；平台 bridge 不得在 RT path
  配置、等待或直接操作 Windows COM。
- `process_tab_capture_lane_v1` 可選接一個已按相同 sample rate 設定的
  `ProgramAwareLevelControllerV1`；它在進 graph 前對該 tab 套用慢速 RMS 或
  `KWeightedProxy` 內容音量，速率與 boost/cut 都受 policy 限制。K-weighted 路徑仍是
  bounded proxy，不是靜默擷取、完整 gated LUFS meter、降噪或 BS.1770 conformance。
- 同一個 tab effects contract 可選套用 `BasicNoiseSuppressorV1`：固定高通＋downward gate，
  只接受 1–8 聲道且要求 sample rate/channel 完全相符。policy 的 `enabled` 欄位是權威
  開關：只有 `enabled=true` 是有效設定，`enabled=false` 一律 fail-closed 拒絕，不得
  被解讀為 bypass。gate 採 upper-only 2 dB hysteresis：
  envelope 在設定的 threshold 關閉，必須回升到 threshold +2 dB 才重新開啟，兩個邊界之間
  維持原狀態，因此訊號在臨界附近徘徊時不會反覆開關（chatter）。它是可測試的基本抑噪，
  不宣稱 RNNoise、頻譜 AI、AEC 或麥克風權限處理；其 caller-owned interleaved processing
  entry point 必須先以 checked arithmetic 驗證 `frames * channels` 可由 `size_t` 表示，
  溢位時在 sample scan、state mutation 或 caller-buffer write 前 fail-closed；效果順序為
  PEQ → IR → basic suppressor → level。
- `SessionRouteGraphBuilderV1` 將 `AudioSessionRegistry` 的 active、已 bind session 轉成
  `GraphConfigV1`；`WindowsSession` gain owner 不重複套 lane makeup，`HibikiInternal` 才
  使用 per-session makeup dB。未綁定 session 忽略、重複 lane ID 或 Strict Direct 搭配 gain
  一律 fail-closed。
- `LaneConfigV1` 保留原本的 `channel_map`，並可選啟用 8×8 `channel_matrix` 來表達 VB
  Matrix 類的 crossfeed、分流與加權混音；矩陣係數在 control side 驗證並複製到 immutable
  RT snapshot。Strict Direct 禁止 matrix_enabled，避免把 DSP 路徑誤標成 bit-perfect。
- `set_makeup_gain_db` 是 registry 唯一的 per-session gain mutator，限制 −144..+12 dB；
  metadata `upsert` 不得靜默覆蓋使用者設定。
- `RtLaneSnapshotV1` 會把每個 lane 的 `output_group` 編譯成固定大小 immutable bytes；
  `process_graph_for_output_group` 與 `AudioEngineModel::process_output_group` 只 render
  指定群組，未命中的群組 fail-closed，避免四個 App／tab 的 samples 互相串音。未指定群組
  的舊 `process_graph` 仍保留「render 全部 lanes」語意。
- grouped render 必須讓每個 enabled lane 的固定 latency ring 剛好推進一次，即使該 lane
  不屬於目前 target group；背景 lane 只推進 clock，不混入 target output。contract test
  覆蓋 main/movie 交錯 callback 時的 impulse 對齊。
- `WindowsWasapiOutputV1` 提供 user-space physical sink boundary：同一個 dedicated sink worker
  apartment 以 endpoint ID 綁定 shared-mode Float32 2/6/8 聲道與固定 sample rate，再由該 worker
  的 `render` 處理 padding、WASAPI buffer copy、ReleaseBuffer。格式不符、裝置不存在或 buffer
  不足都回傳失敗；Hibiki graph RT thread 不得呼叫此 COM API、初始化 COM、配置或重新綁定，
  control plane 只能排程 worker command。
- `VirtualMicRouteModel` 提供未來 Virtual Mic endpoint 的 user-space capture/reference contract：
  固定 1/2 聲道與 44.1/48/96/192 kHz、privacy mute 預設開啟、caller-owned capture 與
  render echo-reference copy。可選 `VirtualMicDspV1` 以固定 128-tap 上限做 normalized-LMS
  reference cancellation 與慢速 noise gate；noise gate 同樣採 upper-only 2 dB hysteresis
  （在設定 threshold 關閉、envelope 回升到 threshold +2 dB 才重新開啟，中間維持狀態），
  臨界附近訊號不會造成 chatter。這是 bounded baseline，不宣稱 acoustic AEC、
  RNNoise 或 conformance，driver/IPC/permission indicator 仍需另行驗收。
  所有 `VirtualMicDspV1` 與 `VirtualMicRouteModel` 的 caller-owned interleaved
  processing entry point 都必須先以 checked arithmetic 驗證 `frames * channels`
  可由 `size_t` 表示；溢位時在 sample scan、privacy fill、reference copy 或 DSP
  state mutation 前 fail-closed。lane adapter 另外以 caller 宣告的 frame capacity
  驗證整個 block，兩層檢查都不能省略。
- `process_virtual_mic_lane_to_wasapi_v1` 在 privacy gate／optional DSP 後共用 lane-to-WASAPI
  adapter；capture、reference、graph 或 sink 任一邊界失敗都不提交。這只提供 user-space
  monitor/output path，不能取代真正的 signed Virtual Mic capture driver。
- `HibikiMiniportTopologyV1`、topology filter tables、WaveRT/topology subdevice pair 與 INF
  per-subdevice interfaces 提供 local WDK build／Inf2Cat 的 PortCls start-path wiring。這仍是
  source/build evidence：不新增 guest 安裝、載入、實體 endpoint 或實體音訊
  證據；下一次 guest 測試必須重新驗證 PnP start。

## 未解問題（阻擋完整 driver 實作）

1. 已固定 endpoint topology/channel mask；仍需 WaveRT／KS PortCls wiring 與 INF/實機測試矩陣。
2. 真實多輸出 endpoint 的 clock drift、ring buffer 與 adaptive SRC soak；portable stream
   ring 與 user-space per-sink SRC baseline 已存在。
3. Virtual Mic 的 echo reference、privacy indicator 與卸載行為。

在上述問題由 ADR 與測試 fixture 定案前，本 Spec 保持 draft；可以繼續完成 user-space
契約與模擬器，但不得宣稱已具備可安裝的虛擬音效驅動。

## 驗收方向

使用合成 Lane fixture 驗證 2.0／5.1／7.1 mapping、重複 Lane 拒絕、裝置交易 rollback，
並在真正 driver 進入主線後增加拔插、睡眠、Audio Service restart 與五來源八小時 soak test。
