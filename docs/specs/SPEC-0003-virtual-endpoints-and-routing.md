---
id: SPEC-0003
status: draft
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 14
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["driver/**", "apps/**", "asio/**", "extensions/**", "src/**"]
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
  ratio（±500 ppm）與無配置線性 SRC；真正 sink 必須保留相同的 no-allocation boundary，並
  在實體時鐘 fixture 上替換為持續相位／高品質 filter。
- `sdk/include/hibiki/driver_control_v1.h` 定義 Apache-2.0 C ABI；`driver/` 的 validator
  保持 MS-PL，檢查固定 header、Q16.16 dB、格式與 endpoint GUID 邊界。這不是可載入的
  WaveRT driver，不能替代 WDK／HLK／簽章驗收。
- `DeviceRecoveryCoordinator` 是 worker-side 的 platform-neutral recovery state machine：
  `IMMNotificationClient` snapshot 只能透過單調 sequence 投遞，失效、拔除、format change
  或 Audio Service restart 進入 `RebindPending`，再以 `begin → prepare → commit/rollback`
  交易換端點。恢復前使用 safe-start dB 並保持 mute，不能回到 0 dB／100%。
- `driver/include/hibiki/wavert_endpoint_state_v1.h` 與其 MS-PL C 實作是 WDK adapter 的
  第一個可測試控制核心：格式、Q16.16 dB、safety ceiling、mute、generation 與 actuator
  都在 driver 邊界驗證；它仍不是完整 PortCls miniport 或可載入 `.sys`。
- `driver/include/hibiki/endpoint_topology_v1.h` 與 `src/endpoint_topology.c` 固定四個 endpoint
  的方向、聲道數、Windows channel mask、預設 buffer、取樣率 flags 與 distribution GUID：
  Main=stereo render、Low Latency=stereo/64-frame render、Surround=7.1 render、Virtual Mic=stereo
  capture。未來 SYSVAD topology 必須消費此 catalog，不得只由 channel count 推斷排列。
- `driver/inf/HibikiVirtualAudio.inf` 固定 Root\HibikiDSP hardware ID、四個 endpoint GUID
  與 service/package 邊界；它只引用未提交的 SYS/CAT，`Inf2Cat`、HLK、Microsoft signing
  與真正 PortCls/SYSVAD topology 仍是 release gate。
- `driver/include/hibiki/wavert_stream_v1.h` 與 `src/wavert_stream.c` 提供可由未來 pin
  callback 掛接的 portable WaveRT data-path core：caller-owned Float32 frame ring、完整
  block overrun reject、underrun silence fallback 與 dropped/underrun counters；它限制在
  2/6/8 channels、44.1/48/96/192 kHz、2–16 periods，且不配置、不等待。WDK miniport 仍
  必須補上 interlocked publication、KS pin wiring、實體 endpoint 與 signed package。
- `driver/wdk/hibiki_stream_adapter.cpp` 示範 WDK-only 的 pin callback 邊界：以 spin lock
  保護 ring、submit render block、讀取時以 silence fallback 填滿 underrun，並提供 reset；
  它仍需在正式 SYSVAD/PortCls 專案中編譯，不能單獨宣稱可載入 driver。
- `sdk/include/hibiki/driver_stream_transport_v1.h` 與 `sdk/src/driver_stream_transport_v1.c`
  定義 driver→engine 的固定 80-byte header＋interleaved Float32 packet；C ABI 提供
  allocation-free encode/validate/payload view，`decode_driver_stream_packet_v1` 會複製到
  caller-owned lane storage 並拒絕非有限 sample。packet span 必須等於 header 宣告長度。
- `AudioEngineModel::process_driver_stream_packet` 只接受 render packet，要求 packet sample
  rate 與 engine、channel count 與 active lane 相同，通過後沿用 `process_lane_block` 的
  immutable graph／Group Master／limiter 路徑；capture packet、NaN 或格式不符都不會進 graph。
