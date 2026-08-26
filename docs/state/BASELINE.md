# Hibiki DSP baseline

## 已完成（有 commit 與 evidence）

第四十五波（wave 45）整合增量已合併六項能力與修正：(1) DesktopCompat 備用 UI 的主輸出與 per-App 音量滑桿旁新增即時 dB 數值標籤，拖曳時可看到精確數值而非只有滑桿位置（Issue #1640 / PR #1642）。(2) 瀏覽器擴充功能修正 offscreen reportState 中 retryInSec 變數未定義的錯誤，讓 tab capture 啟動不再因此中斷（Issue #1634 / PR #1639）。(3) 正式 WinUI shell 修復 NavigationView 內容列被限制在 48px 標題列高度的排版問題，現代導航、快速入門、輸出選擇器與連接按鈕恢復在主要內容區域正確顯示（Issue #1646 / PR #1648）。(4) DesktopCompat 預覽殼層新增 UI 狀態記憶：實體輸出裝置與場景選擇即時保存到 %LOCALAPPDATA%，下次啟動自動回復；狀態檔損壞或過大時 fail-closed 忽略不影響啟動（Issue #1638 / PR #1641）。(5) 校準與等響度相關原始碼註解及 Spec 描述移除殘留的授權標準名稱，改以 equal-loudness 措辭描述功能邊界，屬文件與註解清理不改行為（Issue #1649 / PR #1653）。(6) DesktopCompat 的 App catalog 狀態文字新增 session catalog sequence number 顯示，讓使用者看出 catalog 已刷新且 handle 已更新（Issue #1631 / PR #1633）。以上皆屬 user-space source/UI/control/docs evidence；不宣稱 driver/WaveRT 或實體音訊。
第四十四波（wave 44）整合增量已合併七項能力與修正：(1) Engine Preview 的 WAV file source 解碼幀上限從 realtime IR tap 上限（4096 taps）分離為獨立的 source frame bound，讓超過 4096 幀的音源 WAV 檔正常解碼播放而不被誤拒；IR 載入路徑仍保留 4096-tap fail-closed 限制（Issue #1594 / PR #1604）。(2) 引導式校準工作流新增三種內建目標曲線（flat、Harman in-ear、Harman over-ear）與 log-frequency 插值取樣 API，C# 鏡像提供 TrySampleTargetCurve、BuildTargetedResponse 和最多 8 聲道的批次編譯；任何聲道錯誤即整批 fail-closed（Issue #1564 / PR #1607）。(3) DesktopCompat 備用 UI 新增實體輸出裝置選擇器、切換按鈕與重新掃描按鈕：裝置切換使用 prepare→crossfade→rollback 流程，失敗時回退原裝置；移除過時的「尚未支援」免責聲明，改為誠實描述 30 ms crossfade 行為（Issue #1602 / PR #1610）。(4) 瀏覽器擴充功能修正 popup 中意外重複的「複製診斷資訊」按鈕，只保留一個可用副本；extension-check 新增互動控制 ID 重複檢查避免此類 bug 再現（Issue #1611 / PR #1612）。(5) DesktopCompat 自訂場景區塊新增離線場景同步佇列狀態標籤，顯示待同步筆數與已捨棄的最舊變更，讓離線使用者直接看出還有多少變更等待同步（Issue #1614 / PR #1616）。(6) Control model 的聆聽劑量指示器新增「剩餘安全時間」倒數文字，以小時或分鐘顯示在目前音量下距離每日建議上限還有多久；DesktopCompat 預覽與 WinUI 殼層同步顯示（Issue #1613 / PR #1619）。(7) 正式 WinUI shell 的校準精靈新增本機 PEQ 曲線預覽，讓使用者在套用校正前就能看到目標曲線與測量結果合成後的等化器響應形狀（PR #1625）。以上皆屬 user-space source/UI/engine contract/control/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章。


第四十三波（wave 43）整合增量已合併十項能力與修正：(1) 瀏覽器擴充功能在橋接斷線期間每秒回報已丟棄封包數，讓使用者確認分頁音訊仍在播放且封包持續被丟棄；橋接重新連線或擷取停止後心跳即停止，不會殘留計時器（Issue #1569 / PR #1582）。(2) Control model 的聆聽劑量指示器改為以 effective volume（含靜音與音量衰減）計算，不再只看 requested level，讓使用者看到的累積劑量更接近實際聽到的音量（Issue #1576 / PR #1584）。(3) Engine 對 VST3 tap 讀取加入 seqlock 驗證並把 tap 發布限制在有 lane 的 graph block，避免讀到半寫入幀或在無 lane 時發出無效 tap 資料；tap consumer 不再收到撕裂或不一致的快照（Issue #1575 / PR #1585）。(4) 正式 WinUI 在音量保護頁面加入聆聽劑量指示器顯示，讓使用者直接看到目前累積聽感負荷而非只能在 status JSON 中查找（Issue #1589 / PR #1590）。(5) Engine Preview 修正四情境並行 soak harness：bounded offline render 不再因情境間共享 state 或逾時假設而誤報，讓 per-App delivery 的長時間壓力驗證可在 CI 穩定執行（Issue #1562 / PR #1587）。(6) DesktopCompat 備用 UI 在自訂場景表單加入「音量連動等響度」核取方塊，對齊 WinUI 殼已有的 opt-in 功能；勾選後建立的自訂場景會帶入 loudness live update 旗標（Issue #1574 / PR #1579）。(7) DesktopCompat 備用 UI 新增主輸出「靜音」與 App 工作階段「App 靜音」兩個核取方塊，主輸出靜音會透過 QueueVolumeAsync 送出 fade-out 並保留音量設定，App 靜音由套用按鈕路徑自動帶入；回寫以 _updatingSession 保護避免重入（Issue #1586 / PR #1593）。(8) Control model 修正聆聽劑量的計費窗口標籤與靜音間隙處理：mute 期間不再被計入累積劑量，窗口標籤誠實反映實際統計範圍，WinUI binding 在資料更新後正確刷新（Issue #1592 / PR #1597）。(9) 瀏覽器擴充功能 popup 新增「複製診斷資訊」按鈕：一鍵把 capturing、bridgeConnected、bridgeReconnectState、droppedPackets 和 UTC 時間戳複製為匿名純文字快照，方便 issue 回報時附上連線狀態而不洩露任何音訊內容、網址或裝置識別碼（Issue #1596 / PR #1601）。(10) 瀏覽器擴充功能在 capture teardown 時正確停止 heartbeat 計時器，避免橋接關閉後仍持續查詢 dropped packet 數造成不必要的 runtime message（Issue #1595 / PR #1598）。以上皆屬 user-space source/UI/engine contract/control/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章。

第四十二波（wave 42）文件收尾註記：大膽現代視覺樣式已透過 PR #1478 與後續切片還原至 main；正式 WinUI 恢復實色 section card、footer accent 左邊框、隱含按鈕 6px 圓角、InfoBar 12px 圓角與 section 入場轉場。Issue #1555 的原始 UI 目標已由多個已完成整合切片覆蓋，本切片僅補上 baseline 收尾說明（PR #1572），不改 UI、engine、wire contract 或 runtime 行為。


第四十一波（wave 41）整合增量已合併兩項能力：(1) Engine Preview 的 WAV file source 新增控制面離線 polyphase resample：當檔案取樣率與 prepared sink 不同時，先驗證比值在 0.25x–4.0x 內，再用 bounded PersistentPolyphaseResampler 對整個解碼緩衝區做一次性轉換，讓 44.1 kHz 音樂檔在 48 kHz shared-mode endpoint 上直接播放而不再 fail-closed 拒收；rate-matched 路徑行為不變，status detail 誠實標示 resampled 資訊，超範圍或解碼失敗仍 fail-closed。此為控制面離線 evidence，RT path 維持無配置純拷貝（Issue #1517 / PR #1519）。(2) Control model 為 program-aware level controller 建立 bounded、fail-closed 的 adaptive correction EQ visual frame 發布契約：committed 且 enabled 的非 Strict Direct attachment 在 applied gain 相對上一個已發布值有 >=0.25 dB 變化且距離上次發布超過 rate limit 時，把確認幀存入既有 EqVisualSnapshotV1 cache；disabled、silence-gated、未掛載或 Strict Direct 時不發布新幀且 UI 保留上一個安全畫面，讓現代等化器能呈現內容音量校正正在作用的視覺回饋。此為 control-plane visual projection，不是 ISO 226 或實體音訊 evidence（Issue #1515 / PR #1522）。以上皆屬 user-space source/engine contract/control/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章。


第四十波（wave 40）整合增量已合併四項能力與修正：(1) Engine Preview 新增 opt-in WAV file playback source：--enable-wav-source 搭配 --wav-source-path 與選用 --enable-wav-loop 重播；preview 先以既有 v1 decoder fail-closed 驗證 Float32 PCM、sample rate 與聲道數，再把 bounded decoded block 接進 user-space graph 與 WASAPI handoff，wav-source status route 只在 sink 回報 rendered blocks 後顯示 Ready，並與 test tone、tab bridge、driver loopback 互斥。此為本機檔案播放的 user-space graph evidence，不是 endpoint policy、實體 driver 或 WaveRT delivery 驗收（Issue #1495 / PR #1509）。(2) Control model 與 Engine 建立 bounded、fail-closed 的 EqVisualSnapshotV1 request/reply 快照契約：成功且未靜音的 live phon recompute 把最多 32 點頻率／增益確認幀存入控制面 cache，UI 以 pull-only request 取得，讓現代 EQ 曲線隨等響度變化即時更新；malformed 或 stale 快照被拒絕、不會回退已確認畫面，缺快照時 UI 保留上一個安全畫面；同一分段線性曲線上的稀疏 loudness 控制點會加密到至少四點，不改變實際套用的 EQ response。此為 user-space control-plane visual evidence，不是音訊內容偵測、driver 或實體音訊 evidence（Issue #1498 / PR #1514）。(3) VST3 parameter-timeline 控制面功能整體移除：native timeline/editor/file-store/supervisor-surface contracts、C# surface model/view model、Compatibility Preview 編輯區、timeline schema、對應 contract tests 與文件描述都不再存在；bounded worker lane Hello/ProcessBlock exchange 保留，SceneCatalog wire 欄位為相容性保留但不再使用（Issue #1507 / PR #1510）。(4) Engine 修正 session route 在 device/session catalog 背景 poll 更新後間歇 bind 失敗的問題：三個 handle accessor 與 Route command drain 的 catalog sequence 檢查從「必須完全相等」放寬為「只拒絕未來值」，stale-but-valid handle 仍通過 index bounds-check 與 identity matching，未來 generation／sequence 維持拒絕，讓 per-App route bind 不再因一次 poll 更新就失敗（Issue #1512 / PR #1516）。以上皆屬 user-space source/engine contract/control/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章。

第三十九波（wave 39）整合增量已合併十二項能力與修正：(1) 正式 WinUI 在自訂場景卡片加入即時等響度重算 opt-in，設定會透過自訂目錄持久化並在重啟後回放，且以 scene-catalog byte 84 傳給引擎（Issue #1466 / PR #1473）。(2) 正式 WinUI 完成大膽現代視覺翻新：section card 採用主題感知的分層底色、加大圓角、accent gradient hero、hover 浮起動畫、nav pane 加寬與頁面留白增加，讓整體視覺層級更清晰（Issue #1470 / PR #1478）。(3) Engine schema 把 basic noise suppressor 的 enabled 欄位改為權威語意：enabled=false 一律 fail-closed 拒收而非靜默旁通，schema 鎖定 const true，SPEC-0003 同步記錄（Issue #1474 / PR #1479）。(4) Engine 修正跨聲道數場景切換時 loudness PEQ 與 IR attachment 編譯在舊圖形聲道數的問題：改為以 pending graph 聲道數準備附件，避免 commit 後每個 render block 失敗導致輸出斷音或靜音，新增 stereo→surround→stereo 回歸覆蓋，SPEC-0002 同步更新 pending-graph attachment 語意（Issue #1477 / PR #1486）。(5) 正式 WinUI 改善深色模式表面對比：section card 與 route item 底色從半透明改為實色，淺色模式同步提高不透明度，並為六個 section 加入垂直滑入轉場動畫，讓頁面切換更流暢（Issue #1483 / PR #1484）。(6) Engine Preview 新增 opt-in --enable-driver-loopback 音源：把 bounded 440 Hz packet chain 經 versioned driver stream ring encode/decode 再送入 WASAPI sink，與 browser tab bridge 共享 route slot 6 且互斥 fail-closed；Ready 需要同時確認 loopback packets 與 WASAPI rendering。此為 user-space packet-chain evidence，不宣稱 driver/WaveRT kernel delivery 或簽章（Issue #1476 / PR #1492）。(7) 正式 WinUI 微互動拋光：全域按鈕圓角統一 6px、InfoBar 圓角增至 12px、footer 加入 4px accent 左邊框；補上遺漏的 FooterAccentBorderStyle 定義修復合併後 runtime 錯誤（Issue #1494 / PR #1496；Issue #1500 / PR #1502）。(8) 瀏覽器擴充功能 popup 改為台灣中文介面，並在 bridge 未連線時顯示已丟棄封包數量，讓使用者能確認擷取中斷的影響範圍；wire format、CSP 與權限不變（Issue #1482 / PR #1497）。(9) 文件釐清 f64 model limiter boundary：明確 Group Master 與 limiter 的責任分界，不影響 runtime 行為（Issue #1493 / commit 8c1a5ffd）。(10) CI handoff-audit workflow 加入 20 秒啟動延遲：claim-admission 序列化後 GitHub API eventual consistency 有時間穩定，消除 label+assignee 連續變更造成的誤報紅燈（Issue #1499 / PR #1501）。(11) winui-shell-check 新增 StaticResource 資源存在性 gate：掃描所有 XAML 的 x:Key 定義與 StaticResource 引用比對 allowlist，未定義引用 fail-closed，防止合併後 runtime 才發現資源缺失（Issue #1503 / PR #1504）。(12) docs/state/BASELINE.md wave 38 段落語言一致性修正：把簡體中文混雜英文的敘述轉換為完整繁體中文風格，保留所有 Issue/PR 引用與 evidence boundary 措辭（Issue #1481 / commit c42c3592）。以上皆屬 user-space source/UI/engine contract/schema/docs/tooling/CI evidence；不宣稱 driver/WaveRT/HLK 或簽章。

第三十八波（wave 38）整合增量已合併五項能力與修正：(1) 正式 WinUI 把 DSP graph 節點卡片統一為共用 SectionCardStyle，讓圖形檢視與其他區塊卡片有一致的視覺層級（Issue #1455 / PR #1457）。(2) Engine 對內建 Game／Movie／Voice 場景預設開啟即時等響度重算（Issue #1445 / PR #1458）。(3) 正式 WinUI 把自訂預設列統一為共用卡片樣式，畫面節奏更一致（Issue #1459 / PR #1460）。(4) Engine 修正場景即時更新 opt-in 的提交順序，讓啟用旗標在最後一次 loudness PEQ commit 後仍保留，並新增 update_loudness_phon 直接回歸斷言（Issue #1463 / PR #1464）。(5) Engine Preview 把 BasicNoiseSuppressorV1 接進 tab bridge lane，成為明確的 --enable-tab-noise-suppressor opt-in：設定失敗 fail-closed、route-health 如實反映狀態；這是有界高通加向下閘門的基本降噪連接，不得宣稱 AI denoising（Issue #1456 / PR #1461）。以上皆屬 user-space source/UI/engine contract/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章。

