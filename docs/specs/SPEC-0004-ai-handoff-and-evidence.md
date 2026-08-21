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

## 續作協定

每次交接前必須記錄 base commit、工作樹狀態、已完成內容、驗證命令、剩餘風險與唯一
`Next safe action`。新 AI 先讀 repository 與 Issue，再執行 `doctor.ps1 -CheckOnly`、
`context-pack.ps1` 與必要測試；不得依賴聊天記憶、舊機 registry 或私人路徑。

`tools/context-pack.ps1 -Issue <id>` 會讀 handoff 指定的 Spec/ADR，依各 Spec front matter
的 `source_globs` 只輸出相關 source，並附上該 Issue 的 evidence JSON；`-NoSource` 可先取得
最小文件包。Issue 0 是 foundation bootstrap，例外包含所有 tests 作為基準；後續 Issue
不得把全 repository source 默認塞入 context，若需要跨子系統檔案必須在 Spec 明確加入 glob。

## 文件閘門

Spec、schema、source 與 tests 改動必須同一變更更新。`docs-check.ps1` 檢查必要入口、
唯一 ID 與 adapter 存在；`source-policy.ps1` 阻擋 binary、秘密、私人裝置資料與 ISO
授權內容。CI 失敗時不可宣稱該變更可交接。

## 語言與相容性

正式規格以繁體中文為真值；符號、schema 欄位與協定維持英文。英文 adapter 只能由
`AGENTS.md` 同步產生。Schema 以明確版本號演進，破壞性變更必須新增版本與 migration。
