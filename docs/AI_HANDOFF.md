# Hibiki DSP：AI 接手入口

這一頁是交接摘要，不取代 Spec、ADR、source 或 evidence。遇到衝突時，以
`docs/START_HERE.md` 所列權威順序處理，不要依聊天紀錄猜測。

## 先做這五件事

```powershell
git fetch --all --prune
git status --short --branch
gh issue view <issue>
pwsh -File tools/handoff-check.ps1 -Issue <issue>
pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource
```

工作樹不是乾淨狀態或 handoff check 失敗時，先在 Issue body 記錄事實；不要直接改 DSP、
driver、永久 ID 或 release 設定。需要 build／target evidence 的切片才另跑
`doctor.ps1 -CheckOnly` 與 `probe-environment.ps1`。

## 多 AI 並行入口

- 完整規則見 `docs/ai/MULTI_AGENT.md`：唯讀偵察不需要認領；寫入需要 maintainer／orchestrator
  明確指派、Issue assignee + lifecycle label、非 `main` branch 與 handoff scope。人類
  maintainer 的直接要求算指派，但仍須先 materialize Issue 並檢查 overlap。
- 有並行 writer、branch occupancy 或不確定狀態時必須使用獨立 worktree；單一 writer 時建議
  使用。首次可重建的 WIP/reviewable commit push 後立即開 draft PR，不需要空認領 commit。
- 修改前在 Issue body handoff block 宣告 `scope_globs`、`shared_paths` 與 `depends_on`。
  scope 重疊時先停止，由 integrator 指定 owner。
- feature AI 只更新自己的 handoff、目標 Spec、tests 與 evidence；全域摘要由 integrator 在
  整合時單次更新。每個 active claim 各自只有一個 `Next safe action`。

## 目前狀態與已完成能力

- 第二十一波整合增量：Engine Preview 的 canonical 控制管線改為單一擁有權 fail-closed，無法取得第一個 pipe instance 時新程序立即以非零結束，launcher/UI/smoke 不會再靜默連到舊引擎；PortCls adapter start path 新增 DriverEntry/AddDevice/StartDevice/endpoint 註冊/miniport Init/GetDescription 各階段的 DbgPrintEx 成敗診斷，供下一次隔離 VM 測試定位 CM_PROB_FAILED_START；仍不宣稱已安裝/載入/出聲（Issue #628 / PR #631、Issue #633 / PR #635）。
- 第二十波整合增量：AGENTS.md 改為三層規則索引（硬性限制／流程預設／core+conditional
  驗證門檻），driver 簽章歸 release 階段；README live probe 文件補齊
  live-wasapi-handoff-check 與 live-audio-session-check 的 opt-in 說明與 docs-check
  -SelfTest 範例（Issue #613 / PR #615）；隔離 VM WaveRT 載入測試記錄 TrustedPublisher
  恢復後 pnputil staging 成功、但 PnP start 仍以 CM_PROB_FAILED_START (0xC000000D)
  可重現失敗，下一步是診斷 PortCls adapter start path（Issue #462 / PR #614）。
- main 已合併的能力、限制與最新整合紀錄：見 [baseline](state/BASELINE.md)。
- 子系統地圖（driver/src/apps/vst-host/extensions/tools）：見 [PROJECT_MAP](PROJECT_MAP.md)。
- 各切片的測試命令、環境指紋與證據範圍：見 [evidence](../evidence/0000-foundation/)。

## 必讀順序

1. [AGENTS.md](../AGENTS.md)：三層規則索引（硬性限制／流程預設／驗證門檻）。
2. [START_HERE.md](START_HERE.md)：fresh clone 流程、權威順序與資料邊界。
3. 對應 Issue body 的 handoff block：目前分支的 durable handoff 真值。
4. 對應的 [Spec index](specs/INDEX.md) 與 [ADRs](adr/)。
5. [baseline](state/BASELINE.md) 與 [evidence](../evidence/0000-foundation/)。

## 不可自行做的事

- 不重生 `config/distribution-profile.yml` 的 endpoint GUID、driver hardware ID、ASIO CLSID、IPC namespace。
- 不把 `.local/`、bin/obj、PE/COFF、簽章檔、金鑰、真實 endpoint/session ID 或私人校正檔加入 Git。
- 不宣稱 vendor ASIO、WASAPI Exclusive、RAW、Atmos/DTS:X 或未經使用者手勢的 Chrome tab capture 已受 Hibiki 控制。
- 不把「控制命令已入列」或「預設已保存」寫成「已完成引擎／實體音訊套用」。
- driver 安裝／載入／HLK／Microsoft signing 屬 release 階段；沒有任何日常 probe 可以代替。

## 交接前最小完成條件

換 AI 或電腦前必須：只更新自己 Issue body handoff block 的 owner、base commit、驗證、限制與
下一步，建立 WIP commit、push 自己的 branch，並跑與改動範圍相符的 gate（見 AGENTS.md 第三層）。
所有 public contract 變更都要同步更新 Spec、tests 與 evidence。
