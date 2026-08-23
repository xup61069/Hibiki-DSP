---
id: SPEC-0014
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-23
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/**scene_catalog*", "src/hub/**engine_control*", "apps/control-model/**Scene*", "apps/winui-shell/**", "schemas/scene-definition-v1.schema.json", "schemas/custom-scene-cards-v1.schema.json", "tests/**"]
---

# SPEC-0014：自定義 Scene catalog 與 SceneApply resolver

## 成功條件

控制平面可以保存最多 32 個自定義 `SceneDefinitionV1`，每個 definition 同時包含
`SceneProfileV1`、`GraphConfigV1` 與 `EqualLoudnessPolicyV1`。`SceneApply` 對四個內建
ID 維持原本行為；其他 ID 必須由 catalog 解析，且 payload 的 output group 必須與保存的
Scene 完全相同，避免 UI 顯示一組 Scene 卻把音訊送到另一個 group。

控制模型同時提供一個最多 32 筆的 UI Scene card mirror。它只保存可顯示的 ID、名稱、說明、
延遲標籤與安全旗標；完整 graph、loudness 與 calibration 仍由引擎端
`SceneDefinitionV1` 管理。UI mirror 不得覆寫四個內建 ID，選取自訂卡片仍只能送出既有
`SceneApply(scene_id, output_group)`，因此 output-group exact-match 與引擎端 fail-closed
規則不會被繞過。

UI mirror 以 `custom-scene-cards-v1.schema.json` 保存到 user-space 的本機設定檔；寫入採同目錄
暫存檔替換，載入先完整驗證後才交換 catalog。檔案只含顯示卡片，不含裝置 ID、校正資料、
plugin state 或完整 graph；載入失敗時保留目前記憶體內容。

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

## 正式殼層的本機卡片移除

正式 WinUI 殼層列出本機自訂卡片，並為每個移除操作提供非空無障礙名稱。移除只作用於 UI
mirror 與本機 `custom-scene-cards-v1.schema.json` 檔案；引擎端 `SceneDefinition` catalog 不會被
刪除或改寫。ViewModel 先更新記憶體 mirror，再以既有暫存檔替換流程保存；未知或內建 ID
fail-closed，保存失敗時回復原卡片與選取狀態並顯示可讀錯誤。選取自訂卡片仍只能送出既有
`SceneApply(scene_id, output_group)`。

## 驗收

1. 合法 custom Scene 可在引擎 catalog upsert、find、替換並透過 SceneApply commit；C# UI
   mirror 可 upsert、移除、列舉與選取同一個 ID。
2. Strict Direct mismatch、非法 policy/graph、未知 ID 與 group mismatch fail-closed。
3. 32-entry capacity、remove/clear 與 replacement failure 不破壞既有 slot。
4. CTest、docs-check、source-policy 與 source-only CI gate 通過；沒有 binary 或私人裝置
   metadata 進入 catalog/schema。