第三十七波（wave 37）整合增量已合併六項能力與修正：(1) 正式 WinUI 為主要控制項加入 tooltip、統一 InfoBar 橫幅樣式並在 nav pane 加入 header，讓首次使用者能從控制項提示理解功能用途且頁面結構更清楚（Issue #1431 / PR #1435）。(2) Engine 接通 scene-driven live loudness enable path：custom SceneCatalogCommandV1 byte 84 控制 EqualLoudnessPolicyV1.live_update_enabled，SceneApply 在 opt-in 時掛載 loudness attachment 讓 VolumeNotification 驅動 update_loudness_phon；預設 fail-closed、每次 attachment prepare 重設為 disabled（Issue #1424 / PR #1437）。(3) 正式 WinUI 的 EQ visualizer canvas 訂閱 SizeChanged 事件，在視窗縮放時立即觸發 RefreshEqVisualCanvas() 加上 transition timer 重繪曲線，使用者不再看到 resize 後殘留舊座標的 EQ 曲線（Issue #1430 / PR #1433）。(4) 正式 WinUI 清理 VST3 timeline editor 的殘留程式碼：移除 MainWindow.xaml.cs 中孤立的 ShellVst3Section 引用與 OnVst3* handler，Compatibility Preview 不再建構、同步或訂閱 VST3 timeline editing surface，SPEC-0010 同步更新（Issue #1438 / PR #1442）。(5) 正式 WinUI 把 IR phase panel 的 inline Border 屬性替換為 SectionCardStyle 並加入 PointerEntered/PointerExited hover 行為，與其他 section card 一致（Issue #1444 / PR #1446）。(6) 正式 WinUI 新增 window placement persist：WindowPlacement.cs 以 bounded JSON 儲存在 %LOCALAPPDATA%\Hibiki DSP 下，App.xaml.cs 在 Activate 前還原 bounds 並在關閉時保存，clamp 到最小尺寸與 work area，corrupt 檔案 fail closed 回復預設值，讓使用者在下次啟動時保留自訂的視窗大小與位置（Issue #1439 / PR #1447）。以上皆為 user-space source/UI/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章能力。
第三十六波（wave 36）整合增量已合併十三項能力與修正：(1) Engine Preview 修正 tab bridge 圖形生命週期：啟動時為 --enable-tab-bridge 建立 stereo lane "tab-capture" → output group "main" 的 bounded graph 並在失敗時 rollback fail-closed，route-health 從永遠 Pending 改為反映 receiving/waiting/disabled 三態，Ready 判定以 WASAPI rendered blocks 為準而非 ring pop 成功，並對外呈現 4-slot capture queue 滿載時的 drop 計數（Issue #1382 / PR #1403）。(2) 正式 WinUI 把重複的 status-pill Border 樣式抽成 Styles/Input.xaml 內 StatusPillStyle 與 StatusPillCompactStyle 兩個 keyed style，替換約 18 個 inline 實例（含標題列連線 badge 的 success/caution 邊框覆寫），視覺不變但後續樣式調整只需改一處（Issue #1406 / PR #1409）。(3) Engine Preview 接通 live loudness volume link：VolumeNotification 以 bounded phon proxy 驅動 update_loudness_phon，mute=true 保持 requested_db proxy、未知群組 fail-closed，SPEC-0002 同步記錄語意（Issue #1361 / PR #1373）。(4) Engine Preview 在 status-only smoke 中驗證 tab bridge listener detail：state/listening/received/delivered/dropped 細節進入 status 輸出且 smoke 鎖定欄位語意，避免 listener 存活但 route 資訊失真的回歸（Issue #1396 / PR #1412）。(5) 正式 WinUI 新增 live EQ visualizer surface：等響度 PEQ 曲線以 canvas 呈現並隨場景/群組狀態更新，使用者能直接看到目前等響度形狀（Issue #1365 / PR #1374）。(6) 正式 WinUI ToggleSwitch 與 CheckBox 統一 MinHeight=32 與間距節奏，跨頁控制高度一致（Issue #1413 / PR #1414）。(7) extensions popup 完成深色主題樣式統一：背景、邊框與文字使用 Fluent theme resources，深淺色切換不再出現殘留亮底（Issue #1415 / PR #1416）。(8) Engine 對外提供 committed graph 的 bounded f64 model APIs：main 與 output-group 渲染共用 immutable graph、Group Master 與 limiter boundary，無 active graph 或不支援格式 fail-closed；float-only attachment 與 plugin latency compensation 明確排除，SPEC-0001 更新並新增 contract coverage（Issue #1408 / PR #1419）。(9) Engine 把 per-group live loudness debounce state 移入 LoudnessGraphAttachmentV1：切換群組不繼承前一群組的 250 ms 等待窗口或 phon 基準線，非 active 群組更新 fail-closed；同時 SPEC-0002 明確 mute=true 時 phon proxy 維持 requested_db 估算，並以 contract test 鎖定行為（Issue #1379 / PR #1420）。(10) 正式 WinUI 把 section card 重複的 Border 屬性收斂到 SectionCardStyle，保留 Padding=24 覆寫與 Transitions/AutomationProperties，hover/focus 行為一致且 markup 重複減少（Issue #1421 / PR #1422）。(11) 正式 WinUI 在 StartEqVisualTransitionTimer() 訂閱前先取消舊 OnEqVisualTransitionTick，避免快速屬性變更堆疊重複 handler 造成多餘 tick；無額外視覺行為變化（Issue #1423 / PR #1425）。(12) CI verify workflow 新增 commit-message integrity gate：對 pull request 範圍內每個 commit 檢查 issue 連結與訊息完整性，event SHA 以完整 40 字元解析、shallow checkout 缺物件時 fail closed，損毀或曖昧的 commit metadata 無法再混進主線（Issue #1418 / PR #1427）。(13) 正式 WinUI 移除 local-draft 的 VST3 timeline editor 介面：engine/host 整合仍在範圍外前，使用者不再看到不可用的編輯控制項，shell check 同步移除已刪 handler 的要求（Issue #1426 / PR #1428）。以上皆為 user-space source/UI/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章能力。
第三十五波（wave 35）整合增量已合併兩項 UI 拋光：(1) 新增全域隱含 ComboBox 樣式於 Styles/Input.xaml：CornerRadius=8、MinHeight=36，讓下拉選擇器與 TextBox 共用相同的視覺節奏，不需逐一修改實例標記（Issue #1404 / PR #1405）。(2) 完成 #1381 Fluent 控制拋光的剩餘項目：slider track 填充改為柔和 accent 色、section card 在 hover 時加入 accent 邊框高亮，並新增 Styles/ControlPolish.xaml 字典統一 slider 與卡片樣式，同時移除 #1383 遺留的 dead theme resource；正式 WinUI 的互動回饋一致性提升（Issue #1393 / PR #1397）。以上皆為 user-space UI/build evidence；不宣稱 driver/WaveRT/HLK 或簽章能力。
第三十四波（wave 34）整合增量已合併三項 UI 拋光：(1) 正式 WinUI 標題列加入喇叭圖示，不再顯示空白方塊；VST3 時間軸工具列按鈕統一 MinHeight=32；場景卡新增依序淡入動畫（每張間隔 40ms、上限 8 張），讓首頁載入更有節奏感（Issue #1387 / PR #1389）。(2) 剩餘按鈕統一尺寸：首頁 ConnectButton 升級為 AccentButtonStyle + MinHeight=40 + SemiBold 字重，與「一鍵改善」主行動一致；所有次要動作按鈕統一 MinHeight=32，跨頁視覺高度一致（Issue #1391 / PR #1392）。(3) 新增全域隱含 TextBox 樣式於 Styles/Input.xaml：CornerRadius=8、MinHeight=36、Padding=12,8，透過 App.xaml 資源字典合併套用到全部 15 個 TextBox，不需逐一修改實例標記；輸入欄位圓角與高度從此一致（Issue #1395 / PR #1398）。以上皆為 user-space UI/build evidence；不宣稱 driver/WaveRT/HLK 或簽章能力。
第三十三波（wave 33）整合增量已合併三項能力與修正：(1) 正式 WinUI Fluent 控制拋光：slider 填充、nav 選取指示器、卡片 hover 與 focus ring 對齊視覺層級，提升整體互動回饋的一致性（Issue #1381 / PR #1381）。(2) 正式 WinUI 狀態列新增 footer strip 與 accent toggle on-state 樣式，讓連線與功能啟用狀態更清楚可辨（Issue #1384 / PR #1385）。(3) Engine Preview 新增 opt-in --enable-tab-bridge 模式：搭配 --enable-wasapi-output 時綁定 127.0.0.1:17842 listener、接收 MV3 HIBT packet 並經 immutable graph 送入 WASAPI handoff；缺少 sink 時 fail-closed，route-health 新增 browser-tab 項目反映 receiver 實際狀態；SPEC-0009 同步更新（Issue #1382 / PR #1386）。以上皆為 user-space source/UI/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章能力。
第三十二波（wave 32）整合增量已合併九項能力與修正：(1) VST3 sandbox 在 runtime 終止失敗時把 Job Object 錯誤寫入 bounded crash report store，plugin 異常不再只留下難以追查的靜默終止（Issue #1335 / PR #1341）。(2) 正式 WinUI 修復 hero 區塊 FontSize 屬性格式，讓字型設定正確生效（Issue #1349 / PR #1350）。(3) Engine Preview 對 SceneCatalogCommandV1 upsert 的 reserved bytes 強制 fail-closed，拒收非零保留欄位，維持 wire contract 嚴格性（Issue #1348 / PR #1351）。(4) 正式 WinUI 視窗加入最小尺寸限制並統一各分頁水平邊距為 32px，縮放與跨頁瀏覽更穩定一致（Issue #1352 / PR #1355；Issue #1359 / PR #1360）。(5) extensions popup 診斷新增 bridge 重連進度，連線中斷後使用者能看到目前恢復到哪一步（Issue #1343 / PR #1357）。(6) Engine Preview 新增 bounded equal-loudness PEQ RT crossfade：更換等響度校正時以固定容量狀態平滑交叉淡換，避免爆音或參數跳變，RT path 維持零配置、零鎖、fail-closed（Issue #1354 / PR #1356）。(7) 正式 WinUI modernize NavigationView pane styling、remaining inner panels 對齊 CR12/P16、route-health row 對齊 CR10/P12、SceneCard 圓角統一為 16，讓整體視覺層級一致（Issue #1363 / PR #1364；#1366 / PR #1368；#1370 / PR #1371；#1375 / PR #1376）。(8) Engine Preview 新增 versioned bounded double-precision graph processing path：caller-owned float64 lane input/output 可在既有 immutable graph 上以 double accumulation 處理，預設仍為 float32 且未知格式 fail-closed；SPEC-0001 同步更新（Issue #1362 / PR #1369）。(9) accepted Spec review dates 同步至 2026-08-25，保持文件複審週期誠實（Issue #1367 / PR #1372）。以上皆為 user-space source/contract/UI/build/docs evidence；不宣稱 driver/WaveRT/HLK 或簽章能力。

第三十一波（wave 31）整合增量已合併五項能力與修正：(1) VST3 bridge 新增 sandbox lifecycle events 綁定 bounded crash report store，讓 plugin crash 報告能保留 host 端 lifecycle 變化並維持 bounded/redacted 儲存契約（Issue #1316 / PR #1327）。(2) WinUI 修復場景卡 hover 拋例外與連線狀態圓點固定顯示的問題，讓導覽互動不再中斷且連線狀態誠實反映（Issue #1324 / PR #1328）。(3) Engine Preview 的輸出取樣器從線性內插升級為固定容量 8 相位 × 16 tap polyphase FIR bank：支援 2/6/8 聲道、0.25x–4.0x source-step envelope、跨 block fractional phase 與 bounded history，ratio 變更不重置 stream；invalid input fail-closed，RT path 維持零配置零鎖；SPEC-0007 同步更新。此為 user-space DSP contract/test evidence，不宣稱真實多輸出 clock drift soak、USB/HDMI/Bluetooth unplug soak、WaveRT/driver rate behavior、HLK 或簽章（Issue #1310 / PR #1322）。(4) Engine Preview 新增 per-App process-loopback capture 到 physical WASAPI delivery 的整合路徑，讓選定 App 的音訊能以 bounded session catalog/route command 流程送到實體輸出 worker；此為 user-space WASAPI path evidence，不宣稱 driver/WaveRT 或全機 soak（Issue #1311 / PR #1333）。(5) 正式 WinUI 將 nested card corner radius 對齊視覺深度層級，提升 Expert Panel／自訂場景卡片的一致性與可讀性（Issue #1331 / PR #1332）。

第三十波（wave 30）整合增量已合併五項能力與修正：(1) handoff-check 的區塊擷取改為行首錨定：內文出現 handoff 標記文字時，解析器不再把一般散文段落誤判成 handoff 區塊，並新增散文提及標記的自我測試案例；這消除全域 handoff audit 偶發紅燈的根因之一（Issue #1289 / PR #1295）。(2) Engine Preview 新增 opt-in 的 `--enable-test-tone`：搭配 `--enable-wasapi-output` 時以 bounded 440 Hz、約 -20 dBFS 正弦訊號經 graph、limiter 與 WASAPI handoff 送出，sink snapshot 回報 rendered blocks 後狀態才顯示 test tone rendering；engine-preview-smoke 新增對應檢查，SPEC-0017 明確此結果限於 user-space WASAPI path，不構成真實裝置 delivery、WaveRT driver、HLK 或簽章證據（Issue #1284 / PR #1293）。(3) 新增 clean-closed-issue-residue 工具：批次偵測已關閉 Issue 殘留的 claim-pending／claimed／in-review lifecycle 標籤與 assignee，支援 dry-run 與自我測試，並完成最近 100 筆已關閉 Issue 的實際清理（Issue #1288 / PR #1296）。(4) WinUI 導覽從七頁縮為六頁：VST3 時間軸從主 NavigationView 移除，路由健康改為 Ctrl+5、Expert Panel 改為 Ctrl+6；VST3 時間軸仍是 Expert Panel 內的本機草稿面，SPEC-0010 同步更新導覽契約（Issue #1286 / PR #1299）。(5) VST3 worker 通訊協定新增 versioned multi-bus／side-chain ProcessBlock 契約：ProcessBlockMultiBus（id 9）與回應（id 10）支援最多 8 input／8 output bus、32 聲道、512 frames，reserved bytes 必須為零；codec fail-closed 拒收 NaN／Inf、layout 不一致、截斷 payload；SPEC-0008 更新對應段落。此為 wire contract evidence，實際 plugin dispatch 是後續工作（Issue #1283 / PR #1291）。以上皆屬 user-space source/tooling/docs/UI/process evidence。

第二十九波（wave 29）整合增量已合併八項能力與修正：(1) docs-check 新增 Spec/ADR `source_globs` 存在性 gate：每個 glob 項目必須至少匹配一個 tracked 檔案，否則 fail closed，防止文件指向不存在的路徑（Issue #1266 / PR #1268）。(2) handoff-check 的 issue body 解析器支援 CRLF 行結尾：admission workflow 與全域 handoff audit 在 body-parsing helper 內 normalize CRLF 後成功解析同一格式良好的 block，不再因格式差異誤報缺少 key（Issue #1269 / PR #1270）。(3) 自訂場景的 C# encoder 現在把 `ir_reference` 寫進 SceneCatalog wire command offset 312 / length byte 20，引擎端 apply_scene_catalog 能保留先前準備的 IR attachment；SceneCard persistence、offline replay queue、schema、SPEC-0014 同步更新，contract test 驗證完整往返（Issue #1259 / PR #1272）。(4) 正式 WinUI VST3 timeline editor 的按鈕分組為「draft」與「history」兩組，提升視覺層級與操作可發現性（Issue #1273 / PR #1275）。(5) PROJECT_MAP 的 docs-check 描述同步 BASELINE CTest 摘要 gate 與 Spec/ADR source_globs gate（Issue #1274 / PR #1276）。(6) CODEX_GOALS 的 /goal 啟動詞移除第二次壓縮後停止擴張的規則，讓長時間目標持續推進到真正阻擋為止（Issue #1277 / PR #1278）。(7) route health cards 新增 entrance transition，與 navigation shell 一致（Issue #1280 / PR #1281）。(8) claim-issue parser 支援 CRLF bodies 且只在 handoff block 內掃描 scope keys，消除 false positive（Issue #1279 / PR #1282）。以上皆屬 user-space source/tooling/docs/UI evidence。

第二十八波（wave 28）整合增量已合併三項流程與文件強化：(1) docs-check 新增 BASELINE CTest 摘要 gate：解析驗證摘要宣稱的 CTest 數量與名稱，比對 CMake 實際 `add_test` 註冊，漂移即 fail closed，self-test 覆蓋匹配、缺失、多餘與未知名稱情境；BASELINE 內的測試名稱同步修正為實際註冊的 `hibiki_` 前綴版本（Issue #1253 / PR #1258）。(2) 全域 handoff audit 遇到 claim-admission 換標籤過渡期的「零 lifecycle label」快照時，對單一 issue 做最多三次遞增間隔的重讀，恢復即通過；持續缺失與重複標籤仍 fail closed，單一 issue 檢查模式維持原本嚴格行為，消除多視窗併行時的暫態誤報（Issue #1260 / PR #1261）。(3) MULTI_AGENT 的認領流程說明對齊 SPEC-0004 序列化 admission：orchestrator 只 materialize handoff block，assignee、`claimed` label 與寫入權由 claim-admission workflow 授予；`claim-pending` 為非授權標記、TBD pre-claim 草稿補齊後同樣走 admission（Issue #1264 / PR #1265）。以上皆屬 docs/tooling/process evidence。

第二十七波（wave 27）整合增量已合併九項能力與修正：(1) evidence manifest 修復 home device action row 的 provenance 綁定，讓該 UI 增量可由可重跑的 source snapshot 追溯，而非依賴遺失的中間狀態（Issue #1231 / PR #1233）。(2) SPEC-0010 補上 NavigationView 結構與 Ctrl+1..7 快捷鍵記錄，導覽契約與實作保持同步（Issue #1229 / PR #1234）。(3) claim-admission helper 改為從第一個相依函式載入，離線自檢不再因萃取邊界漏掉前置定義（Issue #1235 / PR #1236）。(4) claim-admission 保留分支時正確傳入 `gh api` 子命令，避免序列化認領流程在 branch reservation 階段失敗（Issue #1238 / PR #1241）。(5) PROJECT_MAP 的 apps/ 描述同步 navigation-first shell，降低新貢獻者從地圖找入口時的落差（Issue #1237 / PR #1243）。(6) claim-pending audit 檢查順序對齊 SPEC-0004，先驗證 pending lifecycle 再判斷 branch ownership，避免合法請求被錯誤拒絕（Issue #1242 / PR #1245）。(7) admission post-swap 回滾涵蓋兩種 lifecycle label，readback 自檢改為實際呼叫 helper，嚴格模式空集合計數也一併修正；這讓搶佔失敗能可靠回到原狀態並暴露真實流程錯誤（Issue #1248 / PR #1250）。(8) driver stream ring consumer 整合 engine packet path 測試，驗證 ring consumer 輸出可餵入既有 outbound encode path 且 invalid packet fail-closed；此為 user-space contract/test evidence，不宣稱 kernel delivery、WaveRT、實體播放或簽章（Issue #1244 / PR #1251）。(9) context-pack `-Issue 0` 可直接建立 foundation bootstrap pack，不需 GitHub issue，self-test 新增端到端案例（Issue #1249 / PR #1252）；BASELINE 驗證摘要同步為四個 CTest，加入 `hibiki_driver_stream_tests`（Issue #1253 / PR #1254）。以上皆屬 user-space source/contract/tooling/docs/test evidence。