- `PersistentLinearResampler` 保存跨 block 的 phase 與 boundary frame，要求 caller 提供
  整個 input block 的 output capacity，並拒絕在不足時部分消耗；它是 clock-drift/SRC 的
  無配置 baseline，尚未宣稱 production-quality polyphase filter。
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
  仍可運作但 UI 必須顯示 detached／dropped blocks，不能宣稱已套用 Hibiki graph。
- `AudioEngineModel::process_asio_transport` 將一個已 pop 的 ASIO block 暫時置入指定 Lane，
  走現有 immutable graph 與唯一 Group Master，再寫入 caller-owned output；完成後還原 caller
  的 Lane view。它只證明 user-space graph data path，不代表已連接實體 sink、WaveRT endpoint
  或 vendor ASIO。
- `AudioEngineModel::process_lane_block` 是同一條 caller-owned block API，讓 TabCapture、
  Virtual Mic 或其他已驗證來源共用 Lane／Group Master 行為；平台 bridge 不得在 RT path
  配置、等待或直接操作 Windows COM。
- `process_tab_capture_lane_v1` 可選接一個已按相同 sample rate 設定的
  `ProgramAwareLevelControllerV1`；它在進 graph 前對該 tab 套用慢速 RMS 代理音量，速率與
  boost/cut 都受 policy 限制。這是可選的內容音量，不是靜默擷取，也不是降噪或 BS.1770
  conformance。
- 同一個 tab effects contract 可選套用 `BasicNoiseSuppressorV1`：固定高通＋downward gate，
  只接受 1–8 聲道且要求 sample rate/channel 完全相符。它是可測試的基本抑噪，不宣稱
  RNNoise、頻譜 AI、AEC 或麥克風權限處理；效果順序為 PEQ → IR → basic suppressor → level。
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
- `WindowsWasapiOutputV1` 提供 user-space physical sink boundary：同一個 dedicated sink worker
  apartment 以 endpoint ID 綁定 shared-mode Float32 2/6/8 聲道與固定 sample rate，再由該 worker
  的 `render` 處理 padding、WASAPI buffer copy、ReleaseBuffer。格式不符、裝置不存在或 buffer
  不足都回傳失敗；Hibiki graph RT thread 不得呼叫此 COM API、初始化 COM、配置或重新綁定，
  control plane 只能排程 worker command。
- `VirtualMicRouteModel` 提供未來 Virtual Mic endpoint 的 user-space capture/reference contract：
  固定 1/2 聲道與 44.1/48/96/192 kHz、privacy mute 預設開啟、caller-owned capture 與
  render echo-reference copy。可選 `VirtualMicDspV1` 以固定 128-tap 上限做 normalized-LMS
  reference cancellation 與慢速 noise gate；這是 bounded baseline，不宣稱 acoustic AEC、
  RNNoise 或 conformance，driver/IPC/permission indicator 仍需另行驗收。

## 未解問題（阻擋完整 driver 實作）

1. 已固定 endpoint topology/channel mask；仍需 WaveRT／KS PortCls wiring 與 INF/HLK 測試矩陣。
2. 真實多輸出 endpoint 的 clock drift、ring buffer 與 adaptive SRC soak；portable stream
   ring 與 user-space per-sink SRC baseline 已存在。
3. Virtual Mic 的 echo reference、privacy indicator 與卸載行為。

在上述問題由 ADR 與測試 fixture 定案前，本 Spec 保持 draft；可以繼續完成 user-space
契約與模擬器，但不得宣稱已具備可安裝的虛擬音效驅動。

## 驗收方向

使用合成 Lane fixture 驗證 2.0／5.1／7.1 mapping、重複 Lane 拒絕、裝置交易 rollback，
並在真正 driver 進入主線後增加拔插、睡眠、Audio Service restart 與五來源八小時 soak test。
