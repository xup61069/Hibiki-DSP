# Hibiki DSP 多 AI 並行開發協定

本檔是多 AI 協作的穩定規則。工作由單一 orchestrator 指派，workers 不得自行認領 open Issue。
即時指派狀態放在 GitHub Issue assignee、lifecycle label 與 linked draft PR。產品、架構與完成狀態的權威順序仍以
`docs/START_HERE.md` 為準。

## 不變式

每個可獨立驗收的工作切片必須一對一擁有：

1. 一個 GitHub Issue；需要兩個 writer 就拆成兩個 child Issue。
2. 一個位於其他 AI working tree 之外的 clone 或 `git worktree`。
3. 一個 `<agent>/<issue>-<slug>` branch；不得直接在 `main` 開發。
4. Issue body 內的 `<!-- hibiki:handoff-v1 -->` handoff block（取代舊 handoff 檔案）。
5. 一個 draft PR；除非有明確 dependency，base 一律是 `main`。

同一時間不得有兩個 writer 共用 Issue、working tree、index、branch 或 handoff。若要把同一 branch
交給另一個 AI，原 owner 必須先完成可重建的 WIP commit、push、更新 handoff 並停止寫入；新 owner
確認遠端 HEAD 與乾淨工作樹後才接手。

## 開始工作：orchestrator 指派

1. Orchestrator 從 open Issue 中挑選下一個工作切片，確認目錄 lane（`driver/`、`tools/`、
   `apps/`、`docs/`、`extensions/`、`vst-host/`、`src/`）內沒有其他 active writer。
   每個 directory lane 同時最多一個 active writer；跨 lane 或 shared-path 衝突由 orchestrator
   指定 owner 與合併順序，不是先 commit 者贏。
2. Orchestrator 在 Issue body 加入 handoff block（branch、base_commit、owner、scope_globs、
   shared_paths、depends_on），指派 assignee 並加上 `claimed` label；同時建立 branch。
3. Worker 收到指派後，從 handoff block 的 base/target branch 最新遠端 HEAD 建立獨立
   clone/worktree 與 branch；tool-provided worktree isolation 是標準機制。例如：

   ```powershell
   git fetch origin
   git worktree add ..\Hibiki-DSP-<issue>-<slug> -b codex/<issue>-<slug> <base-or-target>
   ```

4. Worker 確認 branch、base_commit 與 handoff scope，執行指定的 required gates，開 draft PR，
   然後才修改產品檔案。Lifecycle label 使用 `claimed`（進行中）或 `in-review`（待審）；
   Issue 關閉即代表 done，不需要額外 label。沒有 orchestrator 指派的 handoff block 時，
   workers 只能做唯讀偵察，不得建立或認領 Issue。

### TBD pre-claim 草稿

Orchestrator 可以在正式指派前先建立 pre-claim 草稿：Issue body 已含 handoff block，但 `issue` 與
`branch` 欄位填寫 `TBD`（例如 branch 寫 `codex/TBD-<slug>`）。`handoff-check.ps1` 會偵測 TBD 欄位並
跳過該草稿的完整驗證（含 lifecycle label），不會阻擋其他 active claim 的檢查；這是 #439／PR
#442 導入的行為。TBD 草稿不是指派：它沒有 assignee、沒有 claimed label、也沒有 linked draft PR。
正式認領時必須補上實際 issue 號與 branch 名、加 assignee 與 `claimed` label、從最新 origin/main
建立獨立 worktree，之後才適用一般 claim 的全部規則。

### #124 occupancy fallback

若工具無法提供 worktree isolation，接手或推送非自己開始的 branch 前，
必須執行 `git worktree list` 確認 branch 未被本機任何 worktree 佔用，並在該 Issue 宣告意圖；
這是 fallback 檢查，不能取代隔離的 worktree。

worktree 的實際本機路徑屬環境資訊，不寫入 repository。禁止在別的 AI 正在使用的 worktree 執行
`checkout`、`switch`、branch rename、reset、clean 或 rebase。

## 同一台機器、多個 session

同一台機器上常有多個 AI session 以同一 Git 身分（甚至同一 GitHub 帳號）並行工作。
身分相同不等於有權進入彼此的工作區：

- worktree 隔離是絕對的；不得讀寫、build、commit 或 cleanup 別的 session 的 worktree，
  即使看起來「只是幫忙」。
- 接手前以 GitHub handoff block 為真值確認 owner，也要 `git ls-remote --heads` 與
  `git worktree list` 確認 branch 未被佔位——另一個 session 可能仍有未 push 的 edits；
  workers 不自行挑選未被指派的 open Issue。
- 回到先前中斷的 slice 時：先 fetch，以遠端 HEAD 與 Issue body handoff block 為唯一真值重新確認；
  本機未 push 的 edits 若已被遠端接手完成，接受遠端版本、獨立重跑全部 gates 驗證，
  不重寫歷史。
- 工作被另一 session 接手完成時，在 PR body 誠實記錄接手事件與後續驗證
  （先例：PR #24 / Issue #22）。