第二十六波（wave 26）整合增量已合併六項能力：(1) `hibiki_driver_stream_ring_v1` 提供版本化 driver→engine 共享記憶體 ring 契約：caller-owned storage、whole-block overrun 拒絕、underrun 靜音 fallback、bounded 2/6/8 聲道格式驗證，且不配置、不等待；契約測試覆蓋 lifecycle 與資料完整性，SPEC-0003 已同步（Issue #1209 / PR #1217），evidence provenance 以 snapshot mode 綁定合併後 commit `fe7ba36e`（Issue #1220 / PR #1224）。此為 user-space source/contract evidence，不宣稱 kernel ring delivery、實體播放、driver loading、HLK 或簽章。(2) 獨立 `hibiki_driver_stream_tests` 執行檔讓開發者能快速驗證 driver stream transport v1 wire layout、encode/validate/payload round-trip 與 fail-closed 拒絕路徑，不需跑完整 contract suite（Issue #1210 / PR #1222）。(3) WinUI shell 的系統音量、App 音量與 IR 相位強度 slider 新增可見 Header 標籤，使用者不再只看到滑桿本體（Issue #1221 / PR #1223）。(4) 正式 WinUI navigation shell 重構為 navigation-first surface，並新增 entrance transitions 與統一 presets grid layout，提升視覺層級與切換一致性（Issue #1201 / PR #1205；#1211 / PR #1212）。(5) 導覽頁面新增 Ctrl+1..7 鍵盤快捷鍵，讓熟悉鍵盤操作的使用者能快速切換區段（Issue #1216 / PR #1218）。(6) 首頁裝置控制整合成單一有標籤的動作列：「切換實體裝置」維持 Accent 主按鈕、「重新掃描裝置」為次要按鈕，先後順序一目了然；Click handler、binding 與 automation 語意未變（Issue #1225 / PR #1226）。以上 UI 變更均通過 formal WinUI build 與 accessibility scan，仍屬 user-space control/build evidence。
第四十三波整合增量已合併三項能力：(1) 正式 WinUI shell 新增引擎斷線提醒與連線感知的「連接／重新連接」按鈕，未連線時提供明確下一步，不再只靠狀態徽章（Issue #1194 / PR #1195）。(2) 引擎新增出向 driver stream packet encode path：lane 音訊先通過既有 graph、Group Master 與 limiter，再以 v1 contract 編碼 WaveRT render packet；invalid lane/format/freshness/non-finite samples 一律 fail-closed（Issue #1189 / PR #1191）。此能力目前是 user-space outbound encode evidence，對應 `evidence/1189/wavert-outbound-encode-v1.json`；不宣稱 kernel-mode IPC、WaveRT ring delivery、實體播放或 driver/HLK/簽章。(3) `apply_scene` 例外路徑現在也會冪等回滾 pending IR clear，避免例外後場景套用因 transaction conflict 卡死到重啟（Issue #1202 / PR #1204）。
第四十一波整合增量已合併：equal-loudness policy 已進入 bounded RT output attachment，render 順序為 graph → IR → loudness PEQ → Group Master → limiter，Strict Direct 維持 bypass；這是 user-space DSP/control evidence，不是 equal-loudness conformance、實體音訊播放或 driver/WaveRT 證據（Issue #1178 / PR #1188）。
第四十二波整合增量已合併五項能力與流程快照：(1) optional VST3 SDK 可在本機 unsigned build，worker 缺參數時 fail-closed，但 SDK processing 尚未接進 RT graph（Issue #1177 / PR #1180）；(2) claim admission 新增非授權 `claim-pending` lifecycle、序列化 GitHub workflow 與離線 pre-flight tool，降低同帳號多 AI 搶 claim 的風險（Issue #1176 / PR #1179）；(3) 十七個 schema 改用 shared printable-string `$ref`，語意契約不變（Issue #1181 / PR #1186）；(4) `DescribeSnapshotSourceSet` 讓 evidence snapshot provenance 可重建 source-set digest，並在審計中綁定 candidate index（Issue #1187 / PR #1190）；(5) `apply_scene` 在 loudness/PEQ commit 失敗時也會 rollback graph，避免 prepared transaction 殘留造成後續 revision conflict（Issue #1196 / PR #1197）。
第四十波整合增量已合併：physical sink clock-drift 的離線契約 fixture 覆蓋 invalid no-op observations、slow-clock adaptation、bounded drift、corrected source step、process boundary 與 reset recovery，並以 sink-clock-fixture-v1 evidence 記錄可重跑驗證；這是 deterministic user-space evidence，不是 real-device clock soak 或實體音訊播放（Issue #1118 / PR #1128）。
第三十九波整合增量已合併：calibration response 的 measured_db／target_db schema 契約收緊到 -144..12，SPEC-0011 同步記錄 bounded dB contract；本切片為 schema/spec/evidence 變更，runtime compiler 行為不變（Issue #957 / PR #960）。
- AI handoff now has a short canonical entry, Git-ancestry/document gate and source-only local
  preview command. A Compatibility Preview builds and completes a launch smoke on the non-target
  host using the same `EasyControlViewModel`; the formal WinUI XAML build now completes on a
  VS2026-class host (`visual-studio-msbuild` + `/restore`, 0 warnings/errors) and its shell
  launches with a UIA-recorded observable control tree, while hardware soak still awaits the
  locked target toolchain.
- The explicit `WinUICompat` target now skips the non-packaged `XamlControlsResources` merge that
  crashed during startup on the local host. With Microsoft Windows App Runtime 1.7 x64 installed,
  `tools/build-preview.ps1 -Target WinUICompat -SmokeTest` now keeps a `WinUI Desktop` window alive;
  this is a compatibility launch check only, not formal XAML/accessibility evidence.
- A C++ Engine Preview now owns the local control named pipe and passes a cross-process v1 Hello/Ack
  plus ControlStatusSnapshot smoke; the status exposes four conservative route states and the
  canonical volume mirror. `tools/run-preview.ps1 -Build` uses the runtime-aware `Ui=Auto` launcher:
  it selects WinUICompat when Windows App Runtime 1.7 is available and otherwise uses the self-contained
  Desktop Compatibility UI. It is deliberately driver-free and keeps the physical sink disabled by
  default; explicit `--enable-wasapi-output` binds the existing shared-mode worker and publishes
  `main-output` readiness without claiming driver or full playback evidence.
- The C# `EasyControlViewModel` now refreshes ControlStatus after acknowledged volume and Scene
  commands. `tools/control-model-engine-smoke.ps1` proves −18 dB/generation readback and Game
  One-Tap SceneApply across the real named pipe; this remains a user-space control proof only.
  Evidence is recorded in `evidence/0000-foundation/control-model-engine-v1.json`.
- `tools/live-system-volume-check.ps1 -WriteTest` now starts Engine Preview with
  `--enable-system-volume`, sends a volume notification through named-pipe IPC, verifies
  approximately −3 dB on the local endpoint via broker readback, and restores the original dB/mute
  state. It is opt-in user-space write-through evidence, not driver or WaveRT proof.
- `tools/live-session-volume-check.ps1 -WriteTest` now starts Engine Preview with session routing,
  creates an inaudible shared-mode test session, discovers it through the bounded catalog, sends a
  generation-scoped session handle through IPC/control queue/COM worker, verifies approximately −3 dB
  readback, and restores the original dB/mute state. This closes the target-session COM readback
  boundary; it also sends a bounded `SessionRouteCommand` and requires route catalog `Ready`. It
  remains user-space control evidence; physical per-App capture/re-send and DSP delivery are still
  unverified.
- The control-model Engine Preview smoke now exercises the full IR prepare → Scene IR clear
  round-trip and retries temporary fixture cleanup for bounded transient Windows file-indexer
  locks. Three consecutive session-routing runs are recorded in
  `evidence/0000-foundation/control-model-engine-ir-clear-v1.json`; this remains a user-space
  reliability proof only.
- Desktop Compatibility Preview now exposes a scene selector, route-health summary and volume
  origin/actuator text; scene selection is disabled until the engine is connected and remains
  command/Ack/status-refresh based. While connected it polls the bounded ControlStatusSnapshot once
  per second (coalesced while a command is busy) so external engine/Windows-volume changes can be
  reflected without touching the audio thread. It also displays the local render/capture catalog
  counts and default-render metadata; physical sink activation remains an explicit Engine Preview
  opt-in and switching/playback evidence remains out of scope.
- Both the formal WinUI source shell and Desktop Compatibility Preview now expose the bounded IR
  phase policy: Game minimum-phase/0 ms, Balanced mixed-phase/80 ms maximum, Movie linear-phase/
  160 ms maximum and Bypass. The fixed command now reaches Engine Preview and attaches the prepared
  IR to the user-space graph through an explicit prepare/commit transaction; no physical-sink or
  audible-device claim is made.
- The control-plane now decodes bounded RIFF/WAVE IR files (IEEE Float32 and signed PCM16/24/32),
  rejects malformed/non-finite/oversized input, and prepares a channel-major `IrConvolverV1` bank
  without file I/O on the RT thread. This is a file/import contract only; it does not derive
  minimum/mixed/linear kernels or prove physical-sink playback. Evidence:
  `evidence/0000-foundation/ir-wav-decoder-v1.json`.
- `build_ir_phase_kernel_v1` now performs the bounded control-plane phase transform: real-cepstrum
  minimum-phase reconstruction and source-magnitude causal linear-phase targeting for mixed/linear
  strength. Tests cover delayed impulses, independent channels, source-strength zero and Bypass
  fail-closed behavior. `IrPrepareCommand v1` now carries a bounded local path; C# and Desktop
  Compatibility Preview send it to Engine Preview, which reads/decodes/prepares on its control
  worker, calls `AudioEngineModel::prepare_ir` and commits the attachment before ACK. The RT render
  applies the immutable fixed-capacity convolver before Group Master/limiter; physical sink playback
  and device/driver evidence remain pending.
- SceneApply now prepares and commits an IR clear in the same control transaction because
  `SceneProfileV1` does not yet carry a file/reference. A previously prepared calibration cannot
  silently survive a scene switch; a new IR must be explicitly prepared afterwards.
- 公開 monorepo 文件、component license map 與 source-only paid-release policy。
- AI 接手規則、fresh-clone 流程與 source-only policy。
- `hibiki_driver_control_transport_v1` now provides a fixed 136-byte little-endian
  endpoint-state/volume-notification packet ABI. The GPL `DriverVolumeLinkV1` decodes it,
  suppresses registered event contexts and applies requested dB/mute through the canonical
  output-group safety path; its 16-byte header-only Hello/Ack/Error request-correlation path
  is also contract-tested. This remains source/control evidence and does not claim a loadable
  or signed driver.
- `OutputGroupVolumeState` 與 ISO compensation public C++ boundary 的初始骨架。
- Scene graph、device-switch transaction 與初始 CMake/CTest 驗證入口。
- Immutable RT graph snapshot、2/6/8 聲道 mapping、IPC frame codec、ASIO stream model、VST
  quarantine model 與 MV3 tab-capture source prototype。
- Caller-owned output ring buffer、clock-drift estimator、bounded linear SRC prototype、
  Apache C driver ABI 與 portable driver validator。
- Source-only PowerShell installer bootstrapper with manifest/hash dry-run gate.
- `ReleaseManifest v1` now requires toolchain/dependency/SBOM digests plus driver package/catalog
  and Microsoft signature metadata, and installer signer/RFC3161 metadata before a package can be
  staged; no signed payload is stored in this repository.
- Easy Scene factory、AcousticAnchor phon mapping、PEQ/APO/CamillaDSP/REW exporters 與 WAV IR
  serializer。
- `IrPhasePolicyV1` 與 C# binding-ready slider contract 已加入：Game minimum-phase 0 ms、
  Balanced mixed-phase 最多 80 ms、Movie linear-phase 最多 160 ms、Strict Direct bypass；
  目前只解析 latency budget，未宣稱已產生 FIR 係數。
- `EasyControlSession` provides a UI-independent fail-closed One-Tap Enhance contract, explicit
  Easy/Expert mode, scene selection and active output-group identity for future WinUI rendering;
  the installer source also rejects manifest path traversal and malformed SHA-256 entries.
- C# `IpcCodecV1`/`NamedPipeControlClientV1` mirrors the C++ little-endian envelope and bounded
  4-byte length framing; a cross-language known-byte fixture and malformed-frame checks are part
  of the control-model gate.
- `EasyControlViewModel` exposes binding-ready Easy/Expert state and emits validated SceneApply
  and VolumeNotification commands; `handle_control_frame_v1` hands those typed commands to a
  host-owned sink rather than running DSP on the pipe worker.
- `EasyControlViewModel` now exposes the fixed Main/Low Latency/Surround output-group catalog,
  bounded asynchronous Hello/Scene/volume transport, 40 ms latest-value volume debounce,
  explicit Connected/Degraded states and disconnect cleanup; `apps/winui-shell/` supplies a
  source-only WinUI 3 first-run shell that keeps Expert controls behind an explicit switch. The
  shell now renders a bounded read-only Matrix/DSP Graph/VST3/calibration summary through
  `ExpertSurfaceModel`; no unsent edit is presented as committed. The shell is not compiled on
  this machine.
- The control model now projects `VolumeSafetyStateV1` as separate requested/effective/safety
  values with origin, actuator and generation text, and rejects stale/unsafe snapshots. Expert
  also shows bounded route-health cards for Windows sessions, process loopback, browser tab
  capture and direct/bypass paths; defaults are conservative and never claim an unconnected
  adapter is active. `ControlStatusSnapshot` v1 now supplies a bounded versioned status message;
  its four local route entries remain conservative and do not claim physical per-App delivery.
- `ControlStatusSnapshotStoreV1` publishes a complete immutable volume/route-health snapshot;
  the named-pipe handler replies by request ID and the C# ViewModel rejects stale/malformed
  frames while preserving its prior safe state. The local live probe reports
  `volume=pass status=pass route_status=pass routes=4 status_sequence=4`.
- `SessionCatalogSnapshot` v1 now publishes a bounded App/session selection list through the
  worker-owned route coordinator. Generation-scoped ephemeral handles, safe metadata fallback,
  C++/C# codec/store/handler correlation and stale ViewModel replacement are covered; this is
  still a selection boundary rather than proof of physical per-App delivery.
- The source-only WinUI Expert view now renders the safe App session catalog, sequence and route
  state without exposing raw Windows session identity; App volume/routing commands use validated
  generation-scoped handles, while physical per-App capture/re-send remains unverified.
- `SessionVolumeCommand` v1 now carries only a generation-scoped handle, catalog sequence, dB
  and mute. C++/C# codecs, EngineControl callback, Windows runtime/coordinator stale guards and
  the C# ViewModel command builder are covered; the opt-in live session-volume probe now verifies
  IPC/control-queue delivery, target-session COM readback and restoration through the Engine Preview
  worker. Physical per-App capture/re-send remains unverified.
- Active catalog entries now opportunistically expose worker-read `ISimpleAudioVolume` dB/mute
  availability; expired/inactive/unreadable sessions remain visible with volume unavailable.
- `SessionRouteCommand` v1 now carries only handle/sequence/lane/output labels. The coordinator
  builds and validates a candidate registry/graph before commit, increments generation on success,
  and republishes status/catalog. The live Engine Preview probe now verifies the queued command and
  `Ready` catalog readback; physical process-loopback delivery remains unverified.
- Session volume/route runtime adapters now validate and enqueue from the EngineControl thread;
  a fixed 64-slot SPSC `SessionCommandQueueV1` is drained only by the COM worker after refresh.
  Direct synchronous read/write APIs still fail closed with `RPC_E_WRONG_THREAD`, while normal UI
  commands no longer touch Windows session COM objects from pipe/control callbacks.
- Expert source UI now allows selecting an App catalog entry and entering lane/output labels;
  it also mirrors available per-App dB/mute into bounded controls and sends a separate session
  volume command; disconnected, unavailable or stale submission remains visibly fail-closed.
- `SessionRouteRuleCommand` v1 now provides fixed 480-byte Upsert/Remove/Clear operations with
  bounded printable UTF-8 matchers, priority, makeup gain and gain-owner semantics. C++/C# codec,
  EngineControl callback, COM-worker queue handoff and candidate rule-store/route-graph transaction
  are contract-tested; physical active-session delivery remains unverified.
- Expert control model now provides a bounded 64-entry per-App route-rule catalog with atomic
  JSON persistence, stable priority ordering, validation/rollback on malformed files, and WinUI
  editor bindings. It can build SPEC-0023 commands only after a non-zero App catalog sequence;
  local save without an engine sync is explicitly shown as not yet applied. Selecting a session
  now previews the same case-insensitive App ID/name resolver as the engine; equal-priority
  ambiguity is fail-closed and never silently chooses a rule.
- `CalibrationResponsePointV1` and `compile_bounded_peq_correction_v1` now provide a deterministic
  control-plane measured-response to bounded PEQ compiler (16-filter cap, frequency/spacing/Q and
  boost/cut policy validation, explicit `limited` result) that feeds the existing APO/CamillaDSP/
  REW/Hibiki exporters. It is documented as a baseline, not a room optimizer or acoustic oracle.
- `.github/workflows/verify.yml` now runs the WinUI source gate, MS-PL driver boundary gate and
  repository JSON parse gate in addition to the existing source/docs/control checks; the
  `source-only-ci-check.ps1` gate rejects artifact/package/release uploads, signing permissions
  and tracked binaries.
- `handle_control_frame_v1` validates Hello/Volume/Scene/graph lifecycle commands before passing
  them to a host-owned typed sink; malformed or rejected commands receive Error without touching
  the graph.
- `ControlCommandQueueV1` provides a fixed 64-slot SPSC pipe-worker to control-worker handoff;
  overflow is fail-closed with a dropped counter and no allocation/lock/wait.
