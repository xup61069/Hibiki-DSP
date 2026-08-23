---
id: SPEC-0009
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-23
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["extensions/**", "tools/extension-check.ps1"]
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
新的 capture-state，popup 不得繼續顯示「Capturing」。手動 Stop 仍是唯一主動
`track.stop()` 擁有者，ended 監聽器只負責自然結束路徑，不得重複 teardown。
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
時丟棄送出 packet。receiver 已限制 localhost、WebSocket frame 大小、mask、ping/close
與 decoder 驗證。`TabCaptureQueueV1` 將 validated packet 複製到四格固定 SPSC ring，
控制執行緒可用 `enqueue_tab_capture_packet_v1` 作 callback，RT lane 再以 caller-owned
buffer pop；滿載會回報 dropped blocks，不阻塞 WebSocket。`process_tab_capture_lane_v1`
會把一個 queue block 送入 `AudioEngineModel::process_lane_block`，因此沿用同一份
immutable graph、lane mapping 與 Group Master；adapter 不配置、不等待，也不擁有音訊
buffer。`process_tab_capture_lane_to_wasapi_v1` 使用相同 effects 與 lane validation，完成
graph／Group Master／limiter 後只提交一次到 WASAPI handoff；sink 未綁定時回傳失敗且不
宣稱已播放。降噪模型 provenance、權限提示與斷線重連 policy 仍必須在獨立 source component
完成後，才能宣稱「單分頁掛降噪」。

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
