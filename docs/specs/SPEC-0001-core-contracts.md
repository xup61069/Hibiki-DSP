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
Lane、output group、channel map、DSP chain、latency mode 與安全策略；裝置替換不會
重生 endpoint、ASIO 或 IPC identity。

## 介面

- `SceneProfile v1`：lane routing、DSP、output group、automation、calibration reference。
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
- `AudioSessionRegistry` 以 `endpoint_id + session_instance_id` 作為唯一 session key；PID
  只作顯示／診斷用途。OS metadata refresh 不得覆蓋使用者已選 lane、output group 或 gain
  owner，避免同一 process 的多個 Chrome tab／session 互相串音。
- Windows `IAudioSessionManager2` adapter 的 `OnSessionCreated` callback 只遞增 sequence；
  worker 才呼叫 enumerator、讀取 instance/session ID、PID、display name 與 active state，
  再 upsert registry。這個邊界禁止在 OS callback 裡 QueryInterface、分配或改寫 graph。
- Session volume 的 canonical control 以 dB 表示，worker 呼叫 `ISimpleAudioVolume` 時才轉
  成 0–1 scalar，所有寫入帶 event-context GUID 並 read-back；session API 只適用 shared-mode，
  exclusive／vendor ASIO 仍標示 bypass。

## 不變條件

- RT thread 不 allocation、lock、wait 或呼叫 UI/COM/filesystem。
- graph change 必須 Validate → Prepare → Commit；失敗回復 last-known-good。
- Group Master 只能套用一次；LFE 不重複套用 ISO。
- Strict Direct 是獨立 bit-perfect Scene，不能偷偷混入 DSP 或 Windows gain。
- `AudioEngineModel` 的 control plane 只在 pending snapshot 做 Validate → Prepare → Commit；
  RT `process` 只讀 active immutable snapshot，再對整個 Group Master 套用一次。

## 相容性與驗收

Schema 使用明確 `schema_version`；新增欄位必須向後相容，破壞性變更建立新版本與 migration。用
合成 fixture 測試 2.0、5.1、7.1 與裝置切換。
