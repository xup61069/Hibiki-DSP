---
id: SPEC-0011
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-24
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/include/hibiki/calibration_compiler.hpp", "src/hub/src/calibration_compiler.cpp", "apps/control-model/CalibrationModel.cs", "apps/control-model/EasyControlViewModel.cs", "apps/control-model/IpcProtocol.cs", "src/hub/include/hibiki/ipc.hpp", "src/hub/src/ipc.cpp", "src/hub/include/hibiki/control_payloads.hpp", "src/hub/src/control_payloads.cpp", "schemas/calibration-response-v1.schema.json", "schemas/peq-filter-v1.schema.json"]
---

# SPEC-0011：量測頻響到 bounded PEQ 校正

## 成功條件

控制平面可以讀取 caller-supplied 的頻率響應點，驗證頻率嚴格遞增、有限值、
裝置頻率範圍與校正政策，並產生最多 16 段可交給既有 PEQ processor/exporter 的
peaking filters。超出 boost/cut 或 filter capacity 時必須標示 `limited`，不能靜默
假裝已完全校正。

## 介面與資料流

`CalibrationResponsePointV1` 對應 `measured_db` 與 `target_db`；兩個 dB 值的
schema contract 限制在 -144..12，先拒絕超出實際音量與安全 headroom 範圍的量測值。
`compile_bounded_peq_correction_v1` 在 control plane 以最大殘差優先、log-frequency
spacing 選點，輸出 `PeqFilterV1`。結果可直接送至 Equalizer APO、CamillaDSP、REW、
Hibiki JSON exporter；IR exporter 仍接受另外量測的 caller-owned impulse samples。

C# control model 以 `CalibrationModel.cs` 提供對應的強型別 `CalibrationPointV1`、
`CalibrationResponseV1`、`PeqFilterV1`、`PeqPresetV1`、`CalibrationCompilePolicyV1` 與
`CalibrationCompilerV1`，支援 `schemas/calibration-response-v1.schema.json` 與
`device_id` 欄位限制最長 260 bytes，避免無限長字串進入控制平面。
schema 層對 `measured_db` 與 `target_db` 限制為 -144 至 +12 dB，與系統
volume floor 和 safety ceiling 一致，拒絕不合理量測值進入控制平面。
`schemas/peq-filter-v1.schema.json` 的原子 JSON 載入／保存及相同演算法的編譯與匯出。
schema 層的 `device_id` 上限為 260 字元（`maxLength: 260`）且拒絕 C0/C1 控制字元
（anchored printable exclusion pattern）；C# 控制模型進一步以 UTF-8 位元組數實施 260-byte
上限，並用 `char.IsControl` 執行相同的控制字元排除，與 physical device catalog 的
endpoint ID 邊界一致。多位元組文字即使不超過 260 個字元，也會在進入控制平面或
校正檔前被拒絕。

## 引導式校正與目標曲線（#1564）

引導式校正工作流在既有 bounded PEQ compiler 上加入三條內建目標曲線：
`flat`（0 dB 全頻段）、`harman-in-ear` 與 `harman-over-ear`。曲線以 bounded anchor table 定義，
在 log-frequency 空間做線性內插，全部以 1 kHz = 0 dB 為基準；這些是產品化目標偏好，不是量測
麥克風資料，也不使用任何受限等響度（equal-loudness）表格。

`sample_calibration_target_curve_v1`（C++）與 `CalibrationCompilerV1.TrySampleTargetCurve`（C#）
對 10 Hz–24 kHz 以外的頻率或未知 curve ID 一律 fail-closed。`BuildTargetedResponse` 讓上層
wizard 把 caller-supplied 的量測電平配對到所選曲線後直接取得合法的 `CalibrationResponseV1`；
未排序頻率或超出 schema dB 範圍會被拒絕。

`CompileMultiChannelBatch` 接受固定 1／2／6／8 聲道陣列並逐聲道呼叫同一個 bounded compiler：
任一聲道驗證失敗時整批 fail-closed 且不回傳部分結果；任一聲道被 boost/cut cap 或 filter 數量
限制時，批次結果標示 limited。多聲道輸出仍受每聲道最多 16 段 PEQ 上限。

### Engine apply wire boundary

校正結果送往 engine control plane 使用 v1 `CalibrationPeqPrepare` 訊息（`IpcMessageType = 23`）。
其 payload 固定為 464 bytes、little-endian positional layout：`[0..3]` 為 schema version
`uint32 = 1`；`[4]` 為 filter count（1–16）；`[5]` 為 output-group UTF-8 byte count（1–64）；
`[6]` 為 `clear_existing`（目前必須為 0）；`[7..15]` 為保留且必須全 0；`[16..79]` 為
NUL-padding 的 printable UTF-8 output group；`[80..463]` 為 16 個 24-byte filter entry，
每筆依序為 little-endian `f64 frequency_hz`、`f64 gain_db`、`f64 Q`。未使用的 entry 必須全 0。

每個 filter 必須符合 10–22000 Hz、-24..+24 dB、Q 0.05–20.0；schema version、reserved bytes、
group padding、有限值與上述範圍任一不符時，decoder 必須拒絕整個 payload，不產生部分命令。
拒絕時 C++ `CalibrationPeqPrepareCommandV1` 保持 value-initialized（schema version 1、filter
count／group length 皆為 0，文字與 filter 全為 neutral），包括已讀取有效 filter 後才發現未使用
entry 非零的情況。
這個固定容量命令只代表 control plane 已接受／入列；UI 不得在收到 Ack 時宣稱音訊已完成套用。
真正的 prepare／commit 由 engine control worker 執行，audio-side 的處理順序為既有 IR、等響度
PEQ、calibration PEQ、Group Master、limiter；Strict Direct 仍不套用這條 calibration PEQ 路徑。

## 限制與安全

- 這是 bounded PEQ baseline，不是房間聲學最佳化、耳機 HATS/coupler 推論或 equal-loudness 係數 fit。
- 編譯器不讀取裝置、麥克風或檔案系統；頻響資料與裝置 identity 由上層提供。
- 最大 512 個輸入點、16 段輸出、boost +24 dB、cut -44 dB、Q 0.1–100 的 policy 上限
  固定在 source；一般模式應使用更低的 +6 dB boost。
- 產生檔案前必須保留 `limited`、政策與量測 metadata；使用者應做第二次量測驗證。

## 驗收

1. 合法、遞增的 100 Hz–10 kHz fixture 產生 deterministic filter 順序與 Q 範圍。
2. 過大的低頻誤差會被 cut cap 限制並標示 `limited`；超過 filter 數量也必須標示。
3. unsorted、NaN、重複頻率、超過 512 點或非法 policy 一律 fail-closed。
4. CTest、source-policy、docs-check 與既有 exporter checks 通過。
5. 實際載入 repository schema 的 validator 對 `device_id` 驗證 U+0000–U+001F、U+007F–U+009F
   在開頭、中間、結尾及 controls-only 位置皆拒絕，並保留合法 printable UTF-8；runtime 測試或
   pattern 文字檢查不能取代 schema-instance 驗收。