- 接手或推送任何不是自己開始的 branch 前，套用 #124 occupancy fallback；若仍有碰撞疑慮，
  先由 orchestrator 確認前一個 session 已停寫。
  先例：Issue #22 曾發生外部 session 對持有未 push 變更的 worktree 直接 commit，
  事後雖依遠端 HEAD 重驗收尾，但此類碰撞應從源頭避免。
- 收尾時只清理自己的 worktree 與本地 branch；遠端 branch 留給 integrator 決定。

## Scope 與 ownership

handoff block 的 `scope_globs` 是該 Issue 的獨占預告 write-set；`shared_paths` 是預期需要 integrator
或其他 lane 配合的檔案，不代表 worker 已取得寫入權。工作範圍擴張前，先更新 Issue body 並重新
檢查 overlap。

Directory lanes 是 orchestrator 分配工作的粗粒度邊界：`driver/`、`tools/`、`apps/`、
`docs/`、`extensions/`、`vst-host/`、`src/` 各自最多一個 active writer。下列 functional lanes
是常用細分，仍不得違反 directory-lane 上限：

| Lane | 主要範圍 |
| --- | --- |
| Orchestrator/Integrator | 全域狀態、工作指派、合併順序、CI/workflow、跨 lane 收斂 |
| Contract | Spec、schema、SDK/IPC、跨語言 codec |
| RT core | audio engine、graph、DSP 與 RT tests |
| Windows runtime | device/session/WASAPI worker、engine preview、live probe |
| Control/UI | C# control model、Desktop/WinUI 與其 checks |
| Driver boundary | `driver/`、driver-facing Apache SDK、license/IPC boundary |
| Plugin/source | `vst-host/`、`asio/`、`extensions/` 的獨立子系統 |

以下是全域整合快照，只有 active integrator 可直接修改：

- `docs/AI_HANDOFF.md`
- `docs/state/BASELINE.md`
- `docs/PROJECT_MAP.md`
- root `README.md`
- foundation integration Issue body

以下是高衝突 registry/聚合檔，必須在 Issue body handoff block 的 `shared_paths` 宣告並由 integrator 指定單一
writer：root/subsystem `CMakeLists.txt`、`tests/unit/contract_tests.cpp`、
`apps/control-model-check/Program.cs`、workflow、dependency lock、distribution identity 與 Spec index。

`docs/state/BASELINE.md` 的 volatile 計數（tracked paths／repository JSON）由
docs-check 即時量測（`git ls-files`），不再與 committed 數字比對；新增/刪除 tracked
檔案的切片不需要任何 counter-refresh chore。#197 之後，`build/baseline-counters.json`
已廢除。

永久 ID、public IPC/schema、DSP 順序、安全規則與 license boundary 同時只能有一個 active owner；
即使檔案不重疊，也不得由不同 AI 各自修改同一契約語意。

## Contract-first 與 dependency

跨 lane 功能先拆出最小 contract/schema/Spec Issue 並合併，再讓實作 Issue 從新 `main` 開始。
不能等待時可以使用 stacked draft PR，但 handoff block 必須以 `target_branch` 與 `depends_on` 明列依賴，
上游不得 force-push。上游變更後，下游先同步、重跑 baseline，再繼續工作。

一個 PR 只完成一個 Issue與一組 acceptance。feature AI 更新自己的 handoff block、目標 Spec、tests、匿名
evidence；integrator 在合併階段單次更新全域快照。未合併 branch 不得把局部 smoke 寫成 main 已完成
能力。

## 同步、驗證與交接

- 保持小而可建置的 commits，在安全節點 push；不得把唯一可用狀態留在聊天或未 push 工作樹。
- 發布後的 branch 不 force-push、不 rewrite history；需要同步 target branch 時採可審查的 merge，
  或由唯一 owner 在未被依賴前 rebase。
- push 前比較 `git diff --name-only <target>...HEAD` 與 `scope_globs`；超出 scope 先停下協調。
- PR 轉 ready 前執行 handoff block 指定 gates、把 lifecycle label 從 `claimed` 改為 `in-review`、更新
  handoff block 的驗證紀錄、限制與唯一 `Next safe action`。
- 交接時原 owner commit + push + 停寫，新 owner readback 遠端 HEAD、更新 owner 後再修改。
- 合併後 integrator 關閉 Issue 並記錄 merge SHA 與 evidence；lifecycle 由 issue state 表示，不再歸檔 handoff 檔案。

## 衝突處理

發生任一條件時立即停止重疊範圍的寫入：

- 兩個 active claim 的 Issue、branch 或 `scope_globs` 重複。
- 一個變更跨出已認領 lane，或需要另一個 writer 正在修改的 shared path。
- accepted Spec、ADR、source/tests/evidence 或全域快照互相矛盾。
- branch base、handoff base、PR head 或遠端 HEAD 與預期不一致。

把兩個 claim、paths、commits、觀察到的矛盾與最小決策寫到 Issue/PR；由 integrator 指定 owner、
切 child Issue或安排合併順序。不要把另一個 AI 未驗證的 working tree 直接複製、覆蓋或合併。

## Repository settings 建議

`main` 應啟用 branch rules：只能由 PR 合併、required CI 必須通過、禁止 force-push/deletion。
這是 repository 管理設定，不以本檔假裝已啟用；實際狀態以 GitHub ruleset 為準。
