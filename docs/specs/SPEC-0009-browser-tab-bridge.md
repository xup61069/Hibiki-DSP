---
id: SPEC-0009
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["extensions/**", "tools/extension-check.ps1", "apps/engine-preview/engine_preview.cpp", "tools/run-preview.ps1", "tools/engine-preview-smoke.ps1"]
---

# SPEC-0009：瀏覽器單分頁音訊 bridge

瀏覽器只能在使用者點擊 popup 後呼叫 `tabCapture`。MV3 offscreen document 維持
stream，AudioWorklet 將音訊轉成 `HIBT` packet；Windows process routing 不得宣稱
能靜默取得 Chrome/Edge 單一分頁。

## User-controlled capture lifecycle

popup 必須提供明確的 Stop 控制項：點擊後透過 service worker 通知 offscreen 停止
stream（停止所有 track、斷開 graph）並關閉 offscreen document。popup 開啟時必須
查詢真實狀態（idle 或 capturing），不得假設 idle。start 失敗必須回報錯誤，不得
宣稱「Capture started」。`tools/extension-check.ps1` 驗證這些 stop 與 state 邊界，
self-test 涵蓋缺少 handler 的情況。service worker 中繼與 offscreen 的 onMessage listener
必須對非同步回應路徑 `return true`，讓 Chrome 保持訊息通道開啟直到 `sendResponse`
完成；`tools/extension-check.ps1` 對兩層都強制此邊界，self-test 涵蓋缺少
`return true` 或回傳值不是 `return true;` 陳述式的情況。

messaging 失敗（例如 popup 與 service worker 的訊息通道拋錯）不得讓 popup 卡在
busy 狀態：Start／Stop handler 必須攔截錯誤、顯示實際錯誤文字，並重新查詢真實
capture 狀態後才恢復控制項。被擷取分頁關閉或導航導致 source track 自然結束時，
offscreen 必須立即釋放整個 capture graph（stream、AudioContext、WebSocket）並廣播
新的 capture-state，popup 不得繼續顯示「Capturing」。新的 Start 必須先完整 teardown
先前 capture graph；track ended callback 必須綁定其來源 stream，且只可 teardown 仍是 active
的 stream，不能讓舊 stream 的晚到 callback 結束 replacement capture。packetizer callback 也必須
綁定建立它的 node，且只可在該 node 仍是 active graph 時送包或更新計數，不能把舊佇列封包送進
replacement bridge。手動 Stop 仍是唯一主動
`track.stop()` 擁有者，ended 監聽器只負責自然結束路徑，不得重複 teardown。
service worker 必須把 Start、Stop 與已驗證的自然釋放關閉放在同一個 lifecycle queue；自然釋放
在關閉 offscreen document 前必須查詢真實 capture 狀態，不能讓舊的 close 路徑關閉已開始的
replacement capture。
popup 重新可見時必須再次查詢真實 capture 狀態；此 visibility refresh 只更新狀態，
不得清除既有錯誤訊息。`tools/extension-check.ps1` 驗證 visible-only refresh 邊界，
self-test 涵蓋缺少 listener、visible check 或 `refreshState` 呼叫的情況。

## HIBT packet

固定 little-endian header：`magic='HIBT'`、`version=1`、`channels`、`frames`、
`sampleRate`，後接 interleaved Float32。receiver 必須拒絕未知版本、channels>8、
不支援 sample rate、payload overflow 或 header/實際長度不一致；任何拒絕不得讓
瀏覽器本地回放中斷。

## Native bridge boundary

The repository now includes `hibiki_tab_bridge_contract`, a portable decoder
that validates all fields and rejects non-finite samples before exposing a
non-owning packet view.

