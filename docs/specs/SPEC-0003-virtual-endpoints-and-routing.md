---
id: SPEC-0003
status: draft
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 14
related_adrs: [ADR-0002]
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
- `PersistentLinearResampler` 保存跨 block 的 phase 與 boundary frame，要求 caller 提供
  整個 input block 的 output capacity，並拒絕在不足時部分消耗；它是 clock-drift/SRC 的
  無配置 baseline，尚未宣稱 production-quality polyphase filter。
- `OutputSinkModel` 將 `ClockDriftEstimator` 的 `sink/source` ratio 接到每個 persistent SRC
  的 effective source step（`base_step / ratio`）；clock observation 在 control side，音訊
  process 只讀已設定的 immutable pipeline state，後續仍需真實 USB/HDMI/Bluetooth clock fixture。
- 原生 ASIO transport 使用 Apache-2.0 固定 layout `hibiki_asio_transport_v1`，由 Engine
  control plane 建立合法的 `Local\\HibikiDSP_v1_asio` named mapping；ASIO DLL 只在 host callback
  完成後把八聲道 Float32 block 寫入 SPSC ring。Engine 端 `AsioTransportConsumerV1` 在 RT lane
  以 caller-owned buffer pop，禁止配置與等待。mapping 不存在、格式不符或 ring 滿載時，ASIO
  仍可運作但 UI 必須顯示 detached／dropped blocks，不能宣稱已套用 Hibiki graph。
- `AudioEngineModel::process_asio_transport` 將一個已 pop 的 ASIO block 暫時置入指定 Lane，
  走現有 immutable graph 與唯一 Group Master，再寫入 caller-owned output；完成後還原 caller
  的 Lane view。它只證明 user-space graph data path，不代表已連接實體 sink、WaveRT endpoint
  或 vendor ASIO。

## 未解問題（阻擋完整 driver 實作）

1. WaveRT／KS driver 的 endpoint topology、channel mask 與 INF/HLK 測試矩陣。
2. 多輸出 clock drift、ring buffer 與 adaptive SRC 的上限延遲。
3. Virtual Mic 的 echo reference、privacy indicator 與卸載行為。

在上述問題由 ADR 與測試 fixture 定案前，本 Spec 保持 draft；可以繼續完成 user-space
契約與模擬器，但不得宣稱已具備可安裝的虛擬音效驅動。

## 驗收方向

使用合成 Lane fixture 驗證 2.0／5.1／7.1 mapping、重複 Lane 拒絕、裝置交易 rollback，
並在真正 driver 進入主線後增加拔插、睡眠、Audio Service restart 與五來源八小時 soak test。
