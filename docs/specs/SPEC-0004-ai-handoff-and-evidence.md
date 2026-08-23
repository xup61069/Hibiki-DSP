---
id: SPEC-0004
status: accepted
owner: hibiki-maintainers
authority: repository-process
last_reviewed: 2026-08-23
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
- Issue body 的 `<!-- hibiki:handoff-v1 -->` handoff block：目前分支的 durable handoff。
- `evidence/<issue>/<digest>.json`：測試命令、環境指紋與有效範圍。
- GitHub Issue assignee 與 linked draft PR：多 AI 並行時的即時 claim、scope 與 dependency 狀態。

## 多 AI 並行協定

每個可獨立驗收的工作切片必須對應一個 Issue、一個獨立 clone/worktree、一個唯一 branch、Issue body
handoff block 與一個 draft PR。同一 Issue、working tree、branch 與 handoff block 同時只能有一個
writer；需要並行時拆 child Issue。Issue/PR 是 live claim，issue body 是 durable state。

Issue 建立時可先使用未指派的 TBD pre-claim：handoff block 的 `issue` 與 `branch` 含 `TBD`，且不得有
assignee、lifecycle label 或 linked PR。正式 claim 必須原子式補齊實際 Issue、branch、base、owner 與
write scope，指定唯一且與 `owner` 相同的 assignee，再加入 `claimed`。AI Issue form 不得在仍含 placeholder
的 Issue 上自動加 `claimed`。

handoff block 必須宣告 `branch`、`target_branch`、`base_commit`、`owner`、`scope_globs`、
`shared_paths`、`depends_on` 與 `resume_commands`。Lifecycle 由 labels 表達：`claimed` 表示進行中，
`in-review` 表示待審；Issue 關閉即代表 done。`scope_globs` 是獨占預告 write-set；scope 重疊、public contract 語意重疊或共享整合
檔重疊時，writer 必須停止，由 integration coordinator 指定 owner、拆分工作或安排合併順序。
不得用 force-push、覆蓋別人的 working tree 或聊天中的未提交內容解決衝突。

Foundation integration Issue 只保留 foundation integration。全域 `AI_HANDOFF`、`BASELINE`、`PROJECT_MAP`、
root README 與 foundation Issue body 由 integrator 單寫；feature AI 維護自己的 handoff block、目標 Spec、
tests 與 evidence。每個 active claim 各有一個 `Next safe action`，不同 Issue 可在不重疊 scope 內並行。完整操作協定
見 `docs/ai/MULTI_AGENT.md`。

## 續作協定

每次交接前必須記錄 owner、target branch、scope、dependencies、base commit、工作樹狀態、已完成
內容、驗證命令、剩餘風險與該 Issue 唯一的 `Next safe action`。原 writer 完成 WIP commit、push、
更新 handoff 並停止寫入後，新 AI 才能接手。新 AI 先讀 repository 與 Issue，再執行
`doctor.ps1 -CheckOnly`、`handoff-check.ps1 -Issue <id>`、`context-pack.ps1` 與必要測試；不得依賴
聊天記憶、舊機 registry 或私人路徑。

`tools/context-pack.ps1 -Issue <id>` 會讀 issue body handoff block 指定的 Spec/ADR，依各 Spec front matter
的 `source_globs` 只輸出相關 source，並附上該 Issue 的 evidence JSON；`-NoSource` 可先取得
最小文件包。Foundation integration Issue 是 foundation bootstrap，例外包含所有 tests 作為基準；後續 Issue
不得把全 repository source 默認塞入 context，若需要跨子系統檔案必須在 Spec 明確加入 glob。

`handoff-check.ps1` 也必須對所有帶 handoff block 的 open Issue 做 bounded
repository-relative glob intersection 檢查。完全相同、父子路徑與 wildcard 可相交的 claim
都必須 fail closed，錯誤訊息列出兩個 Issue 與兩個 scope；若 matcher 在固定狀態上限內無法
證明不相交，也必須視為衝突。`shared_paths` 是 integrator 協調宣告，不會把重疊的獨占
`scope_globs` 自動變成合法；不同 scope 的 claim 才能並行寫入。

驗證責任必須分離：PR 的 required `verify` workflow 從 `<agent>/<issue>-<slug>` branch 取出 Issue 編號，
只執行 `handoff-check.ps1 -Issue <id>`；`docs-check.ps1` 不得因 GitHub 上其他 Issue 的暫態狀態而失敗。
所有 open Issue 的 scope overlap、claim 完整性與 owner/assignee 一致性由獨立 `handoff-audit` workflow
在 Issue 事件與排程執行。未指派且無 lifecycle label 的 backlog 可沒有 handoff；已有 assignee 或
lifecycle label 的 open Issue 缺少 handoff 必須讓全域 audit fail closed，但不阻塞不相關 PR。
Handoff 資料只存在於 issue body block；`schemas/task-handoff-v1.schema.json` 已刪除，
`docs/ai/HANDOFF_SCHEMA.json` 保留為穩定入口，不再指向 JSON schema 檔。

## 文件閘門

Spec、schema、source 與 tests 改動必須同一變更更新。`docs-check.ps1` 檢查必要入口、
唯一 ID 與 adapter 存在；`source-policy.ps1` 阻擋 binary、秘密、私人裝置資料與 ISO
授權內容。`handoff-check.ps1` 必須能檢查指定 Issue 或枚舉所有 numeric active handoff，驗證
Issue/branch、v2 ownership/scope/dependency、Git ancestry、必要 headings 與最多五個 resume commands。
CI 失敗時不可宣稱該變更可交接。Handoff 檔案已由 issue body handoff block 取代；
`schemas/task-handoff-v2.schema.json` 已刪除。

## 語言與相容性

正式規格以繁體中文為真值；符號、schema 欄位與協定維持英文。英文 adapter 只能由
`AGENTS.md` 同步產生。Schema 以明確版本號演進，破壞性變更必須新增版本與 migration。