目前 extension 只連 `ws://127.0.0.1:17842/v1/tab` 的 loopback receiver；bridge 未啟動
時丟棄送出 packet；capture 進行中 offscreen 會以 bounded exponential backoff（最多
10 次、上限 15 秒）重試連線，bridge 恢復後新 packet 即送入，不需重新 Start capture。
連線建立失敗或 `WebSocket.onerror` 觸發時走同一個有界重試計時器。只有目前擁有的
WebSocket 才可改變 bridge state 或排程重試；已被 Stop／新 Start 取代的 socket 即使稍後
觸發 open/close callback 也必須忽略。offscreen 透過現有
capture-state / get-capture-state path 回報 bounded reconnect state：connected、waiting、
retrying、exhausted 或 idle；popup 在 capture 中顯示等待重試、正在重試或已停止重試，
而不是只顯示靜態未連線。重試用完時 tab playback 繼續且 packet 繼續丟棄；手動 Stop
或串流自然結束仍會完整 teardown graph 並取消重試。receiver 已限制 localhost、WebSocket frame 大小、mask、FIN-set
control frame、canonical payload length、ping/close 與 decoder 驗證；Close payload 無論收或送都必須為空，
或是合法 status 加上 strict UTF-8 reason，否則在讀取／寫入 payload 前拒絕。fragmented control frame 或非 canonical
payload length 會在讀取 mask 或 payload 前拒絕。offscreen 追蹤累計送出的 packet 總數 `totalPackets`（每次 Start capture
歸零）與既有 `droppedPackets`；兩個匿名計數器都會放進 capture-state / get-capture-state
path，popup 的「複製診斷資訊」快照也會包含這兩行，讓使用者貼進 issue 的快照能直接顯示
packet 是否有在送出或全部被丟棄。此計數是 user-space 匿名健康資訊，不宣稱 engine 接收率
或音訊品質。
offscreen 另追蹤每次 Start capture 重置的匿名時間戳：capture 開始的 `captureStartedAtMs`
與最後一次收到 packetizer packet 的 `lastPacketActivityAtMs`（送出或丟棄都算活動）。這些欄位
隨 capture-state 傳給 popup；popup 顯示擷取已持續多久與最近是否仍有音訊活動，並在尚未收到
packet 時誠實顯示「尚未收到音訊封包」，不偽造時間。停止或自然結束後不再回報 active timing。
「複製診斷資訊」快照也會包含兩個時間戳與當下的 elapsed／age 值。此為 user-space 匿名健康資訊，
不宣稱 engine 接收率或音訊品質。`TabCaptureQueueV1` 將 validated packet 複製到四格固定 SPSC ring，
控制執行緒可用 `enqueue_tab_capture_packet_v1` 作 callback，RT lane 再以 caller-owned
buffer pop；pop contract 必須以 interleaved sample capacity 驗證 `frames * channels`，
容量不足時 fail-closed 且不 consume queued packet。滿載會回報 dropped blocks，不阻塞
WebSocket。`process_tab_capture_lane_v1` 與 `process_tab_capture_lane_to_wasapi_v1` 的
input capacity 以 interleaved samples 表示，output capacity 仍以 frames 表示。
`process_tab_capture_lane_v1`
會把一個 queue block 送入 `AudioEngineModel::process_lane_block`，因此沿用同一份
immutable graph、lane mapping 與 Group Master；adapter 不配置、不等待，也不擁有音訊
buffer。`process_tab_capture_lane_to_wasapi_v1` 使用相同 effects 與 lane validation，完成
graph／Group Master／limiter 後只提交一次到 WASAPI handoff；sink 未綁定時回傳失敗且不
宣稱已播放。降噪模型 provenance、權限提示與斷線重連 policy 仍必須在獨立 source component
完成後，才能宣稱「單分頁掛降噪」。

Engine Preview 在建立 listener 前把 tab queue 綁定到目前 WASAPI sink 的 sample rate。
HIBT decoder 與 portable queue 仍接受 44100、48000、96000、192000 Hz 的合法封包；但
若封包來源 rate 與 host sink 不同，queue 會在 ingress fail-closed 丟棄，不把來源 frame
數當成 sink frame 數送進 graph／WASAPI。這個 v1 host boundary 暫不偷偷重採樣，並以匿名
mismatch counter 與 Pending route detail 說明狀態；未來 bounded resampler 必須另立契約。

Engine Preview 提供 opt-in 的 `--enable-tab-bridge` 模式作為上述元件的宿主：
該旗標必須同時要求 `--enable-wasapi-output`，缺少 sink 時直接拒絕啟動。listener 只綁定
`127.0.0.1:17842`，控制執行緒 callback 僅 enqueue 到固定 SPSC queue；tab 音訊走專屬
lane 經 immutable graph 處理後送入 WASAPI handoff。`--enable-tab-bridge` 不得與
`--enable-test-tone` 或 `--enable-process-delivery` 併用；偵測到互斥來源時，host 必須在
建立 listener 或音訊路徑前 fail-closed 拒絕啟動。route-health 的 browser-tab 項目反映
receiver 實際狀態（disabled、listening 或 waiting），沒有使用者 capture 時不得顯示 Ready。

