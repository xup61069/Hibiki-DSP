---
id: SPEC-0008
status: accepted
owner: hibiki-maintainers
authority: architecture
last_reviewed: 2026-08-21
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
worker 解碼並交給 adapter；supervisor 的 UI／timeline producer、參數持久化與自動化排程仍
未接入，因此不能宣稱完整 host automation。

當本機提供 pinned SDK 時，`hibiki_vst3_sdk_worker` 會把該 adapter 接到既有 named-pipe
worker frame：啟動參數固定包含 pipe、module、class UID、sample rate 與 2/6/8 channels；
Hello/Heartbeat 沿用既有 frame，ProcessBlock 以 caller-owned packet 驗證後交給 SDK，成功
回傳 ProcessBlockResponse，plugin 或格式錯誤回傳 Error。這個 target 不會在一般 CI 或
public source-only checkout 自動生成，且仍不提供第三方 plugin binary。

## 尚未完成的邊界

plugin scan 的 factory metadata catalog、單一主 bus SDK dispatch adapter、bounded parameter
frame 與 optional worker executable 已有 bridge；仍未完成第三方 plugin certification、supervisor
launch integration、parameter timeline/persistence、side-chain/multi-bus、latency compensation、crash dump redaction 與 production
worker policy。目前 supervisor、named pipe、passthrough worker、catalog 與 bounded SDK
processor 提供可測試的 process containment/metadata/processing boundary，不能宣稱已完成
VST3 host。
