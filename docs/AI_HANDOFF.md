# Hibiki DSP：AI 接手入口

這一頁是交接摘要，不取代 Spec、ADR、source 或 evidence。遇到衝突時，以
`docs/START_HERE.md` 所列權威順序處理，不要依聊天紀錄猜測。

## 先做這五件事

```powershell
git fetch --all --prune
gh issue view <issue>
pwsh -File tools/doctor.ps1 -CheckOnly
pwsh -File tools/handoff-check.ps1 -Issue <issue>
pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource
```

工作樹不是乾淨狀態、handoff check 失敗，或 target toolchain 不符合時，先在 Issue body 的
handoff block 記錄事實；不要直接改 DSP、driver、永久 ID 或 release 設定。

## 多 AI 並行入口

- 完整規則見 `docs/ai/MULTI_AGENT.md`：寫入需要被指派（Issue assignee + lifecycle label），
  唯讀偵察不需要認領。首次可審閱的 commit push 後就開 draft PR；不需要空認領 commit。
- 修改前在 Issue body handoff block 宣告 `scope_globs`、`shared_paths` 與 `depends_on`。
  scope 重疊時先停止，由 integrator 指定 owner。
- feature AI 只更新自己的 handoff、目標 Spec、tests 與 evidence；全域摘要由 integrator 在
  整合時單次更新。每個 active claim 各自只有一個 `Next safe action`。

## 目前狀態與已完成能力

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
