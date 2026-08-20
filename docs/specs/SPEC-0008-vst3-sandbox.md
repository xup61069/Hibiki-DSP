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

## Worker IPC frame

`vst3_worker_protocol.hpp` 定義固定 36-byte little-endian frame header：Hello、HelloAck、
Heartbeat、ProcessBlock、ProcessBlockResponse、Shutdown、Error。Process frame 僅接受
2/6/8 聲道、1–4096 frames、exact interleaved Float32 payload，並拒絕 NaN/Inf；codec 不
配置、不擁有 payload，適合由 named pipe 或其他 control transport 在 worker 與 supervisor
間傳遞。真正 VST3 SDK/plugin dispatch 仍必須在 worker process，不能連入 graph RT。
`Vst3WorkerPipeV1` 提供 Windows overlapped named-pipe server、4-byte bounded length prefix、
connect/read/write timeout 與 caller-owned receive buffer；它是 control-plane transport，
不會被 audio callback 呼叫。

## 尚未完成的邊界

VST3 SDK 版本鎖定、plugin scan/certification、named-pipe transport、latency compensation、
crash dump redaction 與實際 worker executable 尚未納入本 commit；目前 supervisor 與 frame codec
提供可測試的 process containment，不能宣稱已完成 VST3 host。
