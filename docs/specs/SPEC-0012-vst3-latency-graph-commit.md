---
id: SPEC-0012
status: accepted
owner: hibiki-maintainers
authority: architecture
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["vst-host/**", "src/hub/**", "schemas/latency-graph-commit-v1.schema.json", "schemas/vst3-parameter-timeline-v1.schema.json", "tests/**"]
---

# SPEC-0012：VST3 lane 延遲對齊提交

## 目的

第三方 VST3 的 plugin-reported latency 只能在 worker/control plane 讀取；audio callback
不可等待 worker，也不可在 callback 內重新配置延遲線。此規格把量測結果變成固定容量、可驗證、
可回復的 graph commit，供上層將延遲線預先配置後再交給 RT graph。

## 資料與限制

- 每個 lane 使用 control-plane 配發且跨 graph revision 穩定的非零 `lane_token`；不得使用
  PID、指標或暫時的 vector index 當身份。
- 最多 32 條 lane，聲道只接受 LPCM 2/6/8，單 lane reported latency 與總補償上限為
  16,384 samples。
- `maximum_latency_samples` 取 active lane 的最大 reported latency；每條 lane 的
  `compensation_delay_samples = maximum - reported`。inactive lane 的 reported latency 固定
  為 0，但仍保留 token 與 channel layout，避免 lane index 漂移。
- `target_graph_revision` 必須大於 `base_graph_revision`；committer 只接受目前 active revision
  作為 base，stale commit 直接進入 Degraded，不改動 active plan。

## 交易流程

1. `prepare_latency_graph_commit_v1` 驗證 token 唯一性、layout、上限與數學不變量，產生完整
   pending snapshot；失敗時不部分寫入。
2. `LatencyGraphCommitterV1::prepare` 將 pending snapshot 與目前 revision 綁定。這一步只在
   control/IPC thread 執行。
3. `commit` 原子語意地替換 immutable control snapshot；`rollback` 丟棄 pending，保留原 active。
   實際 RT 延遲線必須在 commit 前由 caller-owned 固定容量資源完成 prepare，不能在 callback 配置。
4. worker crash、plugin 回報非法 latency、裝置切換或 revision mismatch 時，使用 rollback 或
   quarantine；不得把未驗證的延遲套入其他 lane。
5. graph compile 依 output group 分別取 active lane 的最大 latency，將結果寫入
   `RtLaneSnapshotV1`。`LaneLatencyBankV1` 在 Prepare 時配置所有 scratch/ring，Commit 時與
   graph snapshot 一起交換；RT mixer 只讀 bank，不在 callback 內配置。

## 驗收與邊界

- contract test 覆蓋最大延遲計算、inactive lane、重複 token、stale base、rollback 與 revision
  單調性。
- `FixedDelayLineV1` 是 worker-host 可重用的 RT primitive；hub graph 另以
  `LaneLatencyBankV1` 接入 lane mixer，兩者都以固定上限運作。仍須新增裝置重綁、sink 時鐘
  及第三方 plugin 回報延遲的端對端測試。
- 本規格不提供第三方 plugin certification、side-chain/multi-bus、state persistence 或
  WaveRT driver delivery；VST3 SDK 與 plugin 仍只可在隔離 worker 執行。
