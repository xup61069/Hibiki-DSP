---
id: SPEC-0014
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/**scene_catalog*", "src/hub/**engine_control*", "apps/control-model/**Scene*", "apps/winui-shell/**", "schemas/scene-definition-v1.schema.json", "schemas/custom-scene-cards-v1.schema.json", "schemas/scene-profile-v1.schema.json", "schemas/scene-sync-queue-v1.schema.json", "tests/**"]
---

# SPEC-0014：自定義 Scene catalog 與 SceneApply resolver

## 成功條件

控制平面可以保存最多 32 個自定義 `SceneDefinitionV1`，每個 definition 同時包含
`SceneProfileV1`、`GraphConfigV1` 與 `EqualLoudnessPolicyV1`。`SceneApply` 對四個內建
ID 維持原本行為；其他 ID 必須由 catalog 解析，且 payload 的 output group 必須與保存的
Scene 完全相同，避免 UI 顯示一組 Scene 卻把音訊送到另一個 group。

控制模型同時提供一個最多 32 筆的 UI Scene card mirror。它只保存可顯示的 ID、名稱（字串）、
說明（字串）、延遲標籤（字串）、optional IR 參照與安全旗標；完整 graph、loudness
與 calibration 仍由引擎端 `SceneDefinitionV1` 管理。UI mirror 不得覆寫四個內建 ID，
選取自訂卡片仍只能送出既有
`SceneApply(scene_id, output_group)`，因此 output-group exact-match 與引擎端 fail-closed
規則不會被繞過。

UI Scene card mirror 的 `name`、`description` 與 `latency_label` 在持久化 schema 中
必須明確宣告為字串，且三個可顯示文字欄位都拒絕控制字元（含換行、tab 與不可見
C0/C1 字元）；長度與非空白限制維持不變，讓外部 schema 驗證、C# runtime parser
與 UI 顯示面對 printable contract 有一致的 fail-closed 行為。

UI mirror 以 `custom-scene-cards-v1.schema.json` 保存到 user-space 的本機設定檔；寫入採同目錄
暫存檔替換，載入先完整驗證後才交換 catalog。檔案只含顯示卡片，不含裝置 ID、校正資料、
plugin state 或完整 graph；卡片可另帶 8–64 bytes 的可列印 UTF-8 `ir_reference`，
讓同一校準參照能進入引擎 SceneApply retention 判斷。載入失敗時保留目前記憶體內容。
卡片另可帶 optional boolean `loudness_live_update`（預設 false）：true 時控制模型在 Upsert
的 `SceneCatalogCommandV1` wire byte 84 寫入 1，讓引擎端 `EqualLoudnessPolicyV1`
以 volume-driven phon recompute opt-in 建立該自訂 Scene；false 或省略時 byte 84 為 0。
序列化時，預設 false 不寫出 `loudness_live_update` 欄位；只有 true 會出現在卡片檔。此旗標只改變
engine-side loudness attachment 的 live-update 語意，不擴大 UI mirror 對完整
graph/loudness 參數的所有權。

## 引擎同步管線

UI 新增或刪除自訂場景卡且控制管線已連線時，控制模型以 `SceneCatalogCommandV1`
（IpcMessageType 20）向引擎推送 Upsert 或 Remove 操作。payload 使用固定 3260-byte
wire format，欄位位置互不重疊：header 與 enum 位於 [0..23]，七組 IEEE-754 f64 參數位於
[24..79]，三段 bounded string（scene_id、scene_name、output_group）與兩段 optional
reference 字串分別佔用固定區間，timeline ID 表與最多 4 條 lane record 接在後方。

編碼與解碼必須對稱且嚴格：schema 版本、operation 範圍、zero-padding、bounded-string
可列印 UTF-8、enum 邊界、finite double、lane/timeline 容量任一驗證失敗即拒收，
不得部分套用。C++ 與 managed C# encoder 都必須先拒絕 Upsert、Remove、Clear 以外的
operation underlying value，不得產生明知會被 decoder 拒收的 payload。make-up gain 以
Q16.16 定點數傳輸；channel matrix 以 f32 bit-level 序列化。
引擎收到 Upsert 後重建完整 `SceneDefinitionV1`，通過既有 Scene/Graph/equal-loudness policy 驗證後
才進入 catalog；Remove/Clear 同樣走原子替換。

## 交易與容量

- `SceneCatalogV1::upsert` 先建立完整 replacement，再以 slot swap 原子替換；配置失敗
  不得留下半份 Scene。
- Scene ID 適合現有 bounded IPC payload，限制為 1–31 bytes；名稱最多 120 bytes。
- definition 必須通過 Scene、Graph、equal-loudness policy 驗證；Strict Direct latency mode 與 graph
  的 `strict_direct` 必須一致。
- `EngineControlWorkerV1` 只在 control worker 呼叫 resolver、執行 preflight 與
  Validate → Prepare → Commit；RT thread、pipe callback 不讀 catalog。
- 成功的 `SceneApply` 必須把解析出的 `EqualLoudnessPolicyV1` 一起換入
  `active_loudness()`；commit 失敗時連同 Scene、graph 與 revision 一併回復，讓控制端
  可以檢查引擎實際接受的 loudness 設定。
- catalog pointer 是 non-owning；其生命週期必須覆蓋所有 SceneApply 消費，換機／換 AI 不得
  靜默重建 Scene ID。

## 失敗與相容性

未知 custom ID、output group 不一致、非法 definition 與容量耗盡都回傳 Invalid／Failed，
並保留上一個 active graph、Scene、loudness policy 與 revision。內建
Game／Movie／Voice／Studio 不依賴 catalog，讓沒有使用者 preset 的 fresh clone 維持向後相容。
wire format 解碼失敗不影響已存在的 catalog 內容或 active graph。

## 正式殼層的本機卡片移除與引擎同步

正式 WinUI 殼層列出本機自訂卡片，並為每個移除操作提供非空無障礙名稱。

控制管線已連線時，ViewModel 在本機 mirror 與暫存檔保存成功後，以 `SceneCatalogCommandV1`
Remove 向引擎推送同步刪除指令。引擎在 `SceneCatalogV1::remove` 中查找該 ID：找到才釋放
該 slot 並回傳 Applied；找不到回傳 Invalid 且不影響其他 entry 或 active graph。收到非法
payload 時解碼即拒收，同樣不觸碰既有 catalog。

未連線時，新增與移除仍立即作用於 UI mirror 與本機
`custom-scene-cards-v1.schema.json` 檔案；schema 對三個顯示欄位明確要求字串型別。控制模型
同時把同一筆 Upsert／Remove 記入有界
重播佇列（含 upsert 操作的 optional boolean `loudness_live_update`），
並在卡片檔保存後以 `scene-sync-queue-v1.json` 的同目錄暫存檔替換流程原子持久化；
佇列保存失敗視同卡片變更失敗，必須回復原卡片與選取狀態。佇列上限為 64 筆，超過時捨棄最舊
操作、累計保存並顯示已捨棄數量，且後續離線操作與重播完成訊息不得覆蓋這個容量損失警告。
新 ViewModel 載入本機卡片時會先完整驗證並還原佇列與累計捨棄數；佇列檔不存在代表空佇列，
破損、未知版本或超過大小／容量上限則 fail-closed，不更換已載入的場景卡片。控制管線重新連線
成功後，ViewModel 依原順序把佇列中的 Upsert／Remove 補送到引擎。持久化格式由
`schemas/scene-sync-queue-v1.schema.json` 描述：頂層必須是 `schema_version`（固定 1）、
`dropped_operations` 與最多 64 筆 `operations`；每筆操作只允許 `is_upsert`、`scene_id`、
`name`、`output_group`、optional `ir_reference` 與 optional `loudness_live_update`
欄位。schema 以條件式規則直接強制：
Upsert 操作必須有
非空 `name` 與非空 `output_group`；「非空」在 schema 與執行期皆代表至少含一個非空白字元。
`ir_reference` 必須為空或 8–64 bytes 可列印 UTF-8；Remove 操作時必須為空字串。
控制模型對這些欄位以 UTF-8 位元組數套用與引擎一致的硬上限：`name` 最長 120 bytes、
`output_group` 最長 64 bytes、`ir_reference` 空或 8–64 bytes；超界名稱在輸入當下即被拒絕，
離線佇列載入同樣 fail-closed，
且附帶文字欄位都不得含控制字元（對齊引擎 bounded-string 的可列印 UTF-8 契約），持久化 schema 也以
invisible-control exclusion pattern（anchored）拒收 U+0000-U+001F 與 U+007F-U+009F，不會等到送出
同步指令或外部驗證才失敗。Remove 操作三個附帶文字欄位都必須為空字串，且欄位限制必須與控制模型
執行期驗證一致。離線重播的 Upsert 必須保留原卡片的 `loudness_live_update`
選擇並寫入相同的 wire byte 84；Remove 不攜帶此旗標。序列化只在 true 寫出該欄位，
false 或預設值保持欄位省略，讓檔案內容與「缺少即 false」的載入契約一致。
兩個控制模型 persistence loader 都使用 `JsonUnmappedMemberHandling.Disallow`；因此
`custom-scene-cards-v1` 與 `scene-sync-queue-v1` 的頂層及巢狀未知欄位會和
`additionalProperties: false` schema 一樣 fail-closed，合法已知欄位與省略 optional 欄位仍維持
向後相容。載入失敗時不交換既有記憶體 catalog 或 queue。
全部成功才回報「引擎已同步」，
同時保留先前捨棄數量並清空持久化佇列；中途失敗則保留剩餘操作與其持久化狀態，誠實顯示降級
狀態，待下一次連線再補送。此重播只使用既有 `SceneCatalogCommandV1` wire format 與 Ack 語意，
不改變引擎端驗證或 catalog 容量契約；已連線時的同步同樣以收到引擎 Ack 為準，不得在送出前
宣稱完成。

ViewModel 先更新記憶體 mirror，再以既有暫存檔替換流程保存；未知或內建 ID fail-closed，
保存失敗時回復原卡片與選取狀態並顯示可讀錯誤。選取自訂卡片仍只能送出既有
`SceneApply(scene_id, output_group)`。

## 正式殼層的本機卡片匯出與匯入

正式 WinUI 殼層的「自訂預設」區提供「匯出自訂預設」與「匯入自訂預設」入口。匯出以 FileSavePicker
選擇 JSON 目的地，並呼叫既有 ViewModel 匯出流程；匯入以 FileOpenPicker 選擇 JSON 來源，
呼叫既有 ViewModel 匯入流程：無效、空、超過剩餘容量的檔案在寫入前即被拒絕，不會改變目前
catalog；成功匯入後刷新卡片清單，並以 StatusText 如實回報結果。兩顆按鈕都有非空
AutomationProperties.Name 與 HelpText。此入口只重用既有 user-space 卡片保存契約，不改變
engine、wire contract 或 DesktopCompat 行為。

## 驗收

1. 合法 custom Scene 可在引擎 catalog upsert、find、替換並透過 SceneApply commit；C# UI
   mirror 可 upsert、移除、列舉與選取同一個 ID。
2. Strict Direct mismatch、非法 policy/graph、未知 ID 與 group mismatch fail-closed。
3. 32-entry capacity、remove/clear 與 replacement failure 不破壞既有 slot。
4. wire format encode→decode 往返一致；破損 lane count 或非法 padding 拒收。
5. CTest、docs-check、source-policy 與 source-only CI gate 通過；沒有 binary 或私人裝置
   metadata 進入 catalog/schema。
6. custom-scene-cards-v1 schema 對 name/description/latency_label 強制 string 型別；非字串值在 schema 驗證即拒收。
7. custom 與內建 `SceneApply` 成功後，`EngineControlWorkerV1::active_loudness()`
   分別等於 catalog definition 或內建 Easy preset 的 `EqualLoudnessPolicyV1`；
   套用失敗時保留先前的 active policy。
