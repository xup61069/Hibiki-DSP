---
id: SPEC-0004
status: accepted
owner: hibiki-maintainers
authority: repository-process
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0001]
source_globs: ["AGENTS.md", "docs/**", "evidence/**", "tools/**"]
---

# SPEC-0004：AI handoff、evidence 與文件新鮮度

## 單一事實來源

- `AGENTS.md`：短版工作規則與硬限制。
- `docs/START_HERE.md`：fresh clone 與換 AI 入口。
- `docs/specs/`：產品行為與資料契約。
- `docs/adr/`：不可回寫的架構決策。
- `docs/state/BASELINE.md`：main 已驗證能力與限制。
- `docs/tasks/active/<issue>.md`：目前分支的 handoff。
- `evidence/<issue>/<digest>.json`：測試命令、環境指紋與有效範圍。
- GitHub Issue assignee 與 linked draft PR：多 AI 並行時的即時 claim、scope 與 dependency 狀態。

## 多 AI 並行協定

每個可獨立驗收的工作切片必須對應一個 Issue、一個獨立 clone/worktree、一個唯一 branch、一份
active handoff 與一個 draft PR。同一 Issue、working tree、branch 與 handoff 同時只能有一個
writer；需要並行時拆 child Issue。Issue/PR 是 live claim，repository handoff 是 durable state。

handoff schema v2 必須宣告 `target_branch`、`owner`、`role`、`scope_globs`、`shared_paths` 與
`depends_on`。`scope_globs` 是獨占預告 write-set；scope 重疊、public contract 語意重疊或共享整合
檔重疊時，writer 必須停止，由 integration coordinator 指定 owner、拆分工作或安排合併順序。
不得用 force-push、覆蓋別人的 working tree 或聊天中的未提交內容解決衝突。

Issue 0 只保留 foundation integration。全域 `AI_HANDOFF`、`BASELINE`、`PROJECT_MAP`、root README
與 Issue 0 handoff 由 integrator 單寫；feature AI 維護自己的 handoff、目標 Spec、tests 與 evidence。
每個 active handoff 各有一個 `Next safe action`，不同 Issue 可在不重疊 scope 內並行。完整操作協定
見 `docs/ai/MULTI_AGENT.md`。

## 續作協定

每次交接前必須記錄 owner、target branch、scope、dependencies、base commit、工作樹狀態、已完成
內容、驗證命令、剩餘風險與該 Issue 唯一的 `Next safe action`。原 writer 完成 WIP commit、push、
更新 handoff 並停止寫入後，新 AI 才能接手。新 AI 先讀 repository 與 Issue，再執行
`doctor.ps1 -CheckOnly`、`handoff-check.ps1 -Issue <id>`、`context-pack.ps1` 與必要測試；不得依賴
聊天記憶、舊機 registry 或私人路徑。

`tools/context-pack.ps1 -Issue <id>` 會讀 handoff 指定的 Spec/ADR，依各 Spec front matter
的 `source_globs` 只輸出相關 source，並附上該 Issue 的 evidence JSON；`-NoSource` 可先取得
最小文件包。Issue 0 是 foundation bootstrap，例外包含所有 tests 作為基準；後續 Issue
不得把全 repository source 默認塞入 context，若需要跨子系統檔案必須在 Spec 明確加入 glob。

`handoff-check.ps1` 也必須對所有 numeric active handoff 的 `scope_globs` 做 bounded
repository-relative glob intersection 檢查。完全相同、父子路徑與 wildcard 可相交的 claim
都必須 fail closed，錯誤訊息列出兩個 Issue 與兩個 scope；若 matcher 在固定狀態上限內無法
證明不相交，也必須視為衝突。`shared_paths` 是 integrator 協調宣告，不會把重疊的獨占
`scope_globs` 自動變成合法；不同 scope 的 handoff 才能並行寫入。

## 文件閘門

Spec、schema、source 與 tests 改動必須同一變更更新。`docs-check.ps1` 檢查必要入口、
唯一 ID 與 adapter 存在；`source-policy.ps1` 阻擋 binary、秘密、私人裝置資料與 ISO
授權內容。`handoff-check.ps1` 必須能檢查指定 Issue 或枚舉所有 numeric active handoff，驗證
Issue/branch、v2 ownership/scope/dependency、Git ancestry、必要 headings 與最多五個 resume commands。
CI 失敗時不可宣稱該變更可交接。

`schemas/task-handoff-v1.schema.json` 只保留給 completed 歷史；新的或仍 active 的 handoff 必須遷移
到 `schemas/task-handoff-v2.schema.json`。`docs/ai/HANDOFF_SCHEMA.json` 只引用目前版本，不再維護第二份
重複欄位清單。

## 語言與相容性

正式規格以繁體中文為真值；符號、schema 欄位與協定維持英文。英文 adapter 只能由
`AGENTS.md` 同步產生。Schema 以明確版本號演進，破壞性變更必須新增版本與 migration。
