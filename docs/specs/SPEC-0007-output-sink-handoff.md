---
id: SPEC-0007
status: accepted
owner: hibiki-maintainers
authority: architecture
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/**output*", "src/hub/**wasapi*", "src/hub/**audio_engine*", "src/hub/include/hibiki/output*", "src/hub/src/output*"]
---

# SPEC-0007：多輸出 sink 交接與時鐘連續性

每個 physical sink 由獨立 ring/SRC pipeline 處理。新 sink 必須先完成 format、clock
與 buffer prepare，再進入交叉淡化；切換失敗時保留舊 sink，不重啟全域 graph。

`OutputFanoutPlanV1` 的每個 bounded sink ID 使用與 schema 相同的 printable UTF-8 boundary：
控制字元、overlong、surrogate、truncated 或其他 malformed UTF-8 均在 prepare/validate 時
fail-closed；合法 multi-byte printable ID 保持可用。

## Crossfade

- `OutputCrossfade` 由 control plane 以 2/6/8 聲道、44.1/48/96/192 kHz 與 1–200 ms
  duration prepare；預設交接 duration 為 30 ms。
- audio-side `process` 不配置、不鎖、不呼叫 COM，使用 equal-power sin/cos 權重；完成後
  僅輸出新 sink。buffer 不足、null pointer、非有限輸入或未開始都回傳 failure，不寫部分結果；
  兩個 caller-owned interleaved input 的所有 samples 都必須先通過 finite 檢查。
  `process` 在讀取 caller-owned input 或寫入 output 前檢查 interleaved frame geometry
  不超過 `SIZE_MAX / channels`，並對 `processed_frames + frames` 做 fail-closed 檢查；
  溢位請求不改變 crossfade snapshot。已驗證的 bounded overshoot 仍可在完成淡化時
  將 progress clamp 到 `total_frames`。

## Clock 與回復

- crossfade 與 `OutputSinkModel` 的 persistent SRC 可連續串接；clock drift 只由 control
  plane 更新 ratio，audio thread 讀 immutable snapshot。persistent SRC 是固定容量
  8 phase × 16 tap polyphase FIR bank，支援 2/6/8 聲道與既有 0.25x..4.0x source-step
  envelope；跨 block 攜帶 fractional phase 與 15-frame history，ratio 變更不重置 stream。
  prepare/process 邊界維持 no-allocation、no-mutex、no COM；invalid channels/step 或
  capacity 不足都 fail-closed。Stateless `linear_resample_interleaved` 另在讀取或寫入
  caller-owned interleaved buffer 前檢查 input/output frames 均不超過
  `SIZE_MAX / channels`；無法表示的 geometry 直接回傳 failure，不留下部分輸出。
  PersistentPolyphaseResampler 的 `process` 也會在讀寫 caller-owned interleaved buffer 前
  檢查 input/output capacity 均不超過 `SIZE_MAX / channels`；`required_output_frames`
  對 history-end 與 output-count 算術溢位 fail-closed，失敗不更新 phase/history。這是
  representability 邊界，不另訂新的 render block-size 上限。
- USB/HDMI/Bluetooth 拔插或 Audio Service invalidation 由 `DeviceRecoveryCoordinator`
  進入 safe-start；不得回到 0 dB、100% 或未驗證的舊 endpoint。
- `WindowsWasapiSinkWorkerV1` 將 COM/WASAPI 完整限制在單一 dedicated sink worker apartment：
  graph/ASIO/TabCapture producer 只呼叫 bounded SPSC `submit`，worker 在 endpoint event 後
  pop block，空 queue 補 silence，並透過 `OutputSinkModel` 的 persistent SRC 處理已排程的
  clock observation。`observe_clock` 以 odd/even publication token 發佈完整的 atomic
  latest-request；重疊 writer 直接丟棄 request，worker 只在前後 token 相同且 stable 時套用
  source/sink/elapsed 完整 tuple。實際 SRC 更新在 worker 套用；graph RT 不呼叫 COM、等待或
  配置。caller 一律提供 Float32，worker render boundary
  依 endpoint mix format 無配置地寫入 Float32、PCM16、PCM24 或 PCM32。
- `WindowsWasapiOutputV1` 在 bind 時取得 `IAudioClock`，worker 以 device position 與 QPC
  delta 產生每 sink 的 source/sink frame observation，再由 `OutputSinkModel` 更新 SRC；
  clock 讀取失敗時保留原本的外部 observation／安全 fallback。多聲道 Float32／PCM mix
  format 若為 extensible，channel mask 必須符合 2.0／5.1／7.1 layout；5.1 接受 back 或
  side speaker variant，避免合法 Windows endpoint 被錯誤標成 detached。
- worker snapshot 必須可觀察 `endpoint_ready`、`degraded`、dropped/submitted/rendered blocks、
  `source_step` 與 `drift_ppm`，讓 UI 在實體 endpoint 未綁定時顯示 detached，而不是靜默宣稱
  已輸出。
- `WindowsWasapiSinkWorkerV1` 對 `AUDCLNT_E_DEVICE_INVALIDATED` 與
  `AUDCLNT_E_SERVICE_NOT_RUNNING` 只在 dedicated worker 內執行 bounded bind/start retry；
  普通 event timeout 不會誤觸發重綁。重綁成功恢復 `endpoint_ready`，連續失敗才標記
  `degraded`，不會由 graph RT thread 直接操作 COM。
- 真實 WaveRT endpoint、硬體 clock fixture、DPC/拔插 soak 不在本機
  contract test 中，必須在 Windows 11 24H2+ test machine 完成。
- 本節的 persistent SRC contract test 是 user-space DSP evidence only；不得推論真實
  multi-output clock drift、USB/HDMI/Bluetooth 拔插 soak、WaveRT/driver rate behavior、
  HLK、signing 或 physical audio delivery 已通過驗收。

## WASAPI endpoint handoff

`WindowsWasapiSinkHandoffV1` 以兩個 dedicated `WindowsWasapiSinkWorkerV1` 實例實作
prepare → fade → commit／rollback。候選 endpoint 會先獨立 bind、暖機並通過
`endpoint_ready`；只有同聲道 layout、與目前 graph 相同的 sample rate、以及 1–200 ms
fade 設定才可開始，避免在尚未建立明確 SRC 邊界時把不同速率的 block 直接交給 sink。
交叉淡化期間 graph block 只提交一次，worker 邊界套用 bounded equal-power sin/cos gain；
audio graph 不重啟、不直接呼叫 COM。候選 submit、endpoint bind 或舊 sink render 失敗時，
候選立即停止並回到舊 sink；舊 sink 不可用則進入 `Degraded`，不得假裝已同步。

`ReadyToCommit` 是唯一允許交換 active slot 的狀態；`commit` 停止舊 worker 後才翻轉
active slot，`rollback` 保留仍在執行的舊 worker。這個 user-space handoff 只證明狀態機、
bounded queue 與 source-level fallback；實體 endpoint 的暖機時間、音量連續性、拔插與
Audio Service restart 仍需 Windows 11 24H2+ 實機 evidence。

Rollback 之後只要舊 worker 仍然 `running` 且 `endpoint_ready`，狀態會保留為
`RolledBack` 並允許下一次 `begin → prepare` 重試；不能要求重啟整個 engine 或先重新
`start_initial`。舊 worker 失效時才進入 `Degraded`。

`tools/live-wasapi-handoff-check.ps1` 是 opt-in、無聲的 local probe：它讀取目前 default
render mix format，啟動 active/candidate workers，等待兩端 warm-up，先驗證一次 candidate
rollback 與舊 worker 持續運作，再重試同一候選並送出 30 ms equal-power silence crossfade
後 commit。probe 只輸出 aggregate format／state／block counters，不會寫入 endpoint
identity；沒有可接受 endpoint 時回報 `wasapi=unavailable`，不把缺少硬體當成成功。

`AudioEngineModel` 的 `start_wasapi_output`、`begin_wasapi_output_handoff`、
`prepare/commit/rollback_wasapi_output_handoff` 與 `process_output_group_to_wasapi` 是
唯一 graph-to-WASAPI adapter。前者只在 control plane 呼叫；後者先完成 output-group graph、
Group Master ramp 與 limiter，再把同一個 interleaved block 送進 handoff。若 graph layout
與 sink channels 不符、沒有 active graph、block 超過 `uint32` 或 handoff 尚未 Fading/Synced，
整個呼叫 fail-closed，不會部分提交或重啟 graph。

`WindowsWasapiFanoutV1` 將相同 graph block fan-out 到最多 8 個獨立 handoff。prepare 時要求
所有 enabled sink 使用相同 channel layout／sample rate、非空且不重複 endpoint ID；每個 sink
仍保有自己的 worker、clock、SRC 與 handoff state。任一 sink submit 失敗會將 fan-out 標記
`degraded`，後續必須由 control plane 重新 prepare，不能把部分成功當成全數同步。此邊界仍
只證明 bounded user-space coordination，實體多裝置時鐘與拔插 soak 需另行驗收。

## Multi-sink fan-out

`OutputFanoutPlanV1` 將同一個 graph block 複製到最多 8 個同聲道 layout 的 sink。所有 enabled
sink 的 pointer／capacity 在第一次寫入前一次驗證；任何容量不足或 plan 無效都 fail-closed，
不會只更新部分 sink。對 persistent runtime，caller 若提供涵蓋全部 planned sink 的
`output_frames` span，任何拒絕路徑也會先將每個 planned sink 的 frame count 清為 0，避免
caller 看見上一次成功 block 的 stale metadata。`fanout_interleaved_v1` 與 persistent runtime 共用
`kOutputFanoutMaxInputFramesV1` 的 4096-frame 上限，並在掃描或複製前檢查
`frames * output_channels` 的 bounded geometry；超限或無法表示的 geometry 一律不讀 input、
不寫任何 sink。每個 sink 後續仍由自己的 ring、clock drift 與 SRC worker 處理；fan-out 本身
不碰 COM、裝置或 physical endpoint。

每個 sink 的 `sink_id` 是 1..64 bytes 的 printable label：JSON schema 與 runtime validator
一致地拒絕 C0/C1 控制字元（含 NUL、tab、newline、CR、DEL 與 0x80–0x9F），只接受可顯示的
UTF-8 名稱。這讓 UI 與 downstream consumer 不需要處理不可見字元的 edge case。

`OutputFanoutRuntimeV1` 將每個 enabled sink 綁定一個 persistent `OutputSinkModel`。control
側的 `observe_clock` 只驗證並發佈每個 sink 的 bounded latest-request；audio-side `process`
在容量 preflight 前由唯一 owner 套用一個完整且穩定的 request。control-side `snapshot` 只讀
audio-side 發佈的 coherent snapshot，不直接讀取可變 SRC state。process 先做有限值與容量
上限 preflight，再把各 sink 的 SRC 結果寫入 prepare 階段配置的 scratch，所有 sink 成功後
才一次發佈到 caller-owned output buffers。任何 sink 失敗會回復 SRC state 與該次時鐘進度，
不留下部分輸出；每次最多 4096 input frames、輸出上限按 0.25x source step 的固定界限只用
於 prepare-time scratch，並保留 persistent SRC 的 15-frame history；caller-owned capacity 則依
每個 sink 當下 phase 與 source step 精確 preflight。這是 user-space bounded runtime，仍不等於
真實硬體 sink／clock soak 證據。

每個 clock request 與 sink snapshot 都使用三個固定 slots、release/acquire payload 欄位、
以及 generation／slot／phase publication token。writer 先以 odd token 標記目標 slot 為
in-progress，完整寫入後才發布 stable token；reader 先宣告 hazard slot 並重新驗證 token，
writer 不得覆寫被保護的 slot。觀察到 odd、changed token、非法 slot 或無法完成 handshake
時一律 fail-closed。這個 bounded lifetime/reuse proof 讓 request 與 snapshot 都只能被接受為
單一完整 generation，不依賴 relaxed 多欄位 seqlock；reset／prepare 使既有 token 失效時也
不得清除 in-flight reader hazard，直到 reader guard 完成，避免第一個 post-reset generation
重用仍在讀取的 slot；它仍只是 user-space coordination。

`AudioEngineModel::prepare_wasapi_fanout` 與 `process_output_group_to_wasapi_fanout` 將上述
physical sink fan-out 接到 graph：graph、Group Master 與 limiter 只執行一次，之後同一個
interleaved block 交給每個 enabled WASAPI handoff。fan-out plan 無效、沒有 active graph、
layout 不符或任一 sink degraded 時回傳失敗；這個 API 不會把部分 sink 成功轉成全域成功。

`AudioEngineModel::process_output_group_fanout` 會先完成指定 output group 的 graph、Group
Master ramp 與 limiter，再把同一個 graph block 交給 fan-out runtime；因此 caller 可在不複製
graph 或重啟引擎的情況下，同時取得多個 sink 的獨立 SRC 結果。fan-out plan 的 output layout
必須與 active graph 完全相同，否則 fail-closed。
