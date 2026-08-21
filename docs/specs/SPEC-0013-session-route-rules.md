---
id: SPEC-0013
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/hub/include/hibiki/session_route_rules.hpp", "src/hub/src/session_route_rules.cpp", "schemas/session-route-rule-v1.schema.json", "tests/**"]
---

# SPEC-0013：每個 App 的自訂路由規則

## 成功條件

使用者可以為 Windows App／session metadata 建立固定容量的規則，指定 lane、輸出群組、
gain owner 與 makeup gain。規則套用後可交給既有 `AudioSessionRegistry` 與
`build_session_route_graph`，因此「遊戲小聲補增益」「Chrome VLOG 走耳機降噪 lane」等
設定不需要依賴 PID 或重啟整個引擎。

## 匹配與優先序

- `app_id` 是不分大小寫的完整匹配；`display_name_contains` 是不分大小寫的 ASCII 子字串匹配。
- 兩者同時存在時必須同時符合；至少一個匹配欄位必須非空。
- 優先序較高者勝出；相同優先序的多個匹配規則一律回報 `ambiguous`，不得依插入順序
  靜默選擇。
- `process_id` 只作 Windows session 的即時觀察，不進 rule schema、不作持久身份。
- 規則上限 64 筆；lane/output、rule ID、匹配文字與 gain 都有固定長度／數值上限。

## 資料流與安全

規則只在 control plane 評估；`WindowsAudioSessionWatcher` 可持有 non-owning rule store，
在 worker 的 `enumerate()` 階段把規則套到新的 session descriptor；非 Windows host 也可由
caller 先取得 descriptor，再呼叫 `SessionRouteRuleStoreV1::apply`。套用先建立 candidate，
成功後才替換 descriptor；配置失敗不得留下部分欄位更新。RT graph 只接收已驗證的 immutable
`GraphConfigV1`，不讀取規則文字。

規則 metadata 可持久化為 `session-route-rule-v1.schema.json`；不得把短暫 PID、完整 Windows
session instance ID、私人裝置路徑或使用者校正資料寫入公開 repository。

## 失敗／fallback

無匹配回傳 `no_match`，caller 保留原 descriptor；規則無效回傳 `invalid_argument`；容量滿
回傳 `capacity_exhausted`；同優先序衝突回傳 `ambiguous` 並保留原 descriptor。規則引擎不會
自動重啟 sink、改寫 Windows Master 或偷偷提升全域音量；App 補增益只透過 per-session
`HibikiInternal` makeup gain。

## 驗收

1. Chrome app ID 大小寫變化仍匹配；process ID 改變不影響匹配。
2. 高優先序 app rule 勝過低優先序 display-name rule；同優先序衝突 fail-closed。
3. 缺少匹配欄位、空 lane/output、超界 gain／文字與 65 筆規則都被拒絕。
4. 套用前後 descriptor 的 atomicity、既有 session route graph 與 Strict Direct gain-owner
   規則測試通過。
5. Windows session watcher 的 rule store 是 non-owning、只在 worker enumerate 套用；
   callback 不查詢規則、不配置、不修改 registry。

`tools/live-audio-session-check.ps1` 是 opt-in Windows probe：它在 worker-owned
`IAudioSessionManager2` enumeration 上只輸出 session 總數、active 總數與固定 identity
語意，不輸出 endpoint ID、session instance ID、PID 或顯示名稱。它驗證 default render
endpoint 的 session enumeration 可用，但不把「有 session」誤當成每個 App 已完成實際
路由或 DSP delivery；後者仍需 target endpoint／Lane integration evidence。
