---
id: SPEC-0004
status: accepted
owner: hibiki-maintainers
authority: repository-process
last_reviewed: 2026-08-24
review_after_days: 30
related_adrs: [ADR-0001]
source_globs: ["AGENTS.md", "README.md", "CONTRIBUTING.md", ".github/ISSUE_TEMPLATE/ai-task.yml", ".github/PULL_REQUEST_TEMPLATE.md", "docs/**", "evidence/**", "tools/**"]
---

# SPEC-0004：AI handoff、evidence 與文件新鮮度

## 單一事實來源

- `AGENTS.md`：短版三層規則索引（硬性限制、可演進預設、scope-triggered validation）。
- `docs/START_HERE.md`：fresh clone 與換 AI 入口。
- `docs/specs/`：產品行為與資料契約。
- `docs/adr/`：不可回寫的架構決策。
- `docs/state/BASELINE.md`：main 已驗證能力與限制。
- Issue body 的 `<!-- hibiki:handoff-v1 -->` handoff block：目前分支的 durable handoff。
- `evidence/<issue>/<digest>.json`：測試命令、環境指紋與有效範圍。
- GitHub Issue assignee、lifecycle label 與 handoff block：多 AI 並行時的即時 claim、scope 與
  dependency 狀態；linked draft PR 在第一個 WIP/reviewable commit 後加入。

## 多 AI 並行協定

唯讀偵察不需要 claim。每個寫入切片必須有 human maintainer／designated orchestrator 的明確
指派，並對應一個 Issue、一個唯一非 `main` branch、Issue body handoff block 與一個 draft PR。
maintainer 對目前 session 的直接要求算指派；active orchestrator 可在 overlap 檢查後建立／正式
claim Issue，worker 不得自行挑選 backlog。其他 writer 正在工作、branch 已被 worktree 佔用或
occupancy 不確定時必須使用獨立 clone/worktree；單一 writer 時仍建議隔離。draft PR 在首次可
重建的 WIP/reviewable commit push 後立即建立，不以空 claim commit 作前置。Issue + handoff 是
初始 live claim，PR 是後續 review surface。

Issue 建立時可先使用未指派的 TBD pre-claim：handoff block 的 `issue` 與 `branch` 含 `TBD`，且不得有
assignee、lifecycle label 或 linked PR。正式 claim 必須原子式補齊實際 Issue、branch、base、owner 與
write scope，指定唯一且與 `owner` 相同的 assignee，再加入 `claimed`。AI Issue form 不得在仍含 placeholder
的 Issue 上自動加 `claimed`。

## Claim admission serialization

`claim-pending` 是非授權的 admission 標記：它表示一個 writer session 已通過初步驗證但尚未取得寫入權。
`claim-pending` 加 assignee 或加 claimed/in-review/done 均 fail closed；TBD handoff 加任何 lifecycle
label（含 `claim-pending`）也 fail closed。

正式 claim admission 由 `.github/workflows/claim-admission.yml` 提供全 repo 單一 concurrency 的
序列化 workflow：請求 session 提供 UUID、fresh-read 全部 open Issues、驗證 normalized exact title、
case-insensitive branch、scope overlap（exact/父子/wildcard/case variant），再以 Git refs API 原子
保留工作 branch；完成全域 audit/readback 後才把 `claim-pending` 轉成 `claimed` 並綁定 session identity。
失敗/cancel/readback mismatch 不得留下有效 `claimed` 狀態。第一個 WIP push 必須匹配該 session；
遠端 branch lease 不符即停止。API 列表必須完整分頁或在明確 cap fail closed。

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
更新 handoff 並停止寫入後，新 AI 才能接手。新 AI 只先讀 `AGENTS.md`、`docs/START_HERE.md` 與
active Issue handoff，再執行 `handoff-check.ps1 -Issue <id>`、`context-pack.ps1 -NoSource` 與 scope
所需測試；只有需要 build／target environment evidence 時才執行 `doctor.ps1 -CheckOnly`。不得依賴
聊天記憶、舊機 registry 或私人路徑，也不得把全域歷史當作每個 task 的固定前置。

## 上下文預算與分層載入

