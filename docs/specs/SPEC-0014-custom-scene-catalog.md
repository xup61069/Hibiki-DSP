---
id: SPEC-0014
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/**scene_catalog*", "src/hub/**engine_control*", "schemas/scene-definition-v1.schema.json", "tests/**"]
---

# SPEC-0014：自定義 Scene catalog 與 SceneApply resolver

## 成功條件

控制平面可以保存最多 32 個自定義 `SceneDefinitionV1`，每個 definition 同時包含
`SceneProfileV1`、`GraphConfigV1` 與 `EqualLoudnessPolicyV1`。`SceneApply` 對四個內建
ID 維持原本行為；其他 ID 必須由 catalog 解析，且 payload 的 output group 必須與保存的
Scene 完全相同，避免 UI 顯示一組 Scene 卻把音訊送到另一個 group。

## 交易與容量

- `SceneCatalogV1::upsert` 先建立完整 replacement，再以 slot swap 原子替換；配置失敗
  不得留下半份 Scene。
- Scene ID 適合現有 bounded IPC payload，限制為 1–31 bytes；名稱最多 120 bytes。
- definition 必須通過 Scene、Graph、ISO policy 驗證；Strict Direct latency mode 與 graph
  的 `strict_direct` 必須一致。
- `EngineControlWorkerV1` 只在 control worker 呼叫 resolver、執行 preflight 與
  Validate → Prepare → Commit；RT thread、pipe callback 不讀 catalog。
- catalog pointer 是 non-owning；其生命週期必須覆蓋所有 SceneApply 消費，換機／換 AI 不得
  靜默重建 Scene ID。

## 失敗與相容性

未知 custom ID、output group 不一致、非法 definition 與容量耗盡都回傳 Invalid／Failed，
並保留上一個 active graph、Scene 與 revision。內建 Game／Movie／Voice／Studio 不依賴
catalog，讓沒有使用者 preset 的 fresh clone 維持向後相容。

## 驗收

1. 合法 custom Scene 可 upsert、find、替換與透過 SceneApply commit。
2. Strict Direct mismatch、非法 policy/graph、未知 ID 與 group mismatch fail-closed。
3. 32-entry capacity、remove/clear 與 replacement failure 不破壞既有 slot。
4. CTest、docs-check、source-policy 與 source-only CI gate 通過；沒有 binary 或私人裝置
   metadata 進入 catalog/schema。
