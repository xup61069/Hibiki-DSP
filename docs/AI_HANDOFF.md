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

- 第二十八波整合增量：瀏覽器單分頁捕捉的 offscreen start／stop 回應保持 async message channel 開啟，成功啟動會確實送回 popup，失敗仍帶回錯誤且 payload 不變（Issue #681 / PR #692）；Compatibility Preview WinUICompat target 恢復可編譯並通過啟動 smoke，保留 Core MRT 資源初始化修復先前 0xC000027B 啟動失敗（Issue #687 / PR #694），其暫時 dead-code TextBox seam 已清除並沿用既有雙向 ViewModel binding（Issue #697 / PR #699）。Extension 項目為 extension source／policy gate evidence；WinUICompat 項目為本機 source/build/launch smoke evidence，不是正式 XAML／accessibility 或音訊／driver 能力。
- 第二十七波整合增量：TruePeakLimiterV1 在 graph commit 時重置回 unity gain，前一個 graph 累積的恢復衰減不會延續到新 graph 的安靜段落，新 graph 超限峰值仍立即衰減（Issue #678 / PR #678）；瀏覽器單分頁捕捉的 start/stop 回應改為反映真實成敗，成功啟動不再被誤報為失敗，失敗也會帶回實際錯誤，policy gate 同步涵蓋回應邊界（Issue #681 / PR #684）。DSP 項目為 source／contract test／SPEC-0002 evidence；extension 項目為 source／policy gate evidence。不宣稱實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
- 第二十六波整合增量：WinUI Expert shell 開放本機 VST3 時間軸編輯器（選取、草稿開始／提交／捨棄、復原／重做、事件增刪與數值編輯），Compatibility Preview 同步補上時間軸選取 seam（Issue #667 / PR #669、Issue #673 / PR #677）；瀏覽器單分頁捕捉新增使用者控制的 Stop、開啟 popup 會反映真實捕捉狀態、啟動失敗如實回報錯誤，不再假裝成功（Issue #671 / PR #676）。UI 項目為 control-model／shell source 與 gate 證據；extension 項目含 SPEC-0009 更新與 policy check。不宣稱實體音訊或 driver 能力。
- 第二十五波整合增量：IPC 控制管線在兩次 client 連線之間的空檔收到 stop() 時，會重複取消當下註冊的 server handle I/O 直到 worker 觀察到停止，Engine Preview 關閉不再固定等待完整 idle timeout，framing 與 ownership 語意不變（Issue #655 / PR #661）；TruePeakLimiterV1 恢復速率改為以經過音訊時間（約 +6 dB 每毫秒）計算並跟隨引擎取樣率，限制器放開的速度不再隨回呼區塊大小改變（Issue #659 / PR #666）。兩者皆為 user-space source 與 contract test 證據；不宣稱實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
- 第二十四波整合增量：driver 端 `operator new` 改用現代 ExAllocatePool3 分配 API，並把 GetHardwareLatency 的每 buffer 延遲估計填入 CodecDelay，讓 PortCls 收到有意義的硬體延遲值（Issue #652 / PR #660）；主靜音控制節點改用標準 KSAUDFNAME_MASTER_MUTE 命名，音效工具或系統屬性頁不再看到匿名靜音節點（Issue #662 / PR #663）。兩者皆為本機 WDK 建置與 Inf2Cat source/build 證據；不宣稱 guest 安裝、載入、PnP start、實體音訊、HLK 或 Microsoft signing。
- 第二十三波整合增量：TruePeakLimiterV1 恢復期改為每區塊最多放寬約 +6 dB 的有界增益回升，需要壓低時仍然立即反應，限制器本身不再產生突兀的 click/pumping（Issue #647 / PR #648）。屬 user-space DSP 契約證據；不宣稱 ITU/BS.1770 認證、實體端點、driver 或 HLK/Microsoft signing。
- 第二十二波整合增量：noise gate 修正開／關方向，開啟速度由 attack 控制、關閉速度由 release 控制，說話開頭與尾音不再被吃掉（Issue #636 / PR #640）；共用 handoff 稽核新增同 branch 拒絕的離線自檢（Issue #643 / PR #644）；Engine Preview 同步建立 canonical pipe 後立即 stop 時可馬上取消 pending connect，不再等完整 idle timeout（Issue #637 / PR #645）；driver 端每個 endpoint 改為成對註冊 PortCls Topology 與 WaveRT filter 並接上 bridge 連線，本機 WDK 建置與 Inf2Cat 通過，待隔離 VM 重測確認先前的啟動失敗是否解決（Issue #462 / PR #638）。DSP 與 IPC 項目為 user-space/source evidence；不宣稱實體音訊、driver 安裝/載入/HLK/Microsoft signing。
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
