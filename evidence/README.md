# Evidence manifests

只提交小型 JSON manifest，不提交大型 log、音訊、dump、driver package 或 binary。manifest 記錄
Issue、環境指紋、命令、結果、限制、工具版本與時間；換設備後，舊 evidence 仍只代表原先的
environment class。

## Provenance v2（新紀錄的唯一格式）

新紀錄使用 `evidence_format: 2` 與 `schemas/evidence-manifest-v2.schema.json`，不得再填
`source_commit` 或產品資料常用的 `schema_version`。`metadata.scope` 是方便人讀的 scope label，
不是密碼學 digest；真正的來源綁定在 `source_provenance`：

- `mode: change`：`paths` 必須精確等於這個切片所有非 `evidence/**` 的變更路徑。
- `mode: snapshot`：只允許 evidence-only 切片；`snapshot_commit` 是已存在、main 可達的來源
  commit，`paths` 必須精確等於該 commit 相對 first parent 的完整非-evidence 變更。
- `digest_algorithm` 固定為 `sha256-git-source-set-v1`。摘要依 ordinal-sorted path，綁定每個
  path 變更前／後的存在狀態、Git mode、byte count 與 raw Git blob SHA-256；rename 固定展開為
  delete + add。`evidence/**` 永遠排除，所以沒有 self-reference。

作者先把所有非-evidence 變更放入 Git index，再執行：

```powershell
pwsh -NoProfile -File tools/evidence-audit.ps1 -DescribeCurrentChange
```

將輸出的 `source_provenance` 放進新 manifest 後，執行完整
`pwsh -NoProfile -File tools/evidence-audit.ps1`。PR 與 main 的 required verify 也會跑完整 audit；
squash merge commit 由 first-parent history 推導，不寫回 JSON，因此合併後不需要 hash repair。

v2 紀錄是 append-only：不得覆寫、刪除或 rename。需要更正時新增一筆 v2，使用 `supersedes`
引用先前路徑。legacy 紀錄（沒有 `evidence_format`）保留現有 `source_commit` 稽核，但不得再新增或
修改，無需批次遷移。

來源 digest 能證明 manifest 綁定到哪批 Git bytes，不能證明 `commands` 真的執行，也不等於簽章、
GitHub-hosted attestation、實體裝置、WaveRT、HLK 或 Microsoft signing evidence；這些仍需各自的
可信 gate、review 與 release evidence。