Engine Preview 另提供 opt-in 的 `--enable-tab-noise-suppressor` 旗標：啟用時對 tab bridge
lane 套用 `BasicNoiseSuppressorV1`（固定 high-pass + downward-gate）。此旗標必須與
`--enable-tab-bridge` 併用；單獨出現時 host fail-closed 拒絕啟動。抑制器以實際 sink
sample rate、stereo（2 聲道）初始化；非 stereo 區塊會被 fail-closed 丟棄，route-health
仍只代表 listener 狀態，不因此宣稱降噪品質或 AI/ML denoising。設定失敗時不得把
未設定的 effect 接到 lane。此功能是基本降噪，不是 ML/spectral denoising；不得宣稱
AI denoising 或 RNNoise。未加此旗標時，預設行為與先前完全相同。

若 `BasicNoiseSuppressorV1` 設定失敗，Engine Preview 必須在 listener bind 前停止 tab bridge，
並以 `tab noise suppressor setup failed; tab bridge disabled.` 的 user-space route detail
回報 `Unavailable`／需要使用者處理；不得以普通的「等待 browser capture」狀態接受未經抑制的
封包。這只證明啟動 fail-closed 與狀態投影，不代表降噪品質或 physical audio delivery。

Engine Preview 另提供 opt-in 的 `--enable-tab-bass-correction` 旗標：啟用時必須同時
傳入 `--enable-tab-bridge --enable-wasapi-output`，否則 host 在建立 listener 或音訊
路徑前 fail-closed 拒絕啟動。此旗標把只含 bounded bass correction 的
`ProgramAwareLevelPolicyV1` 掛在 engine-owned `main` output attachment；它不在 tab
bridge adapter 外另建 controller，因此同一個已提交的 RT controller 會同時產生低頻
修正與 control-plane telemetry。`EngineControlWorkerV1` 在收到有效、非靜音 telemetry
後發布 source=2 `EqVisualSnapshot`，正式 EQ UI 取得確認 frame 後以既有 transition
動畫顯示低頻曲線與 dB 狀態。此 detector 是 generic low-frequency energy proxy，不讀取
YouTube URL、DOM 或內容 metadata；使用者仍須在 extension popup 明確點擊開始擷取。
`tools/run-preview.ps1` 與 `tools/engine-preview-smoke.ps1` 只負責傳遞並驗證這個
explicit opt-in。未加此旗標時，tab bridge 不套用自適應低頻修正，行為維持原狀。

## Extension security and CSP policy

MV3 extension source gate (`tools/extension-check.ps1`) 強制驗證最小權限與 CSP 不漂移：
1. 權限僅限 `activeTab`、`tabCapture` 與 `offscreen`，拒絕任何額外或廣域權限。
2. Host 權限僅限 `http://127.0.0.1/*`，拒絕 wildcard remote hosts 與 `<all_urls>`。
3. `extension_pages` CSP 必須為 `script-src 'self'; object-src 'self'; connect-src ws://127.0.0.1:17842`，
   嚴格禁止 `unsafe-eval`、`unsafe-inline`、外部 script 或 non-loopback connect-src。

## Popup accessibility policy

MV3 popup 的每個互動控制項（目前為 Start 與 Stop 按鈕）都必須提供非空無障礙名稱：
直接寫 `aria-label`，或以 `aria-labelledby` 指向頁面上非空的可見文字。當
`aria-labelledby` 列出的任一 ID 不存在、或對應元素沒有可見文字時，gate 必須
fail closed；不得指向空字串或不存在的 anchor。`tools/extension-check.ps1` 掃描
popup.html 的 button/input/select/textarea 控制建構，缺漏名稱會讓檢查失敗；offline
self-test 涵蓋有效 aria-label、缺少名稱與 labelledby 目標失效的情況。此 gate 是
source 層級政策，不宣稱瀏覽器 runtime 螢幕閱讀器實測。
