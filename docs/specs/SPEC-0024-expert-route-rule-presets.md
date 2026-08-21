---
id: SPEC-0024
status: accepted
owner: hibiki-maintainers
authority: control-model
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0001, ADR-0004]
source_globs: ["apps/control-model/SessionRouteRuleCatalog.cs", "apps/control-model/ControlModel.cs", "apps/control-model/EasyControlViewModel.cs", "apps/winui-shell/MainWindow.xaml", "apps/winui-shell/MainWindow.xaml.cs", "schemas/session-route-rules-v1.schema.json", "apps/control-model-check/Program.cs"]
---

# SPEC-0024：Expert per-App 路由預設 catalog

## 成功條件

Expert 使用者可以建立、更新、移除與清除最多 64 筆 per-App 路由預設。預設只保存 bounded
的 App ID／顯示名稱 matcher、lane、output group、優先級、啟用、補償增益與 gain owner；不把
PID、Windows session instance、Endpoint ID 或 plugin state 寫入使用者檔案。Easy 模式不顯示
這些欄位。

## 保存與排序

檔案為 `%LocalAppData%/Hibiki DSP/session-route-rules-v1.json`，schema version 固定為 1，
以同資料夾暫存檔後 replace 保存；載入先完整驗證候選，錯誤或版本不符時保留上一份 catalog。
規則 ID 為小寫英數與 `.`、`_`、`-`，最多 64 UTF-8 bytes；matcher 最多 128 bytes，lane／
output 最多 64 bytes；至少一個 matcher 與 lane／output 必須存在。優先級由大到小排序，同值
以 rule ID 穩定排序。容量、文字控制字元、enum、gain（-144..12 dB）與 finite 條件均 fail closed。

## 命令與套用

Catalog refresh 產生非零 `SessionCatalogSequence` 後，新增／更新／移除／清除才會建立
`SessionRouteRuleCommand` v1；命令沿用 SPEC-0023 的 480-byte wire 與 sequence。沒有同步清單
時可以保存本機預設，但 UI 必須明示「尚未套用」，不可把保存誤報成引擎已套用。送出後仍需
engine Ack；失敗保留本機預設並顯示未套用狀態，下一次可重試。

## UI 與安全

Expert 面板提供可讀的規則摘要、欄位提示、啟用與 gain owner 選擇；移除與清除都經由同一
control model。WinUI code-behind 只呼叫 ViewModel，不在 UI thread 直接接觸 COM、RT graph 或
Windows session identity。這個 catalog 是控制面便利層，不宣稱已完成實體 per-App reroute、
Chrome 單分頁捕捉或 vendor ASIO 攔截。

## 驗收

1. C# catalog 的 insert／update／排序／capacity／文字與 gain 驗證通過。
2. 原子保存、合法載入、schema／JSON／重複 rule 失敗回復上一份狀態通過。
3. ViewModel 使用目前 session catalog sequence 產生 480-byte Upsert／Remove／Clear 命令；
   sequence 為零時 fail closed。
4. Expert WinUI source gate 通過；無編譯物、PID、Endpoint ID 或個人路徑進入 repository。
5. 實體引擎 Ack、active-session delivery、裝置拔插與 Audio Service restart 仍依
   SPEC-0023 與目標 Windows 11 24H2 硬體驗收，不在本 spec 宣稱完成。