`tools/context-pack.ps1 -Issue <id>` 是啟動流程唯一一次輸出 active Issue body 的位置；它預設只輸出
該 body、handoff 指定的 Spec／ADR 與該
Issue 的 evidence JSON；它不再重複輸出啟動時已讀的 `AGENTS.md`／`START_HERE.md`，也不預載
`MULTI_AGENT`、`AI_HANDOFF`、`PROJECT_MAP` 或 `BASELINE`。需要 source 時，工具依 Spec front matter
的 `source_globs` 選取；`-NoSource` 是新視窗與交接的預設入口。

pack 必須先在記憶體完成組裝，將換行與 summary 一併序列化，再檢查完整輸出後一次寫出；預設
`-MaxCharacters 48000` 與 `-MaxEstimatedTokens 12000`。後者用離線、模型無關的保守 heuristic
估算一般混合中文與 code 的 token 壓力，不冒充精確 tokenizer 或數學上界。任一超限都必須在任何
pack 內容輸出前 fail closed。
操作者應縮小 Issue 引用或改成本機按需查閱，不得只為繞過限制無界提高預算。只有明確的
repository-wide 稽核可以使用 `-IncludeRepositoryState`；使用時必須同時明確給定
`-MaxCharacters` 與 `-MaxEstimatedTokens`，讓擴張可見且可重現。Foundation integration Issue 也不取消此上限
或自動載入全域歷史；其 source 模式只保留既有 tests bootstrap 例外。後續 Issue 不得把全 repository
source 默認塞入 context，跨子系統檔案必須在 Spec 明確加入 glob。

每個里程碑完成時，Issue handoff 要保存已完成動作、有效假設、識別碼／commit、驗證結果、未解
阻擋與唯一下一步。同一視窗發生第二次上下文壓縮，或工作切換到另一個里程碑時，writer 必須先
完成 durable checkpoint，再由新視窗以核心入口、active Issue 與最小 pack 接續，不重播完整聊天。

`docs-check.ps1` 必須對固定 AI 入口執行字元預算：`AGENTS.md` 6,000、`START_HERE.md` 7,000、
四個生成 adapter 各 1,000、`AI_HANDOFF.md` 6,000、`PROJECT_MAP.md` 12,000、`MULTI_AGENT.md`
9,000、`CODEX_GOALS.md` 6,000、root `README.md` 20,000。超限代表入口混入了應按需查詢的細節，
必須 fail closed；不得為新增 changelog 直接調高上限。README／PROJECT_MAP 不得重新要求新 AI
預載 `AI_HANDOFF.md`，四個自動 adapter 不得直接指向全域快照；啟動文件不得先全文輸出 Issue body
再由 pack 重播，也不得使用未經 filter 的 `git worktree list` 或未明確限制筆數的 Git／GitHub 歷史
清單。整合歷史由 BASELINE、evidence、merged PR 與 Git history 保存，
`AI_HANDOFF.md` 只保留 live routing 與安全邊界。

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

## Evidence provenance v2

沒有 `evidence_format` 的既有 manifest 是 legacy evidence，無論它的產品資料
`schema_version` 是 1、2 或缺省，都維持 `source_commit` 相容稽核；legacy 檔案不得再新增、覆寫、
刪除或 rename，也不做 repository-wide migration。新紀錄唯一格式是
`schemas/evidence-manifest-v2.schema.json` 定義的 `evidence_format: 2`，根層不得同時含
`source_commit` 或 `schema_version`。

v2 manifest 是 append-only assertion。`metadata.scope` 只是人類可讀 label，不是 digest；
`source_provenance` 才是來源綁定：

- `change` 綁定目前候選相對動態 merge base 的完整非 `evidence/**` change set。
- `snapshot` 只允許 evidence-only 候選，並綁定一個已存在、可由 prior main 到達的
  `snapshot_commit`；paths 是該 commit 相對 first parent 的完整非-evidence change set。
- paths 必須是 ordinal-sorted、case-insensitive unique、ASCII repo-relative exact paths；拒絕
  evidence、`.git`、glob、反斜線、絕對／traversal、symlink、submodule 與無法安全解析的 path。