- `ControlPlaneHostV1` now owns the named-pipe server lifecycle, typed handler context and queue
  wiring. `start_with_queue` gives a host a single source of truth for pipe stop/cleanup while
  keeping command consumption on the separate EngineControl worker; the loopback contract test
  verifies a SceneApply round-trip and queue handoff.
- `WindowsControlRuntimeV1` now composes that host with the worker-owned physical-device service;
  start/stop ordering is host-first on teardown, refresh remains a COM-worker operation, and the
  unbound runtime contract fails closed without starting a pipe.
- `EngineControlWorkerV1` consumes that queue and applies the four Easy Scene presets through
  AudioEngine Validate → Prepare → Commit; invalid scene IDs leave the last committed graph
  and revision unchanged, while volume commands share the same Group Master path.
- `EngineControlWorkerV1::set_scene_preflight` adds an optional control-plane gate before graph
  Prepare; a failed VST3/state/calibration/safety preflight leaves the prior Scene, revision and
  graph untouched.
- `RtLaneSnapshotV1` now carries fixed-size output-group bytes and exposes a group-filtered render
  path; four-lane fixtures verify that selecting one group does not mix the other three.
- `AudioEngineModel` facade connecting graph transaction, Windows volume notification and RT
  processing with one Group Master gain.
- `AudioEngineModel` 的 RT Group Master 已改讀 release/acquire 64-bit Q16.16 dB/mute word；
  mutable Windows volume state 僅留在 control plane，避免 callback/worker 與 RT 讀寫競態。
- `VolumeRampProcessorV1` 已接入 AudioEngine：8 ms 一般音量、5 ms mute、15 ms unmute，
  並以 8/48 kHz fixture 驗證單調 ramp 與完成後的精確 gain。
- `OutputGroupVolumeBankV1` now keeps an independent canonical dB/mute/generation and RT ramp
  for each of up to 32 registered output groups; `AudioEngineModel::process_output_group` applies
  only that group's master, while the legacy volume API remains the `main` shorthand and
  Strict Direct bypasses Group Master.
- The control pipe now preserves the legacy 16-byte Main volume command and adds a validated
  48-byte grouped command; C++ and C# route the selected UI output group to its own canonical
  volume bank instead of silently writing Main.
- C# control model now exposes a bounded 32-entry custom Scene card mirror. Reserved built-in IDs,
  malformed IDs and over-capacity inserts fail closed; selecting a custom card still emits the
  existing SceneApply payload and never claims that its engine graph is loaded.
- `TruePeakLimiterV1` 已接在非 Strict Direct render 尾端：固定 8-channel、非有限值歸零、
  −1 dBTP bounded inter-sample guard；目前仍不宣稱正式 ITU/BS.1770 conformance。
- Windows-only `IAudioEndpointVolume` broker with non-blocking callback snapshot, dB/mute
  read-back and event-context write path; no physical endpoint was exercised on this machine.
- `WindowsControlRuntimeV1` now binds the default eRender/eConsole endpoint to that broker and
  exposes control-thread rebind/read/write/poll methods plus an endpoint-ID-preserving
  `refresh_default_volume_if_changed` path; the live local probe read the endpoint volume
  successfully (`volume=pass`) without touching COM from RT.
- `WindowsVolumeLinkV1` now provides the explicit control-thread bridge from broker snapshots to
  an `AudioEngineModel` output-group master, with self event-context filtering and stale/invalid
  generation handling covered by the Windows contract test. UI/Safety/Scene/Session event
  contexts are stable values from `distribution-profile.yml` and are registered by default.
- Engine Preview now has an explicit `--enable-system-volume` opt-in: it binds the current default
  render endpoint's `IAudioEndpointVolume`, mirrors external dB/mute notifications into Main Group
  Master, and writes UI volume requests back with the UI event-context GUID. The default Preview
  remains non-mutating; status-only broker smoke coverage does not send a volume command.
- Engine Preview now has an independent `--enable-session-routing` opt-in: it binds the current
  default render endpoint's `IAudioSessionManager2`, publishes a bounded `SessionCatalogSnapshot`,
  and drains App volume, lane/output and route-rule commands through the COM-worker-owned fixed
  queue. Desktop Compatibility Preview exposes the catalog and explicit Expert controls without
  raw Windows identity. This proves the selection/command and Windows session-volume control-plane
  boundary only; physical per-App capture/re-send and DSP delivery remain explicitly unverified.
- Engine Preview now has an independent `--enable-wasapi-output` opt-in: it resolves the active
  default render descriptor from the physical catalog, starts the existing dedicated shared-mode
  WASAPI sink worker, and projects its `Pending/Ready/Degraded` state into `main-output`. The normal
  launcher remains sink-disabled; the opt-in is a user-space output-boundary smoke only, with no
  claim of WaveRT, full graph playback, per-App delivery or target-device soak.
- Windows-only `IMMNotificationClient` watcher with bounded default/add/remove/property event
  snapshots, consumed by a worker-side transactional recovery coordinator with safe-start mute
  after endpoint invalidation or Audio Service restart.
- `PhysicalDeviceCatalogV1` now provides a bounded 32-entry control-plane render/capture catalog;
  endpoint identity/format is validated, each flow has one default, stale event sequence is rejected,
  and Disabled/Unplugged/Unknown endpoints cannot be selected. `DeviceRecoveryCoordinator` now
  resolves catalog entries before starting a rebind transaction. COM enumeration and physical
  hotplug soak remain external gates.
- `DeviceSwitch` v1 now has a fixed 288-byte C++/C# request and schema, strict endpoint/format/
  padding validation, catalog-sequence propagation and an explicit EngineControl handler gate.
  The WinUI shell mirrors selectable render cards and rolls back its UI transaction when the
  command send fails; it does not claim a physical switch before an engine Ack.
- `DeviceCatalogSnapshot` v1 now publishes a bounded 16-byte header plus 416-byte entries over
  the versioned control envelope. C++ and C# validate reserved bytes, strict UTF-8, formats,
  duplicate/default invariants and sequence ordering; the ViewModel atomically preserves its
  previous catalog on stale or malformed snapshots. The pipe client has an unsolicited-frame
  reader for a future engine metadata worker; COM enumeration is still external.
- `DeviceCatalogSnapshotPublisherV1` now converts a validated C++ `PhysicalDeviceCatalogV1`
  into one bounded snapshot frame without introducing COM or RT work. Engine Preview now feeds
  it from a COM-initialized, worker-owned Windows endpoint enumeration at startup and polls the
  watcher for metadata changes; the default path still does not open a physical sink, while the
  explicit WASAPI opt-in uses the same catalog to start the dedicated shared-mode worker.
- `DeviceCatalogSnapshotStoreV1` now serializes complete control-plane snapshot publication and
  replies, rejecting empty or invalid frames while retaining the previous safe snapshot. The
  Windows `PhysicalDeviceCatalogServiceV1` joins this store to worker refresh transactions; it
  never performs COM work from the IPC reply callback and commits catalog state only after wire
  publication succeeds.
- `WindowsPhysicalDeviceCatalogWorker` now owns the COM enumerator on a worker thread, maps
  render/capture state, friendly names, mix format and device period into a candidate catalog,
  and commits only after snapshot encoding succeeds. `WindowsPhysicalDeviceCatalogCoordinator`
  bridges watcher notifications to worker polling; `DeviceCatalogRequest` now has an explicit
  snapshot-reply provider path. Engine Preview binds this service and exposes the local catalog
  to the Desktop Compatibility Preview; target 24H2/driver/hotplug soak remains unverified.
- The opt-in `tools/live-device-catalog-check.ps1` probe now built and ran the worker against the
  local `IMMDeviceEnumerator`: 14 endpoints were enumerated, sequence 1 and a 5,840-byte snapshot
  decoded successfully. It prints counts only; this is local Windows 26200 evidence, not target
  Windows 24H2/WDK or driver/handoff soak evidence.
- The same live probe now starts `WindowsControlRuntimeV1` and requests the snapshot through the
  local named pipe; `runtime=pass request=pass` proves service → provider → IPC framing without
  printing endpoint identity data.
- The same live probe now requests `ControlStatusSnapshot` on a second bounded pipe transaction;
  this proves Windows dB readback, status-store publication, session-route summary and C++ wire
  decode (`route_status=pass`, status sequence 4). Browser tab capture, process loopback delivery
  and physical per-App rerouting remain pending.
- `EasyControlViewModel.ConnectAsync` now requests and atomically applies a fresh
  `DeviceCatalogSnapshot` after the Hello handshake; a disconnected or invalid request fails closed,
  preserves the last safe catalog and never claims that a physical endpoint was switched.
- The C# wire/model boundary now accepts one Active default per flow, including capture; only
  Active render entries remain selectable. This matches Windows endpoint metadata and prevents a
  valid default capture endpoint from invalidating the entire snapshot.
- Parameterized equal-loudness SPL-from-phon formula using caller-supplied legal parameters;
  the 1 kHz invariant and phon bounds are covered by CTest without embedding the licensed
  29-point coefficient table.
- `build_formula_compensation` now computes caller-supplied current/reference phon curves with
  1 kHz normalization, F3/boost limits and explicit `limited` diagnostics; it does not embed
  standard coefficients or imply calibrated SPL without an acoustic anchor.
- MS-PL WaveRT endpoint control-state core with fixed-format validation, Q16.16 dB safety
  ceiling, mute/generation ordering and Strict Direct behavior; the PortCls miniport is not
  yet wired and no `.sys` is built here.
- The MS-PL WDK property adapter now checks KS instance/value buffers and rejects ambiguous
  access verbs before touching endpoint state; this is source hardening, not a loadable-driver
  result.
- Its volume/mute handlers now negotiate bounded `KSPROPERTY_TYPE_BASICSUPPORT` with explicit
  GET|SET capability bits and buffer-size reporting; no `.sys` or WDK runtime result is inferred.
- The portable MS-PL endpoint state core now validates and copies an event-context GUID before
  changing requested/effective dB, mute or generation; overlong context input is fully atomic
  and covered by regression tests.
- MS-PL `endpoint_topology_v1` catalog fixes Main/Low Latency stereo render, Surround 7.1 render
  with Windows `0x63f` mask and Virtual Mic stereo capture, including permanent GUIDs, direction,
  default buffer and supported-rate flags; the catalog is portable input to future SYSVAD tables.
- Bounded multi-sink output fan-out now rejects all-disabled plans and non-finite input blocks
  before touching any sink buffer; enabled sinks receive identical same-layout copies. The
  `OutputFanoutRuntimeV1` now attaches one persistent clock/SRC pipeline per enabled sink,
  preflights bounded capacity and publishes only after all sink SRC passes succeed. The
  `AudioEngineModel::process_output_group_fanout` boundary now connects graph output, Group
  Master/limiter and the per-sink runtime without duplicating or restarting the graph. Capacity
  checks use each sink's current SRC phase/step rather than rejecting normal 128-frame buffers
  with a worst-case ratio.
- Persistent no-allocation linear SRC with phase and boundary-frame carry across output blocks;
  insufficient output capacity is rejected before partial consumption.
- VST host control model now requires trusted/certified same-channel descriptors and quarantines
  a lane on crash or missed bounded heartbeat; the source-only worker now exercises bounded
  Hello/Heartbeat/ProcessBlock passthrough/Shutdown IPC. An optional pinned-SDK catalog and
  worker-side one-main-bus processor now cover module/class discovery, 1/2/5.1/7.1 Float32
  dispatch, fixed scratch bounds, plugin latency reporting and bounded parameter point conversion;
  the optional worker also bridges
  ProcessBlock frames, while supervisor launch validation now passes explicit class/rate/channel
  fields. `Vst3SandboxProcess` also exposes bounded HelloAck and ProcessBlock exchange methods
  that verify request IDs, shape, payload and finite output and clear output on failure; Scene
  scheduling and private plugin-state persistence are now bounded source contracts, while
  third-party production certification remains pending. The
  `Vst3WorkerLaneSessionV1` control-plane bridge now binds that exchange to a stable lane token,
  latency projection, bounded parameter timeline and contiguous block-order/degraded state.
  `PluginHostModel` exposes prepare/handshake/process entry points and detaches the lane when its
  trusted/certified host enters `Quarantined`; this remains a source-level worker contract.
- `Vst3BusLayoutV1` now validates explicit Main/Auxiliary/Sidechain layouts (8-bus/32-channel
  bound, Main-at-zero, no Sidechain output, zeroed unused slots) and `PluginHostModel` quarantines
  descriptors whose declared layout does not match the main lane; actual multi-bus worker process
  support remains explicitly separate.
- `Vst3SceneAutomationSchedulerV1` now stores bounded timeline IDs and Scene/lane bindings,
  validates all references before activation, applies snapshots to prepared lanes and rejects
  concurrent per-lane blocks with explicit back-pressure; opaque plugin state remains a separate
  compatibility-gated feature with a fixed migration registry.
- `Vst3PluginStateStoreV1` now provides a private caller-owned 16-slot/1 MiB state boundary with
  plugin/class/module SHA-256 identity and state-version checks; the public schema is metadata-only
  and restore fails closed on mismatch or insufficient destination.
- Optional pinned-SDK `Vst3SdkProcessorV1` now maps component `getState/setState` through a bounded
  1 MiB `IBStream`, separating overflow, plugin-error and destination-size failures. The store and
  `PluginHostModel` now accept only an explicit caller-supplied migration handler for version
  mismatches; the fixed 16-rule migration registry routes approved identity/version pairs, while
  absent/failed/oversized migrations remain fail-closed. Scene state binding now uses the
  bounded coordinator; third-party compatibility certification remains pending.
- `Vst3SceneStateCoordinatorV1` now binds Scene/state IDs to plugin identity and target version,
  inspects private store metadata before activation and restores only into caller-owned buffers;
  its metadata-only contract is `scene-vst3-state-binding-v1`.
- `preflight_scene_vst3_state_v1` adapts the coordinator to the engine Scene transaction: Scenes
  without state references remain compatible, while bound Scenes must pass before graph Prepare.
- `AudioSessionRegistry` keys Windows sessions by endpoint plus session-instance ID, preserves
  user routing on metadata refresh, and supports independent lane/output-group/gain-owner binding;
  Windows `IAudioSessionManager2` worker enumeration now populates it; callbacks remain
  non-blocking and only publish a sequence.
- `WindowsAudioSessionRouteCoordinatorV1` now exposes bounded per-session volume read/write
  control through `ISimpleAudioVolume`, requiring a currently enumerated ephemeral session
  instance ID and finite −144…+12 dB input. Runtime/coordinator unbound, unknown, stale and
  invalid requests fail closed; this is a worker control boundary, not physical per-App rerouting.
- `SessionRouteGraphBuilderV1` compiles active bound sessions into graph lanes with explicit
  WindowsSession versus HibikiInternal gain ownership; two Chrome-session fixtures render into
  separate output groups without cross-talk.
- `AudioSessionRegistry::set_makeup_gain_db` now provides the bounded per-session gain mutator;
  metadata refresh keeps that value and rejects values outside −144..+12 dB.
- `ProgramAwareLevelControllerV1` provides an allocation-free, slow RMS-proxy default and an
  optional fixed-state `KWeightedProxy` path (high-pass plus high-frequency shelf), both with
  silence gate, bounded boost/cut and dB-per-second rate limiting. It is explicitly still a
  proxy rather than a BS.1770 conformance implementation; full gated LUFS/oracle work remains a
  release gate.
- `process_tab_capture_lane_v1` can apply that controller to one user-gesture-gated browser tab
  before graph processing, with a sample-rate match check and fail-closed behavior; no automatic
  microphone capture or denoising model is implied.
- `PeqProcessorV1` compiles up to 16 RBJ peaking filters into fixed per-channel state, and the tab
  lane can apply PEQ before program-aware level correction with matching sample-rate/channel checks.
- `IrConvolverV1` provides a fixed 4096-tap direct FIR with cross-block history and caller-supplied
  phase metadata; the tab lane can apply IR after PEQ and before program-aware level correction.
- `BasicNoiseSuppressorV1` provides a fixed high-pass/downward-gate effect with bounded policy and
  no allocation; tab effects now order PEQ → IR → suppressor → program-aware level correction.
- `LaneConfigV1` now supports an optional validated 8×8 channel matrix in the immutable RT
  snapshot, while the legacy one-destination `channel_map` remains the default; Strict Direct
  rejects matrix-enabled lanes.
- Worker-side session volume read/write now uses `ISimpleAudioVolume` with dB↔scalar conversion,
  event-context GUIDs and readback; unbound/exclusive/vendor ASIO paths remain explicit bypasses.
