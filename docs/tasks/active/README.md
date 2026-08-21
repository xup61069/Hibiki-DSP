# Active task handoffs

每個 GitHub Issue 對應 `docs/tasks/active/<issue>.md`，由
[`TEMPLATE.md`](TEMPLATE.md) 建立。Issue assignee／linked draft PR 是 live claim；handoff 是 branch
內可重建的 durable state。Issue 0 是 foundation integration 特例，只由 integrator 更新；新 feature
一律建立自己的 Issue 與 v2 handoff。

v2 front matter 的 machine fields 至少包含 `schema_version`、`issue`、`branch`、`target_branch`、
`base_commit`、`status`、`role`、`owner`、`updated_at`、`scope_globs`、`shared_paths`、
`depends_on`、`next_safe_action` 與最多五個 `resume_commands`。不得記錄私人 worktree 路徑。

Markdown 必須包含 Objective、Acceptance、Completed、Known limitations、Last verification、
Remaining work、Next safe action 與 Resume commands。front matter 用於機器檢查；heading 補充人類可讀
細節，兩者不得矛盾。

一個 active Issue 同時只能有一個 writer。`scope_globs` 是獨占 write scope；`shared_paths` 只宣告
需要 integrator 協調的高衝突檔，不自動授權 worker 修改。scope 重疊或需要第二個 writer 時，先拆
child Issue或由 integrator 指定 owner/順序。完整規則見 `docs/ai/MULTI_AGENT.md`。

合併後移到 `docs/tasks/completed/<year>/`，保留 merge SHA 與 evidence；不要刪除歷史 handoff。
