# Active task handoffs

每個 GitHub Issue 對應 `docs/tasks/active/<issue>.md`。handoff 至少包含 schema_version、issue、
branch、base_commit、status、environment_fingerprint、specs、adrs、acceptance、completed、
remaining、last verification、limitations、next safe action 與 resume commands。

合併後移到 `docs/tasks/completed/<year>/`，保留 merge SHA 與 evidence；不要刪除歷史 handoff。
