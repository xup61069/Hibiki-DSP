---
id: ADR-0004
status: accepted
date: 2026-08-21
supersedes: []
---

# ADR-0004：固定 endpoint topology 與 Windows channel mask

## Context

SPEC-0003 原本把 WaveRT／KS 的 endpoint topology、channel mask 與 buffer 格式列為
blocking question。若只保存聲道數，SYSVAD、engine、校正器與 Windows speaker order
可能各自推斷，造成 5.1／7.1 聲道錯位。

## Decision

以 `driver/include/hibiki/endpoint_topology_v1.h` 的 MS-PL catalog 作為第一份 topology
真值，固定四個 endpoint：

- Main：stereo render，256-frame default。
- Low Latency：stereo render，64-frame default。
- Surround：7.1 render，Windows mask `0x63f`（front/center/LFE/back/side）。
- Virtual Mic：stereo capture，128-frame default。

每個 descriptor 同時攜帶永久 endpoint GUID、方向、default sample rate、supported-rate
flags、buffer size、channel count 與 channel mask。未來 PortCls/SYSVAD topology 只能消費
此 catalog；user-space graph 仍透過 Apache-2.0 control ABI 與 driver 溝通，不連結 GPL code。

## Consequences

聲道排列與端點身份可在沒有 WDK 的環境先做 portable contract test，降低 AI／換電腦接手時
自行重生 topology 的風險。這不等於已完成 miniport、INF/HLK、Microsoft signing 或實機
拔插驗收；那些仍是後續 release gate。
