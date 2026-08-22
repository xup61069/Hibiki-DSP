# ---
# id: ADR-0002
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-21
# last_reviewed: 2026-08-21
# review_after_days: 90
# supersedes: []
# related_specs: [SPEC-0002, SPEC-0003]
# source_globs: ["src/**", "driver/**", "sdk/**"]
# ---

# ADR-0002：固定虛擬端點與獨立 RT engine

## Status

Accepted

## Date

2026-08-21

## Context

Windows APO 無法可靠攔截 vendor ASIO、WASAPI Exclusive 或 RAW，且裝置切換不應重啟全域
音訊。需要讓 Windows shared、Hibiki ASIO、browser capture 與多 physical sinks 共享同一安全與
校正 graph。

## Decision

以固定 Hibiki virtual render/capture endpoints 作入口，user-space RT engine 擁有 graph 與
per-sink buffers；driver 只提供 endpoint、volume/mute 與 versioned control IPC。所有 graph change
採 transactional prepare/commit/rollback；不同硬體時鐘使用 drift estimator 與 adaptive SRC。

## Consequences

Windows volume 可以和 Hibiki ASIO 共用 canonical master；vendor ASIO bypass 必須在 UI 明示。driver
與 engine 可分別測試、分別授權，但需要精確的 distribution profile、IPC schema 與簽章安裝器。