- `OutputSinkModel` now joins clock-drift estimation to persistent SRC per sink, preserving
  phase while applying bounded `base_step / sink_source_ratio` correction. Deterministic offline
  physical-sink clock-drift fixtures cover invalid no-op observations, slow-clock adaptation,
  bounded drift, corrected source step, process boundaries and reset recovery (Issue #1118 /
  PR #1128); real-device clock evidence/soak is still pending.
- Optional source-only native ASIO COM transport now builds when a local pinned ASIO SDK is
  supplied: stable CLSID, eight Float32 output channels, 32--4096 frame buffers, supported
  sample rates, callbacks, sample position and ASIO registry routines. It remains disabled in
  normal CI and does not yet connect buffers to a physical sink or the signed virtual endpoint.
- `SceneSafetyController` now provides a tested control-plane policy for smart scene attenuation:
  it rate-limits true-peak overage actions, detects manual Windows volume overrides and restores
  the remembered scene baseline only when it is safe to do so.
- `OutputCrossfade` now provides a no-allocation equal-power sink handoff primitive with a
  30 ms default-compatible path for 2/6/8 channels; it is tested independently from the
  still-pending physical endpoint and clock soak fixtures.
- `OutputHandoffCoordinatorV1` now gates DeviceSwitch commit on a completed 30 ms crossfade and
  preserves the previous active target on rollback.
- `Vst3SandboxProcess` now provides a Windows Job Object containment layer with explicit
  launch validation, heartbeat watchdog and crash quarantine; no plugin binary is bundled.
- `vst3_worker_protocol.hpp`/`.cpp` now provide a fixed 36-byte little-endian worker frame codec
  with Hello/Heartbeat/Process/Shutdown/Error plus bounded `ProcessBlockWithParameters`, exact
  Float32/parameter-point validation and finite sample checks. The optional SDK worker decodes
  those points into the processor's official `IParameterChanges`; named-pipe transport, factory
  catalog and one-main-bus SDK processing build locally, while supervisor UI timeline editing and
  certification remain pending; Scene-to-migration binding now uses the bounded coordinator and
  private cross-version state remains guarded by the explicit identity/version registry.
- `LatencyAlignmentPlanV1` and `FixedDelayLineV1` provide a fixed 8-channel, 16,384-sample
  bounded delay primitive with active-lane max-latency alignment, impulse and non-finite-input
  tests. `LatencyGraphCommitV1`/`LatencyGraphCommitterV1` now bind that control result to stable
  lane tokens and graph revisions with stale-base rejection and rollback. `LaneLatencyBankV1` is
  prepared before graph commit and applied cross-block in the RT mixer without allocation; physical
  sink and third-party plugin end-to-end evidence remain pending.
- `VirtualMicRouteModel` now defines fixed 1/2-channel capture/reference blocks, fail-closed
  privacy mute and explicit echo-reference enablement. `VirtualMicDspV1` adds an optional bounded
  normalized-LMS echo-reference canceller and slow noise gate with no allocation; it remains a
  baseline, not acoustic AEC/NS conformance or a loadable virtual capture driver.
- `Vst3WorkerPipeV1` is now attached to `Vst3SandboxProcess`: optional launch pipe setup passes
  `--hibiki-pipe`, bounded overlapped connect/read/write is exposed only to control/IPC callers,
  and stop closes the pipe with the Job Object. `hibiki_vst_worker` provides the bounded
  worker-side client loop; the actual SDK/plugin executable remains pending.
- The MV3 tab-capture source now packetizes user-requested audio through an AudioWorklet into
  validated `HIBT` Float32 frames and optionally sends them to localhost; missing bridge leaves
  browser playback intact, while native receiver/noise-reduction remain separate boundaries.
- `hibiki_tab_bridge_contract` now validates HIBT packet framing, supported LPCM layouts/rates,
  exact payload length and finite samples without owning or allocating audio buffers.
- The optional Windows tab bridge now owns a loopback-only WebSocket listener with bounded
  handshake/frame parsing and control-thread callbacks; engine lane routing and denoising model
  provenance remain intentionally outside the receiver.
- `TabCaptureQueueV1` now provides a four-slot fixed SPSC handoff for validated HIBT packets;
  `enqueue_tab_capture_packet_v1` is a ready callback adapter, with bounded dropped-block reporting
  and no allocation/wait on the pop path. `process_tab_capture_lane_v1` feeds the selected lane's
  immutable graph and Group Master without owning audio buffers and can apply the optional
  program-aware level controller before the graph.
- The MS-PL WDK source boundary now has a property-dispatch scaffold for volume/mute that calls
  the portable Q16.16 endpoint core; it is source-checked but intentionally not a loadable `.sys`.
- WDK endpoint-indexed stream/property entry points now consume the fixed topology catalog for
  render/capture geometry, channel mask and endpoint identity; the Virtual Mic capture entry uses
  the same format contract. This is still only a source boundary and has no target WDK, HLK or
  Microsoft-signing evidence.
- Issue #394 已在本機 26100 家族 WDK 工具鏈完成第一次 kernel-mode WaveRT PortCls adapter 建置：
  tools/build-driver.ps1 動態選擇最新含 km headers 的 kit、以核心模式編譯 portable C cores
  與 driver/wdk/**、連結 HibikiVirtualAudio.sys，INF 複製後由 genuine Inf2Cat 產生 .cat，
  並以 driver-signability-check.ps1 -PackageRoot .local/driver-package -RequireInf2Cat 重驗
  封裝（evidence/0000-foundation/driver-sys-build-v1.json）。輸出全部留在 ignored .local/；
  這是 build/package/signability evidence，仍無安裝、載入、runtime audio、HLK 或 Microsoft
  signing 宣稱。
- Issue #433 已在本機完成 WaveRT adapter 的測試簽章封裝 evidence：以 .local/certs 下自建的
  self-signed code-signing certificate（PFX/CERT 留在 ignored .local/certs，私密金鑰不進 Git）
  以 signtool /fd SHA256 同時簽署 HibikiVirtualAudio.sys 與 .cat，signtool verify /pa 如預期
  停在 untrusted root（evidence/0000-foundation/driver-load-test-v1.json）。憑證匯入
  Trusted Root／TrustedPublisher、TESTSIGNING 開機旗標與 pnputil 安裝仍是需要使用者同意的
  下一步；此 evidence 不宣稱 endpoint 出現或任何音訊行為。
- Apache-2.0 `hibiki_asio_transport_v1` now provides a fixed-layout SPSC shared-memory ring. The
  optional native ASIO DLL writes eight-channel Float32 blocks after callbacks, and
  `AsioTransportConsumerV1` creates/owns `Local\\HibikiDSP_v1_asio` for an allocation-free pop;
  `AudioEngineModel::process_asio_transport` now runs that block through the selected graph lane
  and Group Master, while `process_asio_transport_to_wasapi` submits the processed block once to
  the dual-worker sink handoff. Physical driver/endpoint delivery remains pending.
- `WindowsWasapiOutputV1` plus `WindowsWasapiSinkWorkerV1` now supply a Windows shared-mode
  physical render boundary: one dedicated sink-worker apartment owns COM bind/start/stop, event
  waits, bounded SPSC blocks, silence underrun fill, Float32-to-Float32/PCM16/24/32 conversion,
  persistent SRC and clock-observation updates. The opt-in silent local handoff probe now passes
  active/candidate warm-up and 30 ms commit on a 6-channel 48 kHz endpoint; the graph RT thread
  never calls this COM API, and target hotplug/HLK soak remains pending.
- `WindowsWasapiSinkHandoffV1` now permits a new candidate `begin` after a failed candidate
  rollback while the active worker remains ready; the live silent probe verifies rollback,
  active-worker retention, retry, 30 ms fade and commit on a local 6-channel endpoint.
- `WindowsWasapiSinkWorkerV1` now classifies device/service invalidation and retries bind/start
  inside its dedicated worker; ordinary event timeouts do not trigger recovery. This is source-
  level only until an actual Audio Service restart/hotplug fixture is run.
- `IpcNamedPipeServerV1` provides the Windows control-plane worker boundary with overlapped,
  bounded read/write, local-only pipe validation, request decoding and callback response framing;
  a Windows loopback contract test exercises Ack/request-ID round-trip.
- `driver/inf/HibikiVirtualAudio.inf` is a source-only MS-PL package template with stable Root
  hardware identity, four endpoint GUIDs and service boundary; it references only the future
  signed SYS/CAT and remains non-installable from a fresh clone.
- `driver/include/hibiki/wavert_stream_v1.h` and `src/wavert_stream.c` now provide a portable
  WaveRT Float32 ring boundary: caller-owned storage, whole-block overrun rejection, counted
  underrun silence fallback, bounded 2/6/8-channel format validation and no allocation/wait.
- `driver/wdk/hibiki_stream_adapter.cpp` now provides a WDK-only spin-lock adapter for render
  submit/read/reset and the ring's underrun-safe fallback; it remains source-only until a real
  SYSVAD/PortCls project compiles it.
- `sdk/driver_stream_transport_v1` defines a fixed 80-byte driver→engine packet header and
  allocation-free C encode/validate/payload APIs; `driver_stream_bridge.hpp` copies validated
  packets into finite-value caller-owned lane storage without linking MS-PL driver code.
- `AudioEngineModel::process_driver_stream_packet` now gates those packets by render type,
  endpoint GUID, engine sample rate and active-lane channel count before routing them through the
  existing lane graph.
- `process_driver_stream_packet_to_wasapi` and the shared `process_lane_block_to_wasapi` now route
  validated driver render packets through the same graph/Group Master/limiter and one bounded
  WASAPI handoff used by Hibiki ASIO; mismatched endpoint, format or sink state fails closed.
- `WindowsWasapiOutputV1` now acquires `IAudioClock` and the dedicated worker feeds device
  position/QPC deltas into per-sink SRC; extensible 2/6/8-channel formats also enforce their
  expected channel masks. This remains source-level until real endpoint soak is run.
- `process_virtual_mic_lane_v1` applies the fail-closed privacy gate before sending caller-owned
  capture blocks through the shared lane graph, with optional bounded VirtualMicDsp cancellation/
  gate; it still does not claim acoustic AEC/NS conformance or a loadable capture driver.
- `process_tab_capture_lane_to_wasapi_v1` now reuses tab PEQ/IR/suppressor/level effects and the
  shared lane-to-WASAPI adapter, so a user-gesture-gated browser tab follows the same Group Master,
  limiter and device handoff semantics as ASIO and driver lanes.
- `process_virtual_mic_lane_to_wasapi_v1` now applies privacy/optional bounded VirtualMicDsp before
  the same lane-to-WASAPI adapter; it remains a monitor/output boundary, not a signed capture driver.
- `WindowsWasapiFanoutV1` now validates up to eight enabled, unique same-layout/rate sinks and
  coordinates an independent handoff per sink; any physical submit failure is reported as degraded.
- `AudioEngineModel::prepare_wasapi_fanout` and `process_output_group_to_wasapi_fanout` now connect
  that physical fan-out to graph output, applying Group Master/limiter once before all sink submits.

## 尚未開始

- 可載入的 WaveRT/SYSVAD-derived driver、ASIO physical sink delivery、Scene-wired out-of-process
  VST3 SDK plugin host、WinUI 3 SDK/build and accessibility validation、real-device sink clock
  evidence/soak 與 signed package delivery。
- equal-loudness 合法係數來源與正式 conformance oracle（公式本身已完成，但係數資料仍待
  授權／法務確認）。
- Microsoft driver signing、Gumroad release artifact 與 production installer。

toolchain lock 已依 ADR-0005 對 SDK/WDK 改採最低基線 >= 10.0.26100：目前開發機是
Windows build 26200、VS 2026／SDK-WDK 26100 家族，符合基線。user-space tests 可通過，
但本機結果仍不是 driver 安裝、載入、runtime audio、HLK 或簽章的 target evidence。

## 最近驗證

初始 foundation evidence 已寫入 `evidence/0000-foundation/initial.json`，目前對應
Windows volume/device、ISO formula、recovery、driver control-core/INF template、persistent SRC、
VST worker、control pipe/payloads、session volume adapter、sink clock pipeline、optional native
ASIO transport/ring、tab/Virtual Mic lane adapter、session-route/output-handoff、control-model
與 VST3 latency graph commit／RT lane latency bank／parameter timeline／Scene automation refs／rollback lifecycle／UI device fade／plugin lane token／VST3 supervisor handshake/process exchange／VST3 worker lane timeline bridge／PluginHostModel worker-lane wiring／VST3 Scene automation scheduler／VST3 private plugin-state store／VST3 bounded SDK IBStream／VST3 state migration registry／VST3 Scene state coordinator／VST3 bus-layout admission validator／physical device catalog／stale device event rejection／catalog capacity boundary／ISO formula compensation builder／source-only CI publication gate／WDK property request hardening／multi-sink fan-out、per-sink clock/SRC runtime、AudioEngine fan-out boundary、精確容量 preflight、高倍率 SRC phase guard、portable WaveRT stream ring、WDK pin adapter、driver→engine stream packet bridge、endpoint identity、graph lane binding、WASAPI IAudioClock drift path、雙 worker WASAPI handoff、graph-to-WASAPI adapter、ASIO-to-WASAPI path、driver-to-WASAPI path、tab-to-WASAPI path、virtual-mic-to-WASAPI path、Windows WASAPI fan-out graph adapter、K-weighted program-level proxy、WinUI Expert readonly surface、VST3 Scene state preflight adapter、第三方 state compatibility review checklist、migration output overflow fail-closed coverage、WinUI accessibility source gate、bounded per-App session route rules、64-rule capacity、Windows watcher enumerate 套用、per-output-group volume bank、bounded custom Scene catalog/resolver、grouped volume IPC、C# custom Scene card mirror、custom Scene persistence coverage 的 fail-closed safety baseline `fa5b219`；
新 AI 接手時仍必須確認
working tree 與該 scope 是否一致。

目前 catalog-gated recovery rebind 的 source commit 是 `bc66229`；DeviceSwitch control-plane
與 WinUI picker 的 source commit 是 `a97e9f8`；DeviceCatalogSnapshot 的 source commit 是
`9dc903a`；catalog publisher 的 source commit 是 `9be7f15`；Windows COM worker／request
provider 的 source commit 是 `0b2800f`；opt-in live probe 的 source commit 是 `b92cc1f`；連線後自動刷新
裝置清單的 source commit 是 `80d9cad`；snapshot store stale-sequence fail-closed 的 source
commit 是 `a18e785`；live probe service-provider coverage 的 source commit 是 `25bd3a3`；其餘較早
scope 仍以下方 evidence manifest 的各自 commit 與限制為準。
ControlPlaneHost pipe/queue lifecycle 的 source commit 是 `a087e96`。
WindowsControlRuntime 與 runtime pipe request probe 的 source commit 是 `bc6952d`。
Driver endpoint state atomic invalid-context guard 的 source commit 是 `6b3f7fb`。
WDK volume/mute basic-support source gate 的 source commit 是 `1572b5f`。
WindowsControlRuntime default endpoint volume binding/read/poll 的 source commit 是 `df36929`。
WindowsVolumeLink broker-to-engine adapter 的 source commit 是 `a375ff5`。
Endpoint-ID-preserving volume rebind 的 source commit 是 `4d9e1d5`。
固定四組 volume event-context GUID 與自動註冊的 source commit 是 `1ebf026`。
WASAPI PCM render conversion 與 silent 30 ms live handoff probe 的 source commit 是 `9d0d426`。
WASAPI rollback/retry state-machine fix 與 live probe 的 source commit 是 `135c7ac`；target
Audio Service restart、hotplug、HLK 與 signed endpoint evidence 仍未完成。
WASAPI service/device invalidation recovery source commit 是 `5333ac4`；本機尚未注入實際
restart 或拔插事件。
Driver signability source gate 的 source commit 是 `dc1d3b2`；預設只驗證 INF contract，
目標 WDK package 才能執行 Inf2Cat。
Topology-indexed WDK render/capture pin formats 與 Virtual Mic generic format boundary 的
source commit 是 `741a54b`；文件與 evidence 對應 commit 是 `d2c8717`，仍未宣稱 `.sys`、HLK
或 Microsoft signing。
ReleaseManifest custody metadata source commit 是 `b1c64d4`；documentation gate 擴充的 source
commit 是 `2a6aa8f`，文件與 evidence 對應 commit 是 `015a4eb`。
ReleaseManifest hash-casing schema compatibility fix 的 source commit 是 `8ae3499`；目前
evidence manifest 以此 commit 為 source anchor。
Anonymous live Windows audio-session probe 的 source commit 是 `6fb6efc`；documentation
gate 對應 commit 是 `6154a5c`，本機 probe 僅證明 session enumeration，不證明每個 App 已完成
實際 Lane routing 或 DSP delivery。
Windows session→immutable route-graph coordinator 的 source commit 是 `2822461`；它已接上
watcher、rule store 與 graph candidate 的 fail-closed control-plane boundary，但沒有宣稱
實際 per-App audio capture、physical routing 或 DSP delivery。
Windows process-loopback source boundary 的 source commit 是 `3cd4620`；`d18a224` 補上
官方 FTM/agile completion handler 與 opt-in live probe。它使用官方
`ActivateAudioInterfaceAsync` 建立 process-tree shared-mode Float32 capture，包含固定容量
讀取、overflow drop 與明確 Degraded 狀態；`3d3735b` 再把 caller-owned block 接到既有
Lane graph／WASAPI handoff。兩者尚未在目標機注入含音訊程序、Audio Service restart 或
完成實體 per-App 重送；`2a85ea5` 新增 active session→bounded process request plan，
對同一 PID 的不同 Lane/output 以 `AmbiguousProcess` fail-closed。
`aac9274` 再拒絕不同 PID 共用同一 Lane 的 `DuplicateLane`，與 graph duplicate invariant 對齊。
`7d43e67` 將 requested/effective/safety dB、origin、actuator、generation 與 bounded
route-health cards 接到 Easy／Expert control-model；它只顯示保守的 session、process loopback、
瀏覽器單分頁與 direct bypass 邊界。`e97fb90` 接上 ControlStatusSnapshot 的 C++/C# wire、
store、handler 與 atomic ViewModel apply；本機 status probe 通過，但仍不宣稱 physical
per-App delivery 或 browser tab capture 已完成。

VST3 自動化時間軸資料鏈已合併（皆為 user-space source contract）：
`9f5f02e` 加入 supervisor 端 bounded `Vst3TimelineEditorV1` 編輯交易（draft/commit/
discard，upsert 以同一 (parameter, position) 取代）；`8a041a2` 把編輯交易綁進
`Vst3SceneAutomationSchedulerV1`（begin/commit/cancel + `timeline_snapshot` 讀回，
編輯中拒刪 slot）；`ff46e15` 提供 canonical fail-closed JSON 持久化
（`vst3-parameter-timeline-v1.schema.json`，64 KiB 上限、逐位元組穩定 round-trip）；
`dbafc4f` 加入 fixed-capacity per-timeline 檔案儲存 `Vst3TimelineFileStoreV1`
（嚴格檔名安全 ID、Windows 保留名拒絕、temp-write-then-replace）；
`9cca03d` 加入 store→scheduler 的 `sync_timeline_store_to_scheduler_v1`
（單項失敗計 skipped 不中斷）；`b7e2ea8` 加入唯讀內省
（排序 `timeline_ids` 與不可變 binding views）。UI 編輯介面、side-chain/multi-bus
worker process 與第三方 certification 仍待完成；以上不宣稱任何實體音訊或 driver 能力。

VST3 supervisor 端 selection-aware 編輯 surface 已合併（Issue #351 / PR #377）：
`Vst3TimelineSupervisorSurfaceV1` 把 `Vst3TimelineEditorV1` 和非擁有的
`Vst3TimelineFileStoreV1` handle 組合成單一 fail-closed facade；attach/detach、
select(id)、編輯轉發與 save_selected() 在未 attach 或未選取時一律拒絕，dirty
狀態由已發布 snapshot 與載入/儲存 baseline 比較推導。證據為
`evidence/0000-foundation/vst3-supervisor-surface-v1.json`；SPEC-0008 已加入對應小節。
這是 headless 控制面契約，不宣稱 UI 編輯器或實體音訊能力。

流程 gate 本週新增：#21 讓 BASELINE 摘要計數 fail-closed 對照 git 實測；
#51 把 docs-check 改成 merge-ref 感知（PR 未動 BASELINE 時容忍自身 tracked/JSON
漂移、push-to-main 與本機維持嚴格），並附 `-SelfTest`；#25 讓 active handoff 的
scope_globs 重疊直接 fail-closed。gate 腳本需要 PowerShell 7（PS 5.1 無法執行）。

SDK／tooling 增量已合併：C# control model 加入 Vst3TimelineSurfaceModelV1 surface model、
binding-state notifications 與 observable timeline editor view model（含 RemoveSelectedRow
與 undo-after-remove coverage）；supervisor surface 轉發 bounded history introspection 與
ClearHistory()；sandbox 啟動失敗以 redacted diagnostic reason codes／incident summary 記錄。
對應 evidence 有 vst3-timeline-surface-model-v1.json、vst3-timeline-binding-state-v1.json、
vst3-timeline-editor-viewmodel-v1.json、vst3-timeline-editor-remove-selected-row-v1.json、
vst3-timeline-remove-selected-row-undo-v1.json、vst3-supervisor-history-introspection-v1.json、
vst3-timeline-clear-history-v1.json 與 vst3-sandbox-redacted-diagnostic-v1.json。SDK 邊界同步
fail-closed 收緊：ASIO shared-memory transport 拒絕非零 reserved bytes；driver-stream packet
拒絕零 sequence/generation；driver-control 拒絕零 request ID。工具面增量：handoff-check 改用
UTF-8 編碼、extension gate 加入 tabCapture owner guard 與 tab-only media constraints、
driver-source-check 接受兩代 PortCls notification-buffer naming、verify workflow 以 concurrency
group 取消被取代的 run。以上皆為 user-space/source evidence，不新增實體音訊或 driver 載入宣稱。

第二波 tooling／UI 增量已合併：build-driver.ps1 改為從腳本位置錨定 repo root、把 obj/package
輸出固定在 .local 內並加 containment 檢查，新增 -SelfTest 驗證 root 探索、輸出範圍與 source
boundary（Issue #444 / PR #445）；handoff-check 可解析 TBD pre-claim draft 並驗證其 issue/branch
欄位（newline-stable self-test，Issue #439 / PR #442）；WinUI MainWindow.xaml 改用 Mica BaseAlt
backdrop、Fluent theme 筆刷與 type ramp styles（含 AccentButtonStyle）取代硬編碼色彩
（Issue #438 / PR #443）。皆為 source/tooling evidence，不宣稱正式 XAML build 或視覺驗收。

第三波 tooling／UI 增量已合併：WinUI shell 完成標題列整合——48px drag-region 列含 app
icon/name 固定在左、connection-status pill（圓角 Border + theme brushes）與連接按鈕固定在右
且不與 caption buttons 重疊，ExtendsContentIntoTitleBar 由 code-behind 接線（完整 shell 限定，
Desktop Compatibility Preview 維持自己的 chrome），標題改用 Fluent type ramp
（Issue #446 / PR #450）；verify.ps1 -Clean 在刪除前 fail-closed 驗證 target 必須完全符合
repo-local build root、必須是真實目錄且 target／parent 都不是 reparse point，self-test 涵蓋
mismatch/reparse 拒絕（Issue #448 / PR #452）。皆為 source/tooling evidence。

第四波之後的增量已合併：SceneProfileV1 支援在相符場景切換時保留 referenced IR——schema
新增 ir reference 形狀，hub contracts/engine 驗證並在 engine control 更新中攜帶 referenced
IR identity，contract tests 涵蓋保留行為，evidence 記錄於 scene-ir-reference-v1.json
（Issue #423 / PR #461）；所有 accepted ADR 補上結構化 frontmatter 且 docs-check 強制
檢查 ADR frontmatter 欄位（Issue #453 / PR #459）；run-preview.ps1 在 Start-Process 前
fail-closed 驗證 engine/UI launch target 必須位於 repo .local root 內，拒絕 root 外路徑、
reparse point 與非檔案目標，-SelfTest 新增五個 launch-target 安全案例
（Issue #455 / PR #457）。皆為 user-space/source evidence。

第六波 tooling 增量已合併：winui-a11y-smoke.ps1 在 Start-Process 前 fail-closed 驗證
supplied WinUI accessibility smoke 輸出目錄必須位於 repo .local root 內、為真實目錄且
parent 安全，Hibiki.WinUI.exe 必須位於該輸出目錄之下、為真實檔案且 target／祖先皆非
reparse point；-SelfTest 從 4 個案例擴充至 11 個離線案例，不寫檔、不載入
UI Automation、不啟動程序（Issue #470 / PR #472）。皆為 tooling/source evidence。

第七波 tooling 增量已合併：build-engine-preview.ps1 在 CMake configure 前與 build
命令前雙重驗證固定 .local/engine-preview build root，拒絕 root 外路徑、既有
reparse point 祖先／target 與非目錄 build root，同時保留正常 missing-root 行為；
-SelfTest 從 5 個案例擴充至 7 個離線案例，不呼叫 CMake 也不寫檔（Issue #474 /
PR #477）。皆為 tooling/source evidence。

第八波 docs／tooling 增量已合併：engine-preview-smoke.ps1 在啟動 Engine Preview
執行檔或寫入暫存 IR WAV 前新增離線 path-safety 驗證，wrapper self-test 擴充且不啟動
程序、不呼叫 CMake、不寫 repo 輸出（Issue #482 / PR #483）；README 工具鏈文字對齊
accepted ADR-0005（SDK/WDK >= 10.0.26100 最低基線），並把 V1 gap/milestone 快照更新為
已合併的本地 kernel-mode PortCls adapter .sys build、Inf2Cat packaging 與 self-signed
test-sign evidence，同時明確保留未宣稱的安裝／載入、runtime audio、HLK、Microsoft
signing 與 consumer release 界限（Issue #478 / PR #480）。

第九波 tooling／retention 增量已合併：control-model-engine-smoke.ps1 對固定 Engine
Preview 執行檔、工作目錄、project path 與 .local/control-model-engine-smoke 輸出 root
新增 fail-closed 驗證，build/launch 前驗證並在 dotnet run 前重新驗證 project/output
路徑，保留既有 missing output-root 建立行為，launcher self-test 以離線合成路以離線合成路徑案例
擴充（Issue #489 / PR #490）；另移除切片合併後殘留的 docs/tasks/active/423.md 與
docs/tasks/active/466.md handoff 檔案，依 Issue #341/#346 前例新增
retention-final-v2.json evidence（Issue #485 / PR #487）。

第十波 tooling 增量已合併：docs-check.ps1 新增 markdown 相對連結驗證——target 缺失時
fail-closed，URL、anchor 與 fenced code block 略過，-SelfTest 加入離線連結案例
（Issue #496 / PR #497）；live-audio-session-check.ps1 對固定 build root 與 probe path
新增 fail-closed 驗證，拒絕 root 外路徑、既有 reparse point 祖先／target 與非目錄／
非檔案形狀，保留 missing build root 建立行為，wrapper self-test 擴充且不呼叫 CMake、
不執行 probe、不寫 repo 檔案（Issue #492 / PR #494）。皆為 tooling/source evidence。

第十一波 tooling 增量已合併：live-process-loopback-check.ps1 對固定 build root 與
probe path 新增 fail-closed 驗證，拒絕 root 外路徑、既有 reparse point 祖先／target 與
非目錄／非檔案形狀，保留 missing build root 建立行為，wrapper self-test 擴充且不呼叫
CMake、不執行 probe、不寫 repo 檔案（Issue #500 / PR #502）；docs-check 的 markdown
相對連結閘門交付實作——先前的 PR #497 僅含空 claim commit（已記錄的 race 事件），
現對 tracked markdown 缺失 target fail-closed、略過 URL/anchor/fenced blocks、回報
檢查數量並新增六個離線 self-test 案例（Issue #496 / PR #501）。皆為 tooling/source
evidence。

第十二波 tooling 增量已合併：live-wasapi-handoff-check.ps1 對固定 build root 與
probe path 新增 fail-closed 驗證，拒絕 root 外路徑、既有 reparse point 祖先／target 與
非目錄／非檔案形狀，保留 missing build root 建立行為，wrapper self-test 擴充且不呼叫
CMake、不執行 probe、不寫 repo 檔案（Issue #506 / PR #508）；
live-system-volume-check.ps1 與 live-session-volume-check.ps1 移植既有 live-probe
path-guard 模式，在任何 build、寫入或啟動程序前 fail-closed 驗證 build roots 與
engine executable（root 內、reparse point 掃描與形狀檢查），self-test 加入離線
合成屬性案例（Issue #505 / PR #511）。皆為 tooling/source evidence。

第十三波 tooling 與 UI 增量已合併：live-device-catalog-check.ps1 在建立 build tree
與執行 probe 前，對固定 .local root 新增 fail-closed 驗證（root 內、reparse point
掃描與目錄／檔案形狀檢查），self-test 擴充十一個離線合成路徑案例，且不呼叫 CMake、
不執行 probe、不存取 endpoint 或音訊狀態、不寫 repo 檔案（Issue #512 / PR #513）；
WinUI hero card 重構為自適應雙欄 Grid——Quick Start 面板整合連線按鈕、busy
ProgressRing、一鍵增強動作與狀態 chip，視窗寬度 840px 以下自動收合回單欄，
狀態文字改用膠囊 chip，連線按鈕從標題列移入 hero 面板，32 個互動控制項的
automation names 全數保留（Issue #495 / PR #514）。皆為 tooling/source evidence。

第十四波 tooling 與 UI 增量已合併：build-preview.ps1 在每次 Start-Process 前對
preview smoke 執行檔 fail-closed 驗證，拒絕 .local 外、缺失、目錄形狀與 reparse／
非目錄祖先 target，self-test 從 11 案例擴充至 17 個離線案例，且不建置、不啟動程序、
不寫 repo 或存取機器狀態（Issue #519 / PR #522）；WinUI shell 統一頂層卡片外觀
（CornerRadius 12、一致 24px padding），場景／自訂預設／音量保護標題加入 accent
icons，requested／effective 音量讀值改用 chip 且 SafetyStatusText 維持 polite-live，
IR phase 控制項收進帶說明的 tinted 子面板（mode 文字與 added-delay 以 chip 呈現），
32 個互動控制項與必要 bindings 全數保留（Issue #517 / PR #524）。皆為
tooling/source evidence。

第十五波 tooling 增量已合併：probe-environment.ps1 在建立 .local 或寫入
.local/context.json 前，把 fail-closed path-guard 家族延伸到 root／leaf 驗證
（必須位於 repo .local 樹內、祖先為真實目錄、leaf 與祖先皆非 reparse point，
保留 missing leaf 行為），self-test 在 12 個文件案例外新增 8 個離線合成路徑案例，
不探測機器也不寫 repo；該 PR 同時完成 PR #520 空白 claim commit 提前合併的補救
並依 #497/#507 前例記錄 race 事件（Issue #518 / PR #527）；
build-driver.ps1 在 New-Item 與後續 compiler/linker/package 寫入前，對 object 與
package 輸出目錄 fail-closed 驗證，拒絕 repo .local 外輸出、reparse point
target／祖先與非目錄 target，write-free self-test 新增 10 個離線輸出路徑案例
（Issue #525 / PR #528）。皆為 tooling/source evidence。

第十六波 tooling 與 UI 增量已合併：WinUI 自訂場景表單改為自適應雙欄 Grid
（Scene ID／名稱並排、說明跨欄），視窗寬度 720px 以下自動疊回單欄；「加入自訂
預設」升級為全寬 accent 主按鈕（更高的觸控目標），頁尾提示改為呼應卡片外觀語言的
caption chip，控制模型與引擎行為不變（Issue #529 / PR #534）；
evidence/0000-foundation/probe-environment-path-guard-v1.json 補齊先前空白的
validation 陣列，記錄 Issue #518 path guard 的實際離線驗證命令與結果
（self-test 案例、docs/source/source-only 閘門與 diff check），
滿足 evidence 驗收契約（Issue #533 / PR #535）。皆為 tooling/source evidence。

第十七波 tooling 防護與文件增量已合併：control-model-check.ps1 在任何 dotnet run 前
將 fail-closed path-guard 模式延伸到自身建置輸出，專案檔必須存在，且 BaseOutputPath／
MSBuildProjectExtensionsPath 必須留在 repository root 內，拒絕語彙路徑外 target、
非目錄 target 與 reparse point target／祖先，-SelfTest 新增離線合成屬性案例，並以
evidence/0000-foundation/control-model-check-path-guard-v1.json 記錄驗證
（Issue #536 / PR #537）；verify.ps1 在 New-Item 與 CMake 使用前重新驗證固定的
.local/build target 與 .local parent，拒絕語彙路徑外 target、檔案與 reparse point，
write-free self-test 從 14 案例擴充到 22 案例（Issue #531 / PR #539）；
README 新增 opt-in live-device-catalog-check 與 live-process-loopback-check 探針文件
（匿名彙整輸出、unavailable 時如實記錄），M0 里程碑列補記 Hyper-V VM 隔離載入測試
環境建置中的主張而不誇大完成度，並新增
evidence/0000-foundation/readme-live-probe-surface-v1.json（Issue #540 / PR #545）；
probe-environment.ps1 對既有路徑的屬性檢查改為僅接受 ObjectNotFound 為
「路徑不存在」，leaf 或 parent 的其他檢查錯誤一律 fail-closed，self-test 新增
leaf／parent 兩個合成檢查錯誤案例（Issue #543 / PR #546）；build-engine-preview.ps1
同步採用 Get-Item -ErrorAction Stop 檢查模式，僅 ObjectNotFound 視為缺失，建置根或
父目錄損壞／不可存取時在 CMake 前 fail-closed，self-test 從 7 案例擴充到 9 案例
（Issue #551 / PR #552）；live-system-volume-check.ps1／live-session-volume-check.ps1
與 live-wasapi-handoff-check.ps1 同步採用「僅 ObjectNotFound 視為路徑不存在」的
檢查語意並各自新增離線拒絕案例，分別以 volume-probe-inspection-guard-v1.json 與
wasapi-handoff-inspection-guard-v1.json 記錄（Issue #554 / PR #555、
Issue #561 / PR #562）；engine-preview-smoke.ps1 對 engine executable、工作目錄與
IR 目錄／檔案套用同一檢查語意，損壞或不可存取時在啟動前 fail-closed
（Issue #559 / PR #560）；docs/ai/MULTI_AGENT.md 文件化 TBD pre-claim handoff 流程
（建立 Issue 時 issue／branch 先標 TBD，正式認領前 handoff-check 跳過該草稿）
並附 tbd-preclaim-docs-v1.json（Issue #556 / PR #558）；
WinUI Expert 界面完成卡片化：Expert expander 內容改用與其他介面一致的圓角描邊
卡片，route health／App sessions／route presets／Matrix／DSP graph／VST3 lanes／
calibration 分組進 tinted sub-panel，route health、session、Matrix gain 與 VST3
狀態讀值改用緊湊 caption chips，route preset 欄位雙欄平衡排列，
AutomationProperties 名稱全數保留（Issue #542 / PR #563）；
WinUICompat 啟動崩潰修復：MainWindow.CompatibilityPreview.cs 的七個
Application.Current.Resources 直接索引查找改為 TryGetValue-based fail-soft resolver，
缺失 framework 主題資源不再讓視窗啟動 fail-fast（0xC000027B stowed exception），
formal XAML 路徑與 DesktopCompat 行為不變（Issue #548 / PR #571）。
皆為 tooling/source/UI/docs evidence。

第十八波整合與修復增量已合併：移除 PR #564 誤提交的全部 809 個
.opencode/opencode-loop/** 檔案（loop log、session 排程狀態與 corrupt 快照），
新增根錨定 .opencode/ 的 .gitignore 規則防止復發，不重寫歷史，純衛生切片
（Issue #568 / PR #569）；WinUICompat 啟動崩潰修復的實際交付落地：
CompatibilityPreview code-behind 七個主題資源直接索引查找改為 fail-soft
TryGetValue resolver，缺失 framework 主題資源不再讓視窗啟動 fail-fast
（0xC000027B stowed exception），本機 build-preview -Target WinUICompat -SmokeTest
從必當機轉為通過，evidence winuicompat-launch-fix-v1.json
（Issue #548 / PR #571）；GitHub handoff CI 可靠性改造：PR verify 只驗證該
branch 擁有的 Issue，repository 全域 handoff 健康改由獨立 handoff-audit
workflow（事件＋排程）稽核，AI task 範本改發有效 TBD pre-claim 且不自動加
claimed，SPEC-0004 同步更新，evidence github-handoff-ci-isolation-v1.json
（Issue #565 / PR #566）。皆為 hygiene/UI/docs/CI evidence。

第十九波恢復與防護增量已合併：build-preview.ps1 與 build-driver.ps1 的既有路徑
檢查改為只把 ObjectNotFound 視為不存在，其他檢查錯誤在建置／寫入前 fail-closed，
各自補離線 self-test 案例（Issue #586 / PR #590、Issue #598 / PR #599）；
README「給 AI 協作者」的必跑 gates 清單對齊 AGENTS.md 單一真值，並把工具鏈需求
段落改為指向同一清單（Issue #591 / PR #594）；WinUI 場景卡片系統經兩次空合併後
由 recovery PR 重新落地：新增 Styles/SceneCard.xaml 共用卡片樣式（圓角、描邊、
tinted 背景、hover/pressed/focused 狀態），MainWindow 三組標題統一 Subtitle 層級、
safety badge 加無障礙標籤，32 個 automation bindings 全數保留（Issue #593 /
PR #595 空合併，Issue #603 / PR #605 re-land 完成）；live-device-catalog-check.ps1
同樣補上 fail-closed 路徑檢查（PR #597 空合併，Issue #592 / PR #601 recovery
完成，含 leaf/parent 檢查錯誤拒絕共 13 個離線案例）；VST3 supervisor surface
remove_selected() 經 PR #583 空合併遺失後由 Issue #600 / PR #602 re-land：
native 端在 detached/unselected/edit-session-open 時拒絕、成功時透過 store 移除並
清除狀態，C# Vst3TimelineSurfaceModelV1.RemoveSelected 鏡像與 ViewModel wrapper
同步，contract test 修正 list_ids 字典排序斷言（64 字元長 ID 排在最前）
（vst3-surface-remove-timeline-v1.json）；GitHub Issue intake 改為結構化表單必填：
.github/ISSUE_TEMPLATE/config.yml 停用空白 Issue、保留安全通報聯絡連結與 GitHub
官方空白 Issue 逃生口（Issue #604 / PR #606）。皆為 tooling/UI/VST3 surface/docs
evidence。

 第三十四波整合增量已合併：正式 WinUI VST3 時間軸編輯器新增「清除歷史」與「保存基準」；
清除歷史只重置 undo/redo，保留時間軸資料列、dirty 狀態與開啟中的草稿，空歷史是安全 no-op。
保存基準重用 `SaveSelected()`，把目前內容設為新的未修改基準並清除 dirty 指示；沒有選取
時間軸或草稿開啟時仍拒絕（Issue #786 / PR #791、Issue #794 / PR #798）。DesktopCompat
Preview 新增自訂 Scene 的 ID／名稱／描述欄位與可達新增／移除控制，WinUI Compatibility
Preview 新增含自訂卡片的 Scene picker 和可達清單；兩者只綁既有 ViewModel seam、驗證規則、
32 筆上限、儲存失敗回復與狀態文字，不直接讀寫 catalog 檔或改 IPC/schema（Issue #788 /
PR #796）。VirtualMicDspV1 noise gate 加入 upper-only 2 dB hysteresis：在設定 threshold
關閉、高 2 dB 才重開，中間保持狀態，避免房噪／呼吸聲造成快速 gain pumping；每聲道只增加
一個 bool 與一個 float，RT thread 維持無配置、鎖、等待或 I/O（Issue #787 / PR #795）。
equal-loudness 公式補償加入頻段 phon 邊界：20–4000 Hz 允許 20–90 phon，4 kHz 以上至 12.5 kHz
允許 20–80 phon，0/10 phon 與超出範圍仍 fail-closed；批次中任一現值或參考值無效就整批拒絕，
係數與 contour 資料仍由呼叫端提供（Issue #789 / PR #797）。

installer-check 交易式 uninstall 自檢從函式存在檢查升級為實際行為測試：路徑 traversal 拒絕、
只刪 planned files、移除計數、成功後冪等、locked-file rollback 位元組一致、backup 不殘留，
共 12 案例；後續再把 TEMP/TMP 只導到 self-test process 的 fixture temp root，確認
`.NET GetTempPath()` 解析到隔離目錄、刻意保留的 backup 會被拒絕，成長到 13 案例。這些仍是
離線 temp-fixture source/self-test evidence，不是真實 `-Apply`/`-Uninstall` 或機器狀態變更
（Issue #785 / PR #793、Issue #800 / PR #802）。engine-preview-soak 的離線報表形狀自檢要求
schema version 1、harness identity、completed <= requested 且 iteration result 只有 pass/fail，
共 17 案例，不啟動 engine、不建置、不寫檔（Issue #799 / PR #801）。上述分別是 source、
contract-model、source-gate、compatibility preview build/launch 或離線 self-test evidence；
不宣稱 runtime screen-reader audit、實體音訊量測、driver guest 安裝／載入／HLK 或 Microsoft signing。

第三十八波整合增量已合併：自訂 Scene catalog 的離線同步佇列加入持久化，重新啟動後可依原順序回放待送 Upsert／Remove；佇列檔使用同目錄暫存檔原子替換，保留 64 筆上限與累計捨棄警告，malformed 或 oversized 資料 fail-closed 且不取代已載入卡片；離線編輯只有在卡片與佇列操作都成功保存時才接受，佇列保存失敗會回復卡片變更 (Issue #855 / PR #866)。隨後新增 scene-sync-queue-v1 JSON Schema 讓磁碟格式可獨立驗證 (Issue #878 / PR #879)，並以 if/then/else 直接強制 upsert 需要 name/output_group、remove 必須兩者皆空字串的跨欄位契約 (Issue #884 / PR #885)。公開 schema 契約全面收緊：session route rules 的 $id URI 正規化、catalog 欄位約束加嚴、driver-control 各 message_type 改為精確欄位集合、scene identities 與 route-rule matcher 拒絕空白內容、rule_id 加上 pattern/bounds、custom Scene card display fields 強制字串型別 (Issue #886 / PR #892、Issue #889 / PR #896、Issue #893 / PR #900、Issue #888 / PR #902、Issue #907 / PR #908、Issue #909 / PR #911、Issue #910 / PR #913)。工具與安裝防護同步強化：PowerShell gates 全面啟用 strict-mode guards 並補上漏網腳本；docs-check SelfTest 修復 early-exit、doctor.ps1 strict registry/count 修復；所有 tracked schemas 成為 docs-check 必要入口，且結構與唯一 $id 都會 fail-closed 驗證；installer 的 ReleaseManifest validation 啟用 strict mode 並要求 product_version (Issue #864 / PR #867、Issue #870 / PR #876、Issue #881 / PR #882、Issue #887 / PR #898、Issue #891 / PR #897、Issue #912 / PR #915、Issue #914 / PR #916)。文件與 evidence 完整性一併更新：legacy evidence JSON 補滿 source_commit（73 個目標檔、66 處必要編輯），兩份舊式 evidence 移除從未存在於 Git 的 $schema 引用，README／PROJECT_MAP 的 driver 狀態同步到隔離 guest PnP-start evidence 但仍不宣稱實體播放／WaveRT streaming／HLK／Microsoft signing，SPEC-0008 移除已接線完成的 RT lane latency 待辦描述 (Issue #904 / PR #906、Issue #917 / PR #918、Issue #890 / PR #899、Issue #863 / PR #865)。以上分別是 control-model persistence、source+contract schema、gate/tooling/installer 與 documentation/evidence-metadata correction evidence；不宣稱瀏覽器 runtime automation、實體音訊量測或 driver 實體行為驗收。

第三十七波整合增量已合併：自訂 Scene catalog 離線同步的佇列滿溢現在誠實處理容量損失：超過 64 筆時捨棄最舊操作並累計顯示已捨棄數量；後續離線操作與重播完成訊息不得覆蓋這個警告，重播全部 Ack 才回報「引擎已同步」並保留先前捨棄數，中途失敗保留剩餘操作並誠實顯示降級狀態；control-model-check 新增 65 筆有序操作的回歸案例，SPEC-0014 同步記錄此權威契約 (Issue #840 / PR #844、Issue #838 / PR #847)。driver WaveRT bridge pin 補上與 topology 半邊相同的 analog data range，PortCls 得以完成 physical connection 註冊，解決先前 guest 回報 STATUS_RANGE_NOT_FOUND (Code 10) 的啟動根因；PnP 診斷工具追加 hardware ID 解析與精確目標自檢 (Issue #462 / PR #845)。隨後以官方 Windows 11 Enterprise Evaluation 25H2 x64 媒體（SHA-256 事先驗證）在隔離 Hyper-V Gen2 guest 完成同意重測：test-signed SYS/CAT 在 guest 內簽章有效、pnputil staging 成功、devcon 建立裝置，乾淨重新啟動後單一 MEDIA 裝置 Status OK、ProblemCode 0、HibikiVirtualAudio 服務 Running；匿名證據記錄於 driver-vm-pnp-retest-v2.json (Issue #462 / PR #850)。證據來源修正：scene-offline-sync-v1.json 改用 main 可達的 squash merge commit，另將 18 份 evidence JSON 的孤兒 source_commit 逐一替換為經 git merge-base 驗證祖先關係的合併 commit (Issue #846 / PR #848、Issue #849 / PR #854)。以上分別是 source+contract test/control-model evidence、documentation-only evidence、driver source/build/signability evidence、anonymous isolated-guest PnP-start evidence 與 evidence-metadata correction；不宣稱實體音訊播放、WaveRT streaming 行為、HLK 認證或 Microsoft signing。

第三十六波整合增量已合併：瀏覽器單分頁捕捉在 bridge 啟動晚或暫時掉線時，offscreen 以有界指數退避重試
連線（最多 10 次、上限 15 秒）；bridge 恢復後新封包自動送入，不需重新 Start capture。手動 Stop 或串流
自然結束仍完整 teardown graph 並取消重試；extension-check 追加對 connectBridge、scheduleBridgeRetry、
cancelBridgeRetry 與 bounded attempt 常數的 source-boundary 斷言（Issue #826 / PR #834）。自訂 Scene catalog
加入離線操作的有界重播：未連線時新增／移除仍立即作用於 UI mirror 與本機檔案，同時把操作記入最多 64 筆的
FIFO 佇列；超過容量時捨棄最舊並顯示壓力訊息。重新連線成功後依原順序補送 Upsert／Remove 到引擎，全部 Ack
才回報「引擎已同步」；中途失敗保留剩餘操作並誠實顯示降級狀態，待下次連線再補送。重播只使用既有
SceneCatalogCommandV1 wire format 與 Ack 語意，不改變引擎驗證或 catalog 容量契約；control-model-check 的
契約案例涵蓋離線排隊→連線 flush、部分失敗保留與 connected add 不殘留佇列 (Issue #823 / PR #832)。
VST3 host README 能力圖刷新：明確標示 bounded 參數自動化（最多 16 ID × 5 sample-accurate points）、
timeline 持久化／編輯交易／undo-redo history 及 C# Compatibility Preview 編輯面已在 source baseline scope；
補上 LatencyGraphCommitV1/LaneLatencyBankV1 的 graph-side commit 契約摘要——lane token/revision 驗證、
Prepare 期配置 scratch/ring、Commit 期原子 swap，RT mixer 只讀 bank 不在 callback 配置。仍明確不宣稱
third-party certification、crash-dump redaction pipeline、multi-bus worker process 或實體端到端延遲驗收
(Issue #836 / PR #837)。上述分別為 extension source/gate evidence、source+contract test/control-model evidence
與 documentation-only evidence；不宣稱瀏覽器 runtime automation、實體音訊量測、driver guest 安裝／載入／HLK
或 Microsoft signing。
第三十五波整合增量已合併：Desktop Compatibility Preview 新增完整的每-App 路由規則編輯面，
WinUI Compatibility Preview 以可達 TextBox／NumberBox／CheckBox／ComboBox 提供同一流程；使用者
可管理有界規則清單中的 ID、App ID、顯示名稱、Lane、output group、priority、makeup gain、
enabled 與 gain owner，並執行新增／更新、逐筆移除和清除全部。操作重用既有 ViewModel 驗證、
持久化 rollback、狀態文字與 IPC command seam，不改 EasyControlViewModel 或 engine contract
(Issue #804 / PR #808)。SPEC-0003 把 BasicNoiseSuppressorV1 與 VirtualMicDspV1 的 noise gate
hysteresis 記錄為權威契約：envelope 在設定 threshold 關閉，須回升到 threshold +2 dB 才重新
開啟，中間保持狀態，避免臨界附近快速開關 (Issue #810 / PR #812)。自訂 Scene catalog 的控制平面
同步加入 SceneCatalogCommandV1（IPC type 20）：固定 3260-byte payload 支援 Upsert／Remove／Clear，
欄位邊界嚴格驗證、zero padding、bounded UTF-8 字串、finite double 與容量限制任一失敗即整包拒收；
engine control worker 擁有 mutable catalog，收到 Upsert 後重建完整 SceneDefinitionV1 並通過既有
Scene／Graph／ISO policy 驗證才原子替換。C# 控制模型在本機新增或刪除自訂卡且已連線時推送同步；
contract tests 涵蓋 encode→decode 往返、engine apply、SceneApply resolution、remove 與破損 payload
拒收 (Issue #768 / PR #814)。Wave34 快照 evidence 的未來時間 placeholder 已修正為實際 gate start/
finish，補上 doctor、source-only-CI 與 verify 命令紀錄，並留下 post-merge correction 紀錄
(Issue #809 / PR #811)。以上分別為 compatibility preview source/build/launch、spec/documentation、
source+contract test/control-model/engine smoke 與 documentation/evidence correction evidence；
不宣稱 runtime screen-reader audit、實體音訊量測、真實 installer 執行、driver guest 安裝／載入／HLK
或 Microsoft signing。
第三十三波整合增量已合併：官方 bootstrapper 新增經 manifest 與 SHA-256 驗證的交易式
user-space payload staging，失敗會回復既有安裝且不碰 `%LocalAppData%/Hibiki DSP` 使用者
資料；9 個離線功能案例涵蓋有效 staging、路徑逃逸拒絕、hash mismatch、rollback 與 backup
清理。另新增只移除 manifest 所列 payload、失敗時回復、保留使用者資料的 uninstall 路徑；
後者目前仍是 source/boundary evidence，尚未執行真實 `-Apply`／`-Uninstall`，交易式 uninstall
功能自檢由後續 Issue #785 補強（Issue #745 / PR #759、PR #770；Issue #766 / PR #777）。

driver host 工具統一 Inf2Cat 探測順序（明確 `WDK_BIN`、Windows Kits tree、PATH），本機已建置
package 可在不手動設定環境變數時重跑 signability；build-driver 使用唯一 source/object plan，
`guids.cpp` 只編譯一次，compiler 與 linker warning 都以 `/WX` fail-closed（Issue #764 /
PR #769、Issue #774 / PR #776）。PortCls adapter 原本把四組 endpoint pair 數量誤當成
可註冊 filter object 上限，`PcAddAdapterDevice` 只預留 4 個 slot 卻需註冊 8 個
Topology/WaveRT filter；`MaxObjects` 已修為 8，本機 WDK build、Inf2Cat 與 CI 通過
（Issue #462 / PR #782）。另新增只讀匿名 PnP 診斷工具，只鎖定
`ROOT\HIBIKIDSP`／`HibikiVirtualAudio`，記錄 typed problem/service/driver 欄位及有界、遮蔽後的
SetupAPI 摘要到 `.local/`；AST 自檢拒絕安裝、啟停、移除裝置或修改開機設定的命令
（Issue #781 / PR #783）。上述皆為 host-side source/build/self-test 或 unavailable-path evidence，
不代表 driver 已在 guest 安裝、載入、PnP start、出聲、通過 HLK 或取得 Microsoft signing；
Issue #462 因重建 package 的隔離 VM PnP start 重測尚未完成而保持開啟。

瀏覽器 popup 從隱藏回到可見時，會透過既有 service-worker query 重新取得實際捕捉狀態，且
不覆蓋先前需保留到下一次使用者操作的錯誤（Issue #762 / PR #773）。DesktopCompat 與
Compatibility Preview 都改用 control model 組合出的完整 route-health accessible summary，
不再只顯示名稱與短狀態（Issue #765 / PR #771）。正式 WinUI 的本機自訂 Scene 卡加入具名的
移除操作，儲存失敗會回復卡片與選取；實體輸出 picker 加入重新掃描操作，只送出既有 bounded
`DeviceCatalogRequest`，未連線、逾時、錯誤或 stale snapshot 都保留上一份清單
（Issue #767 / PR #775、Issue #779 / PR #784）。這些是 source、contract-model、source-gate
或非目標 preview evidence；未執行正式 runtime UIA／螢幕閱讀器或瀏覽器自動化，也不代表
實體音訊已送達。

Basic noise gate 在原關閉 threshold 上加入 2 dB reopening hysteresis；訊號在 threshold 附近
擺動時不再每個區塊快速重開，既有 attack/release 語意維持不變。實作保持 `noexcept`、每聲道
固定狀態且 RT thread 無配置、鎖、等待或 I/O，contract regression 與完整 verify 通過
（Issue #778 / PR #780）。這是 user-space DSP source/contract evidence，不是實體音訊量測。

第三十二波整合增量已合併：SPEC-0009 補上 popup 無障礙政策的權威文件條目，記錄 PR #744
引入並由 PR #750 擴充的 aria-label／aria-labelledby 強制檢查與 fail-closed 行為
（Issue #753 / PR #755）。winui-shell-check 新增兩個回歸防護：DesktopCompat Preview 的
互動控制項必須在 Program.cs 宣告非空 AccessibleName，正式 shell MainWindow.xaml 必須保留
IR WAV 載入按鈕及其 AutomationProperties.Name 和 OnPrepareIrClick handler，缺漏會讓 gate
fail-closed（Issue #752 / PR #757）。正式 shell route-health 卡片將控制模型的完整無障礙
摘要投射給輔助技術而非僅視覺片段，Expert 狀態變更改用 polite live region 公告
（Issue #756 / PR #758）。前者為文件 evidence，後者分別為 UI source-gate 與 source-only
accessibility projection evidence。不宣稱正式 XAML/accessibility runtime audit、螢幕閱讀器
runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。

第三十一波整合增量已合併：Desktop Compatibility Preview 的 14 個互動控制項全部補上非空
AccessibleName，涵蓋 session 選擇器、音量滑桿、路由欄位、場景與 IR 控制以及本機建立的
output-group 下拉和連線按鈕；名稱沿用可見的繁中介面用語，Release build 與啟動 smoke 通過
（Issue #747 / PR #749）。extension popup gate 追加 aria-labelledby 目標解析檢查：引用 ID
必須存在於 popup markup 且解析到非空文字，壞引用即使同時提供 aria-label 也會 fail-closed，
離線自檢覆蓋有效引用、缺失目標與空文字目標（Issue #748 / PR #750）。前者為 compat preview
source/build/launch evidence，後者為 extension source/policy evidence。不宣稱正式
XAML/accessibility runtime audit、螢幕閱讀器 runtime automation、實體音訊、driver
安裝/載入/HLK 或 Microsoft signing。

第三十波整合增量已合併：瀏覽器單分頁捕捉的 Start／Stop 失敗訊息保留到下一次使用者操作，
不會被立即的狀態重繪蓋成 Idle；extension gate 同步要求 status.dataset.error 邊界並在
自檢中驗證（Issue #724 / PR #727）。Compatibility Preview 開放與正式 shell 相同的
「刪除目前時間軸」動作，重用 fail-closed ViewModel handler 並帶無障礙名稱（Issue #723 /
PR #729）。offscreen natural-end release gate 追加 handler 定義移除時 fail-closed 的
自檢，避免 ended listener 靜默指向未定義函式（Issue #733 / PR #735）。winui-shell-check
掃描 Compatibility Preview C# 控制建構的 AutomationProperties.Name，缺漏或空值會讓
檢查失敗（Issue #732 / PR #734）。分別為 extension source/policy evidence、compat preview
source/build/launch evidence 與 UI source-gate evidence。不宣稱正式 XAML/accessibility
runtime audit、瀏覽器 runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。

第二十九波整合增量已合併：Compatibility Preview 的 MRT 紀錄修正為 fail-soft 邊界——
WinUICompat 建置不啟用 Core MRT 資源工具，主題資源走既有 TryGetValue 回退路徑，
預覽以無樣式但可啟動的方式執行；審計文字同時澄清 VS2026 Appx packaging tasks 存在於
本機而 dotnet CLI build 不會載入它們（Issue #703 / PR #709、Issue #720 / PR #721）。
瀏覽器單分頁捕捉的 popup Start／Stop handler 攔截訊息通道錯誤，顯示實際錯誤並重新
查詢真實捕捉狀態後才恢復控制項（Issue #702 / PR #704）；後續修正讓失敗文字在背景狀態
刷新時保留，直到使用者下一次操作才更新（Issue #724 / PR #727）。extension gate 強制
popup 檢查 response.ok、如實回報錯誤，並要求以 error 標記保護此訊息；自檢在缺少任一
邊界時 fail-closed（Issue #715 / PR #719、Issue #727）。
offscreen 在來源串流自然結束時釋放捕捉 graph、通知 service worker 並自動關閉 document，
不需要使用者手動停止（Issue #706 / PR #707、Issue #714 / PR #716）。SPEC-0009 記錄失敗回應與
stream-ended 邊界，README 補上捕捉生命週期與 bridge 狀態說明；compat preview smoke 改為從
乾淨輸出目錄執行，避免殘留 binary 造成假通過（Issue #710 / PR #711、Issue #712 / PR #718、
Issue #713 / PR #717）。extension/UI 項目分別為 extension source/policy evidence 與 compat
preview source/build/launch evidence。不宣稱正式 XAML/accessibility、實體音訊、driver
安裝/載入/HLK 或 Microsoft signing。
第二十八波整合增量已合併：瀏覽器單分頁捕捉的 offscreen start／stop listener 保留非同步
回應通道，成功啟動不再因回應通道提早關閉而被誤報為失敗（Issue #681 / PR #692）。
Compatibility Preview 的 WinUICompat target 恢復編譯與啟動 smoke，隨後清除暫時 dead-code
TextBox seam 且無運行行為變化（Issue #687 / PR #694、Issue #697 / PR #699）。每個註冊 output sink 現在擁有獨立 TruePeakLimiterV1 狀態，一個輸出
群的尖峰不會壓低另一群後續安靜區塊；graph commit 仍重置所有 limiter（Issue #683 / PR #695）。
Engine Preview 新增有界 opt-in soak harness，離線 SelfTest 覆蓋參數邊界、IPC frame、聚合結果
與清理決策；預設三循環 smoke 以 Hello/Ack 加 Main volume 往返驗證並產生匿名報告（Issue #672 /
PR #685）。extension/UI/DSP 項目分別為 source、policy/build/contract evidence；soak 為本機
user-space process evidence。不宣稱正式 XAML/accessibility、實體音訊、driver 安裝/載入/HLK 或
Microsoft signing。
第二十七波整合增量已合併：TruePeakLimiterV1 在 graph commit 時重置回 unity gain，
前一個 graph 累積的恢復衰減不會延續到新 graph 的安靜段落；新 graph 中超過上限的
峰值仍然立即衰減（Issue #678 / PR #678）。瀏覽器單分頁捕捉的 start/stop 回應改為
反映真實成敗：成功啟動不再被誤報為失敗，失敗會帶回實際錯誤，policy gate 同步
涵蓋回應邊界（Issue #681 / PR #684）。皆為 user-space source、contract test、
extension source 與 policy gate evidence；不宣稱實體音訊、driver 安裝/載入/HLK 或
Microsoft signing。
第二十六波整合增量已合併：WinUI Expert shell 開放本機 VST3 時間軸編輯器，
支援時間軸選取、草稿開始／提交／捨棄、復原／重做與事件增刪／數值編輯，
Compatibility Preview 補上相同的選取 seam（Issue #667 / PR #669、Issue #673 /
PR #677）。瀏覽器單分頁捕捉新增使用者控制的 Stop，popup 開啟時反映真實捕捉
狀態，啟動失敗如實回報錯誤而非假裝成功（Issue #671 / PR #676）。皆為
control-model／shell source、extension source、policy check 與 SPEC-0009／SPEC-0010
evidence；不宣稱實體音訊或 driver 能力。
第二十五波整合增量已合併：IpcNamedPipeServerV1 在兩次連線之間的空檔收到 stop() 時，
會重複取消當下註冊的 server handle I/O 直到 worker 結束，Engine Preview 關閉不再
固定等待完整 idle timeout；connected-idle prompt-stop 行為維持不變，IPC framing 與
ownership 語意不變（Issue #655 / PR #661）。TruePeakLimiterV1 恢復上限從每區塊 +6 dB
改為以經過音訊時間（dB domain，約 +6 dB 每毫秒）計算並跟隨引擎取樣率，20 個 48-frame
區塊與單一 960-frame 區塊在相同時間跨度內恢復曲線一致，需要壓低時仍立即反應
（Issue #659 / PR #666）。皆為 user-space source／contract test／SPEC-0001、SPEC-0002
evidence；不宣稱實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
第二十四波整合增量已合併：driver adapter 配置改為 ExAllocatePool3，paged／non-paged pool 類別與 'ibiH' tag 不變，建置定義升至 NTDDI_WIN10_VB 以取得宣告；GetHWLatency 把每 buffer 延遲估計寫入 HWLatency->CodecDelay，PortCls 讀到有效硬體延遲而不是被丟棄（Issue #652 / PR #660）。NODE_MUTE 名稱改指向 WDK 既有 KSAUDFNAME_MASTER_MUTE，移除未用的 null fallback，讓系統屬性頁與音效工具顯示標準「靜音」名稱（Issue #662 / PR #663）。皆為 source／local WDK build／Inf2Cat evidence；不宣稱 guest 安裝／載入／PnP start／實體音訊／HLK／Microsoft signing。

第二十三波整合增量已合併：TruePeakLimiterV1 記錄上一區塊套用增益，恢復上限固定為 2x/區塊（約 +6 dB），attenuation 仍立即套用；reset() 一併重置 recovery 狀態，並新增連續區塊回歸測試確保安靜後不會瞬間跳回（Issue #647 / PR #648）。屬 user-space DSP 契約證據，不宣稱 ITU/BS.1770 或 certified true-peak conformance、實體音訊、driver 安裝/載入/HLK/Microsoft signing。

第二十二波整合增量已合併：BasicNoiseSuppressorV1 修正開／關增益方向——attack_ms 控制開啟速度、release_ms 控制關閉速度——並新增 closed-to-open 回歸測試（Issue #636 / PR #640）；handoff 稽核新增「多個 active Issue 共用同一 branch」fail-closed 自檢（Issue #643 / PR #644）；IpcNamedPipeServerV1 在 worker 啟動前註冊同步初始 pipe handle，stop() 可立即取消等待中的 ConnectNamedPipe 而非等 idle timeout（Issue #637 / PR #645）；driver 端每個 endpoint 成對註冊 PortCls Topology 與 WaveRT filter、INF 介面一致並接上 bridge 實體連線，本機 WDK 建置與 Inf2Cat 通過（Issue #462 / PR #638）。DSP/IPC/handoff 項目為 user-space/source evidence；WaveRT 配對為本機建置證據，不宣稱 guest 安裝/載入/PnP start/實體音訊/HLK/Microsoft signing。

第二十一波整合增量已合併：Engine Preview 的 canonical 控制管線加入單一擁有權 fail-closed，無法取得管線時以離開碼 3 退出（Issue #628 / PR #631）；PortCls adapter start path 新增分階段 DbgPrintEx 診斷（Issue #633 / PR #635）；這僅為 control/start-path 診斷與 user-space 證據，不宣稱 driver 已安裝/載入/HLK/Microsoft signing。

第二十波指令面與證據增量已合併：AGENTS.md 改寫為三層規則索引（硬性限制／產品與流程
預設／core+conditional 驗證門檻），driver 安裝、載入、HLK 與 Microsoft 簽章明確歸屬
release 階段，README gates 清單同步拆分核心與條件式兩層，AI_HANDOFF 從長文壓縮為短入口、
MULTI_AGENT 認領流程改為「指派後即開工、首個可審閱 commit 開 draft PR」並把 directory
lane 表降級為路由提示，CONTRIBUTING 移除已廢除的 baseline counter 檔指引，退役的
task-handoff schema 以 SPEC-0004 記錄並由 CHANGE_CONTRACT.yml 的 validation_tiers 取代，
evidence ai-instruction-audit-v2.json（Issue #610 / PR #616）；README live probe 文件對齊：
live-wasapi-handoff-check.ps1 與 live-audio-session-check.ps1 兩個 opt-in 探針從公開入口頁
可被發現（匿名彙整輸出與 unavailable 如實記錄語意），docs-check -SelfTest 範例補進 gates
說明，evidence readme-live-probe-docs-v1.json（Issue #613 / PR #615）；隔離 Hyper-V VM
WaveRT 載入測試 evidence：official Win11 media 建置 Gen2 guest、testsigning 開啟、Root
cert 匯入成功但 TrustedPublisher store 缺失導致 Driver Store 以 0xE0000242 拒絕 catalog，
guest 內 Import-Certificate 恢復後 pnputil 成功 staging 為 oem2.inf，devcon 建立
Root\HibikiDSP 且服務安裝但 PnP start 可重現失敗（CM_PROB_FAILED_START /
NTSTATUS 0xC000000D，含 disable/enable 重試），明確不主張實體音訊、WaveRT streaming、HLK
或 Microsoft signing，evidence driver-vm-load-test-v1.json（Issue #462 / PR #614）。皆為
docs/evidence 增量。

目前驗證摘要：`verify.ps1` 的 10 個 CTest（hibiki_contract_tests、hibiki_driver_stream_tests、hibiki_lane_latency_tests、hibiki_noise_suppressor_tests、hibiki_output_crossfade_tests、hibiki_peq_dsp_tests、hibiki_asio_transport_selftest、hibiki_tab_bridge_selftest、hibiki_asio_transport_consumer_tests、hibiki_true_peak_limiter_tests）通過（含 #1474 新增的 suppressor fail-closed 測試、#1647 新增的 lane latency 行為測試、#1667 新增的 output crossfade 行為測試、#1660 新增的 PEQ DSP 行為測試，以及 #1663 新增的 ASIO transport consumer 行為測試、#1684 新增的 true peak limiter 行為測試）；`docs-check.ps1` 的 86 個必要入口與
24 份 Spec 通過；`source-policy.ps1` 掃描 tracked paths 且無 blocked
binary/secret；volatile 計數（tracked paths、repository JSON）由 docs-check 即時量測；
`extension-check.ps1`、`installer-check.ps1`、`control-model-check.ps1`、`winui-shell-check.ps1` 與
`distribution-check.ps1` 與 `driver-source-check.ps1` 通過；repository JSON 檔案均可解析。C++/C# DeviceSwitch
288-byte payload、catalog sequence、handler fail-closed、WinUI send-failure rollback、DeviceCatalogSnapshot、ControlStatusSnapshot
wire/atomic replace、catalog-to-wire publisher、Windows worker unbound/coordinator rollback、
DeviceCatalogRequest provider response、連線後自動刷新裝置清單、ControlPlaneHost loopback
queue handoff、live 14-endpoint runtime pipe probe（含 default endpoint volume read）、
WindowsVolumeLink 外部／自家回授／stale generation contract、ControlStatusSnapshot wire/store/handler/
ViewModel atomic apply 與 WDK basic-support source gate 亦通過。以本機 pinned ASIO SDK
另行執行的 optional CMake target `hibiki_asio_native` unsigned build 亦通過；該輸出只在
`.local/`，未提交或發布。以本機 pinned VST3 SDK 另行執行的 optional target
`hibiki_vst3_sdk_catalog` 與 `hibiki_vst3_sdk_worker`（含 bounded one-main-bus processor、
parameter frame 與 `IParameterChanges` bridge）unsigned build 亦通過；輸出同樣
只在 `.local/`。ADR-0005 改採 >= 10.0.26100 最低基線後，本機 `doctor.ps1 -CheckOnly` 已通過；
本機結果仍不把 driver 安裝／載入、HLK、簽章、真實 endpoint 或第三方 plugin certification
結果誇大為已驗收。C++
與 C# grouped-volume payload round-trip、legacy payload compatibility、selected group resolver、
VST3 timeline editor 交易、C# CalibrationModel 資料契約／PEQ 編譯器
及 custom Scene card mirror 的 JSON save/load、atomic replace、malformed rollback 亦已通過本機
contract/control-model checks。
本次 CalibrationModel C# control model 與 bounded PEQ compiler 的 source commit 是 `5ef674f`；
本次 Vst3TimelineEditor supervisor-side parameter timeline editing transaction 的 source commit 是 `9f5f02e`；
本次 live SessionRouteRuleCommand upsert/remove readback transaction probe 的 source commit 是 `d1ad93e`；
本次 live Engine Preview system volume write-through IPC probe 的 source commit 是 `1300e4cb5b2a6388f51049ebd98c945dcfeaf214`；
本次 Expert per-App volume controls 的 source commit 是 `ebb80a3`；
本次 Session command worker queue 的 source commit 是 `6f9d6b1`；
本次 App route selection controls 的 source commit 是 `d4862d9`；
COM worker-thread guard 的 source commit 是 `cbc860e`；
SessionRouteCommand graph boundary 的 source commit 是 `662abbb`；
本次 session catalog volume availability projection 的 source commit 是 `b1538b1`；
SessionVolumeCommand handle boundary 的 source commit 是 `6c4a8b7`；
本次 WinUI App session catalog projection 的 source commit 是 `66f6298`；
ephemeral App session catalog additions 的 source commit 是 `68cf466`；
本次 control-status-snapshot additions 的 source commit 是 `e97fb90`；
Engine Preview opt-in Windows system-volume link、safe default launcher 與 status-only smoke 的 source commit 是 `6a04764`；
session-route health 接入與避免同端點重綁的最新 source commit 是 `5f8dbcb`；
volume broker unchanged-endpoint result 修正的最新 source commit 是 `ca8ea40`；
volume node 與 session-route 獨立重綁的最新 source commit 是 `ef4af32`；
control-model route-health／volume-safety additions 的 source commit 是 `7d43e67`，
對應 handoff/evidence 更新 commit 是 `e13cfd8`；最後一次 live session evidence 更新是
`2ba5299`；Engine Preview opt-in session-routing vertical slice、Desktop Compatibility Expert
controls 與 smoke evidence 的 source commit 是 `d668982`。
