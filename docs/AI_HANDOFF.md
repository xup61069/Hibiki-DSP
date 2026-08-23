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

- 第三十七波整合增量：自訂 Scene catalog 的離線同步在佇列滿溢時改為誠實處理容量損失——捨棄最舊操作並累計顯示已捨棄數量，後續離線操作與重播完成訊息不得覆蓋該警告，control-model-check 新增 65 筆有序操作的回歸案例（Issue #840 / PR #844）。SPEC-0014 補上同一行為的權威描述：64 筆上限、捨棄最舊、保留容量損失警告、重播全 Ack 才回報「引擎已同步」、部分失敗保留剩餘並顯示降級狀態（Issue #838 / PR #847）。driver WaveRT bridge pin 補上與 topology 半邊一致的 analog data range，修復 PortCls 無法註冊 physical connection 造成 STATUS_RANGE_NOT_FOUND 啟動失敗的根因；PnP 診斷工具同時支援依 hardware ID 解析目標裝置（Issue #462 / PR #845）。隔離 Hyper-V guest 以官方 Win11 25H2 媒體完成同意重測：test-signed 套件通過簽章驗證與 pnputil staging、devcon 建立裝置，穩定重新啟動後 MEDIA 裝置 Status OK、ProblemCode 0、HibikiVirtualAudio 服務 Running，匿名證據已入庫（Issue #462 / PR #850）；仍不宣稱實體音訊播放、WaveRT streaming 行為、HLK 或 Microsoft signing。證據來源稽核把 scene-offline-sync 短雜湊換成 main 可達的 squash merge commit（Issue #846 / PR #848），另將 18 份 evidence JSON 的孤兒 source_commit 全部修正為經 git merge-base 驗證祖先關係的合併 commit（Issue #849 / PR #854）。上述分別為 source+contract test/control-model evidence、documentation-only evidence、driver source/build/signability evidence、anonymous isolated-guest PnP-start evidence 與 evidence-metadata correction；不宣稱瀏覽器 runtime automation、實體音訊量測、driver 實體播放／HLK／Microsoft signing。
- 第三十六波整合增量：瀏覽器單分頁捕捉在 bridge 啟動晚或暫時掉線時，offscreen 以有界指數退避（最多 10 次、上限 15 秒）重試連線；bridge 恢復後新封包即自動送入，不需重新 Start capture，手動 Stop 或串流自然結束仍完整 teardown 並取消重試，extension-check 追加對應 source-boundary 斷言（Issue #826 / PR #834）。自訂 Scene catalog 加入離線操作的有界重播：未連線時新增／移除仍立即更新本機 mirror 與檔案，同時記入最多 64 筆的佇列（超過捨棄最舊並顯示容量壓力），重新連線成功後依原順序補送 Upsert/Remove 到引擎，全部 Ack 才回報「引擎已同步」，中途失敗保留剩餘並誠實顯示降級狀態（Issue #823 / PR #832）。VST3 host README 能力圖刷新：明確標示 bounded 參數自動化、timeline 編輯交易、undo/redo history 及 C# Compatibility Preview 編輯面已在 source baseline scope，並補上 LatencyGraphCommitV1/LaneLatencyBankV1 graph-side commit 契約摘要，仍不宣稱 third-party certification、crash-dump redaction 或實體端到端延遲驗收（Issue #836 / PR #837）。上述分別為 extension source/gate evidence、source+contract test/control-model evidence 與 documentation-only evidence；不宣稱瀏覽器 runtime automation、實體音訊量測、driver guest 安裝／載入／HLK 或 Microsoft signing。
- 第三十五波整合增量：DesktopCompat 與 WinUI Compatibility Preview 開放 Expert 的每-App 路由規則工作流程，涵蓋有界規則清單、ID／App ID／顯示名稱、Lane、output group、priority、makeup gain、enabled 與 gain owner，並提供新增／更新、逐筆移除與全部清除；兩者重用既有 ViewModel 驗證、保存回復、狀態文字與 IPC seam，不改 engine contract（Issue #804 / PR #808）。SPEC-0003 補上 BasicNoiseSuppressorV1 與 VirtualMicDspV1 共用的 upper-only 2 dB reopen hysteresis 契約：threshold 關閉、回升 +2 dB 才開啟，中間維持原狀態以避免臨界 chatter（Issue #810 / PR #812）。自訂 Scene catalog 新增 control-plane 同步：SceneCatalogCommandV1（IPC type 20）使用固定 3260-byte wire format 送出 Upsert/Remove/Clear，引擎 worker 擁有 mutable catalog，重建完整 definition 並通過既有 Scene/Graph/ISO 驗證後原子替換；C# 控制模型在連線時於新增或刪除本機卡片後推送同步，契約測試涵蓋往返、套用、移除與破損 payload fail-closed（Issue #768 / PR #814）。Wave34 快照 evidence 也修正為實際 gate 時間與完整命令紀錄，並明確記錄 post-merge correction（Issue #809 / PR #811）。上述分別是 compatibility preview source/build/launch、documentation/spec、source+contract test/control-model/engine smoke 或 documentation/evidence correction；不宣稱 runtime screen-reader audit、實體音訊量測、driver guest 安裝／載入／HLK 或 Microsoft signing。
- 第三十四波整合增量：正式 WinUI 的 VST3 時間軸編輯器新增可達的「清除歷史」與「保存基準」操作；清除只重置 undo/redo、保留資料列與開啟中的草稿，保存會把目前內容設為新基準，未選時間軸或草稿開啟時維持拒絕（Issue #786 / PR #791、Issue #794 / PR #798）。DesktopCompat 與 WinUI Compatibility Preview 開放自訂 Scene 的名稱／描述編輯和可達的新增／移除控制，重用既有驗證、32 筆上限、儲存失敗回復與狀態文字（Issue #788 / PR #796）。Virtual Mic noise gate 加入 2 dB reopening hysteresis，等化臨界附近的房噪或呼吸聲不再造成快速 gain cycling（Issue #787 / PR #795）；ISO 226 補償公式加入頻段 phon 有效性邊界，任一現值／參考值無效時整批 fail-closed（Issue #789 / PR #797）。installer-check 的交易式 uninstall 自檢成長到 13 案例，涵蓋 traversal 拒絕、選擇性移除、冪等、locked-file rollback 與實際 process temp root 的 backup residue；engine-preview-soak 報表形狀的離線自檢成長到 17 案例（Issue #785 / PR #793、Issue #800 / PR #802、Issue #799 / PR #801）。上述分別是 source、contract-model、source-gate、compatibility preview build/launch 或離線 self-test evidence；不宣稱 runtime screen-reader audit、實體音訊量測、真實 installer 執行、driver guest 安裝／載入／HLK 或 Microsoft signing。
- 第三十三波整合增量：官方 bootstrapper 補上經 manifest 與 SHA-256 驗證的交易式 user-space payload staging，以及只移除 manifest 所列檔案、保留使用者資料並可回復的 uninstall 路徑；staging 的成功、路徑逃逸、hash mismatch 與 rollback 已有 9 個離線功能案例，uninstall 目前仍僅有 source/boundary evidence（Issue #745 / PR #759、PR #770；Issue #766 / PR #777）。driver 工具可自動尋找 Inf2Cat、以 `/WX` 拒絕 compiler/linker warning 並避免重複物件，PortCls `MaxObjects` 也從錯誤的 4 個 pair 數量修正為能容納 8 個 Topology/WaveRT filter，另新增只讀匿名 PnP/SetupAPI 診斷包供隔離 VM 重測使用（Issue #764 / PR #769、Issue #774 / PR #776、Issue #462 / PR #782、Issue #781 / PR #783）；這些都不是 driver 已在 guest 成功安裝或 PnP start、HLK 或 Microsoft signing evidence，#462 因 VM 重測未完成而保持開啟。popup 回到可見狀態會重新查詢真實捕捉狀態且保留既有錯誤（Issue #762 / PR #773）；兩種 preview 會讀出完整 route-health 摘要（Issue #765 / PR #771）；正式 WinUI 可安全移除本機自訂 Scene 卡、也可手動重新掃描實體輸出裝置，失敗時各自回復或保留先前狀態（Issue #767 / PR #775、Issue #779 / PR #784）。Basic noise gate 加入 2 dB reopening hysteresis，臨界附近訊號不再快速開關（Issue #778 / PR #780）。UI/extension 項目仍是 source、contract-model、source-gate 或非目標 preview evidence；不宣稱正式 runtime accessibility、瀏覽器自動化或實體音訊。
- 第三十二波整合增量：SPEC-0009 補上 popup 無障礙政策的權威文件條目，記錄 PR #744 引入並由 PR #750 擴充的 aria-label／aria-labelledby 強制檢查與 fail-closed 行為（Issue #753 / PR #755）；winui-shell-check 新增兩個回歸防護：DesktopCompat Preview 的互動控制項必須在 Program.cs 宣告非空 AccessibleName，正式 shell MainWindow.xaml 必須保留 IR WAV 載入按鈕及其 AutomationProperties.Name 和 OnPrepareIrClick handler（Issue #752 / PR #757）；正式 shell route-health 卡片將控制模型的完整無障礙摘要投射給輔助技術，取代僅視覺片段的讀法，Expert 狀態變更改用 polite live region 公告（Issue #756 / PR #758）。分別為文件、UI source-gate 與 source-only accessibility projection evidence；不宣稱正式 XAML/accessibility runtime audit、螢幕閱讀器 runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
- 第三十一波整合增量：Desktop Compatibility Preview 的全部互動控制項補上非空無障礙名稱，螢幕閱讀器不再遇到未命名按鈕、下拉選單、滑桿或輸入欄（Issue #747 / PR #749）；extension popup gate 追加 aria-labelledby 目標解析檢查，引用 ID 必須存在且含可見文字，壞引用即使同時有 aria-label 也會 fail-closed（Issue #748 / PR #750）。分別為 compat preview source/build/launch evidence 與 extension source/policy evidence；不宣稱正式 XAML/accessibility runtime audit、螢幕閱讀器 runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
- 第三十波整合增量：瀏覽器單分頁捕捉的 Start／Stop 失敗訊息會保留到下一次使用者操作，不會被立即的狀態重繪蓋成 Idle（Issue #724 / PR #727）；Compatibility Preview 開放與正式 shell 相同的「刪除目前時間軸」動作並帶無障礙名稱（Issue #723 / PR #729）；offscreen natural-end release gate 追加「handler 定義被移除時 fail-closed」自檢（Issue #733 / PR #735）；winui-shell-check 現在掃描 Compatibility Preview C# 控制建構的 AutomationProperties.Name，缺漏或空值會讓檢查失敗（Issue #732 / PR #734）。分別為 extension source/policy、compat preview source/build/launch 與 UI source-gate evidence；不宣稱正式 XAML/accessibility runtime audit、瀏覽器 runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
- 第二十九波整合增量：Compatibility Preview 的 WinUICompat 紀錄修正為「無 Core MRT 資源工具下以 fail-soft 啟動」——主題資源走 TryGetValue 回退，預覽無樣式但不崩潰，不再宣稱 Core MRT 修復（Issue #703 / PR #709、Issue #720 / PR #721）；popup Start／Stop 攔截訊息通道錯誤，顯示實際錯誤並重新查詢真實捕捉狀態後才恢復控制項；失敗文字在背景狀態刷新時保留到下一次使用者操作（Issue #702 / PR #704、Issue #724 / PR #727），extension gate 同步強制 popup 檢查 response.ok 與錯誤回報（Issue #715 / PR #719）；offscreen 在來源串流自然結束時釋放捕捉 graph、回報 service worker 並自動關閉 document（Issue #706 / PR #707、Issue #714 / PR #716），SPEC-0009 與 README 補上失敗回應、stream-ended 邊界與 bridge 狀態說明（Issue #710 / PR #711、Issue #712 / PR #718）；compat preview smoke 改從乾淨輸出目錄執行，避免殘留 binary 假通過（Issue #713 / PR #717）。皆為 extension source/policy、compat preview source/build/launch 與文件 evidence；不宣稱實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
- 第二十八波整合增量：瀏覽器單分頁捕捉的 offscreen start／stop 回應保持 async channel 開啟，成功啟動確實送回 popup（Issue #681 / PR #692）；Compatibility Preview WinUICompat 恢復編譯與啟動 smoke，暫時 TextBox seam 隨後清除（Issue #687 / PR #694、Issue #697 / PR #699）；每個 output group 有獨立 true-peak limiter 狀態，跨群尖峰不再壓低安靜輸出且 graph commit 重置所有狀態（Issue #683 / PR #695）；Engine Preview 新增有界 opt-in soak harness 與離線 SelfTest，預設三循環驗證 Hello/Ack、Main volume 往返、status convergence 和乾淨停止（Issue #672 / PR #685）。Extension／UI／DSP／soak 分別為 extension source/policy、compat preview source/build/launch、user-space contract test、local process evidence；不宣稱實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
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