- `sha256-git-source-set-v1` 對每個 path 變更前後的存在狀態、regular-blob mode、raw Git blob
  byte count 與 SHA-256 作 domain-separated canonical digest。rename 使用 `--no-renames`，因此穩定
  表示為 delete + add；不得從 checkout 後可能經 CRLF/smudge 改寫的 bytes 計算。

canonical byte stream 使用 UTF-8、無 BOM 與 NUL 分隔：先寫
`hibiki-evidence-source-set-v1\0`；每個 ordinal-sorted path 寫 `P\0<path>\0`，接著依序寫 before、
after state。absent state 是 `0\0`；present state 是
`1\0<mode>\0<size-invariant-decimal>\0<sha256-raw-blob>\0`。最外層再取 SHA-256 lowercase hex。
固定 self-test vector 鎖定此 encoding，修改演算法必須改名／升版，不能靜默重算既有紀錄。

pre-merge audit 從 main/head 推導 merge base，要求所有非-evidence source 已 staged、v2 record 是新增，
並重算完整 paths/digest。squash merge 後，audit 從 main first-parent history 推導 record commit 與其
first parent，要求目前 record blob 等於 introduction blob，再重算同一 source set；manifest 不保存
未知的 future merge SHA，因此不得產生 post-merge hash repair。更正另加新的 v2 record，以
`supersedes` 指向 prior-main 已存在的紀錄，不回寫原 assertion。

required `verify` 必須跑完整 `evidence-audit.ps1`，不能只跑 `-SelfTest`。audit 不得執行 manifest
內的 `commands`；內容 digest 只證明 assertion 綁到哪些 Git bytes，不證明命令確實執行，也不取代
trusted CI、review、signed attestation、live-device 或 release evidence。workflow／audit 本身仍可在
PR 中被修改，是既有 repository trust boundary；需要對抗惡意 writer 時，另由 default-branch
required workflow、GitHub App 或簽章 attestation 建立不可由同一 PR 關閉的 trust root。

## 文件與驗證閘門

public contract 變更必須在同一 slice 更新相關 Spec/schema、source、tests 與 evidence；純文件
措辭修正不需要假造 product build evidence。每個寫入 slice 的 always-run checks 是 scoped
`handoff-check.ps1 -Issue <id>`、`docs-check.ps1`、`source-policy.ps1` 與 `git diff --check`。
`doctor`、`verify`、`source-only-ci-check`、subsystem builds 與 live probes 只在 scope 或 acceptance
觸發時執行；live probes 永遠 opt-in。

`docs-check.ps1` 檢查必要入口、
唯一 ID 與 adapter 存在；`source-policy.ps1` 阻擋 binary、秘密、私人裝置資料與 ISO
授權內容。`handoff-check.ps1` 必須能檢查指定 Issue 或枚舉所有 numeric active handoff，驗證
Issue/branch、v2 ownership/scope/dependency、Git ancestry、必要 headings 與最多五個 resume commands。
CI 失敗時不可宣稱該變更可交接。Handoff 檔案已由 issue body handoff block 取代；
`schemas/task-handoff-v2.schema.json` 已刪除。

## 對人回報與 Codex Goal 契約

AI 對 maintainer 的進度與完成回報以產品結果為主：先用白話說明正在建立或修正的能力、使用者
影響、可重現的驗證與剩餘缺口。branch、commit、push、PR、merge 與 CI 是內部協作或 audit
evidence；除非它們改變風險、阻擋或結論可信度，或 maintainer 明確詢問，否則只在末尾簡短列出，
不得作為標題或主要敘事。這個呈現規則不降低 handoff、scope、validation 或 evidence 要求。

新 Codex 視窗使用一個可驗證成果作為單一 Goal。長任務也必須包含邊界、驗證、停止條件與需要
人類決定時的 pause 條件，不得用「永遠處理所有 backlog」讓 agent 自行無限擴張 scope。建議的
五種視窗角色與可貼用啟動詞見 `docs/ai/CODEX_GOALS.md`；並行視窗仍受本 Spec 的獨占 write scope
與 handoff 規則約束。

## 語言與相容性

正式規格以繁體中文為真值；符號、schema 欄位與協定維持英文。英文 adapter 只能由
`AGENTS.md` 同步產生。Schema 以明確版本號演進，破壞性變更必須新增版本與 migration。
