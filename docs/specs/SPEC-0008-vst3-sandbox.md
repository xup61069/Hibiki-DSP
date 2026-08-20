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
- `mark_heartbeat` 與 `poll_watchdog` 只在 control/IPC worker 呼叫，禁止 audio callback
  使用 Win32 handle 或等待。

## 尚未完成的邊界

VST3 SDK 版本鎖定、plugin scan/certification、音訊 buffer IPC、latency compensation、
crash dump redaction 與實際 worker executable 尚未納入本 commit；目前 supervisor
提供可測試的 process containment，不能宣稱已完成 VST3 host。
