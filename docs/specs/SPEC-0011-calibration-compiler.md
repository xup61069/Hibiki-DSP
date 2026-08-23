---
id: SPEC-0011
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/include/hibiki/calibration_compiler.hpp", "src/hub/src/calibration_compiler.cpp", "apps/control-model/CalibrationModel.cs", "schemas/calibration-response-v1.schema.json", "schemas/peq-filter-v1.schema.json"]
---

# SPEC-0011：量測頻響到 bounded PEQ 校正

## 成功條件

控制平面可以讀取 caller-supplied 的頻率響應點，驗證頻率嚴格遞增、有限值、
裝置頻率範圍與校正政策，並產生最多 16 段可交給既有 PEQ processor/exporter 的
peaking filters。超出 boost/cut 或 filter capacity 時必須標示 `limited`，不能靜默
假裝已完全校正。

## 介面與資料流

`CalibrationResponsePointV1` 對應 `measured_db` 與 `target_db`；
`compile_bounded_peq_correction_v1` 在 control plane 以最大殘差優先、log-frequency
spacing 選點，輸出 `PeqFilterV1`。結果可直接送至 Equalizer APO、CamillaDSP、REW、
Hibiki JSON exporter；IR exporter 仍接受另外量測的 caller-owned impulse samples。

C# control model 以 `CalibrationModel.cs` 提供對應的強型別 `CalibrationPointV1`、
`CalibrationResponseV1`、`PeqFilterV1`、`PeqPresetV1`、`CalibrationCompilePolicyV1` 與
`CalibrationCompilerV1`，支援 `schemas/calibration-response-v1.schema.json` 與
`device_id` 欄位限制最長 260 bytes，避免無限長字串進入控制平面。
`schemas/peq-filter-v1.schema.json` 的原子 JSON 載入／保存及相同演算法的編譯與匯出。

## 限制與安全

- 這是 bounded PEQ baseline，不是房間聲學最佳化、耳機 HATS/coupler 推論或 ISO 係數 fit。
- 編譯器不讀取裝置、麥克風或檔案系統；頻響資料與裝置 identity 由上層提供。
- 最大 512 個輸入點、16 段輸出、boost +24 dB、cut -44 dB、Q 0.1–100 的 policy 上限
  固定在 source；一般模式應使用更低的 +6 dB boost。
- 產生檔案前必須保留 `limited`、政策與量測 metadata；使用者應做第二次量測驗證。

## 驗收

1. 合法、遞增的 100 Hz–10 kHz fixture 產生 deterministic filter 順序與 Q 範圍。
2. 過大的低頻誤差會被 cut cap 限制並標示 `limited`；超過 filter 數量也必須標示。
3. unsorted、NaN、重複頻率、超過 512 點或非法 policy 一律 fail-closed。
4. CTest、source-policy、docs-check 與既有 exporter checks 通過。
