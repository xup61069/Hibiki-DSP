---
id: SPEC-0010
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["apps/control-model/**", "apps/winui-shell/**", "evidence/0000-foundation/winui-vst3-history-clear-v1.json", "tools/winui-shell-check.ps1"]
---

# SPEC-0010：WinUI 3 易用控制殼

## 成功條件

第一次開啟時，普通使用者只需要連接引擎、選擇輸出群組並按「一鍵改善」；
所有操作都能顯示目前是已控制、繞過或降級狀態。Expert 開關只改變可見的
控制面板，不會改變已提交的音訊 graph，也不會讓 UI 執行緒直接碰 RT graph。

## In / Out

In：WinUI 3 視窗、Easy/Expert 顯示、固定輸出群組卡片、場景卡片、實體裝置目錄鏡像與
DeviceSwitch request、Windows 音量與 IR 相位滑桿、有效音量／安全上限／來源／致動器的
可讀摘要、Expert 的 Matrix／DSP Graph／VST3 隔離／校正唯讀摘要，以及 Windows session、
process loopback、瀏覽器單分頁與 direct bypass 的路由健康卡片、App 工作階段清單與「刷新 App 工作階段清單」動作（附帶實體 per-App 擷取／重新送出未驗證的清楚邊界說明）、版本化 named-pipe Hello／SceneApply／VolumeNotification／DeviceSwitch／IrPrepareCommand
／DeviceCatalogRequest／DeviceCatalogSnapshot／ControlStatusRequest／ControlStatusSnapshot／SessionCatalogRequest／SessionCatalogSnapshot 命令、連線失敗回復；音量拖曳使用 40 ms bounded debounce 與 command serialization，
只送出最新的控制值。
正式殼層使用單一 NavigationView 導覽，固定六頁依序為「快速開始」（Ctrl+1）、
「場景」（Ctrl+2）、「自訂預設」（Ctrl+3）、「音量保護」（Ctrl+4）、「路由健康」（Ctrl+5）
與「Expert Panel」（Ctrl+6）；正式 shell 不提供 VST3 時間軸編輯面。
這些頁面是同一 shell 內的面板切換，不新增 IPC 命令，也不改變 Easy/Expert 的
顯示邊界：Expert Panel 頁面只收納既有 Expert 唯讀摘要與本機編輯面。
正式 shell 不再暴露 VST3 時間軸編輯卡片；VST3 host 能力仍由 SPEC-0008 的 bounded
model seams 管理，不得由 shell 宣稱已同步到 worker、plugin 或持久儲存。

正式 shell title bar 提供可及的深色模式切換；選擇以 bounded JSON 保存於
`%LOCALAPPDATA%\Hibiki DSP\ui-theme-v1.json`，讀取或寫入失敗時回到淺色，且不影響
engine/control-plane 狀態。Root theme 以 `ElementTheme.Light`／`ElementTheme.Dark`
套用到 NavigationView、InfoBar、cards、inputs、footer 與自訂 ThemeResource。

音量保護頁的主要視覺卡稱為「等響度補償」，顯示既有 `EqVisualSurface` 的狀態、來源圖例
與曲線。它只呈現引擎確認的 user-space `EqVisualSnapshotV1`；這不是音量量測、耳機校準、
受限等響度表格或任何聲學合規證據。

Out：WaveRT/PortCls 驅動、實體裝置枚舉、音訊處理、VST3 host UI、校正量測與
任何編譯後的 EXE／DLL。這些能力仍由各自 Spec 與 worker 負責。

## 介面與資料流

`EasyControlViewModel` 是主要 binding surface；`ExpertSurfaceModel` 提供固定、唯讀且
明確標示未認證／未校準的詳細摘要。WinUI 只讀寫其公開屬性與
非同步命令；ViewModel 透過 `NamedPipeControlClientV1` 建立 local-only
versioned IPC，Hello 成功後才可送出 SceneApply 或 VolumeNotification。

`EasyControlViewModel` 不再向 WinUI shell 暴露 VST3 時間軸編輯 binding seam。若未來需要
重新引入，必須另立 Spec 切片並保持 fail-closed 邊界：不送 IPC payload、不寫 native file
store，也不代表 engine 已載入、plugin 已套用或 timeline 已持久化。

固定輸出群組 ID 為 `main`、`low-latency`、`surround`；它們是 UI 選擇值，
不是實體 Endpoint ID。場景 ID 延用 `game`、`movie`、`voice`、`studio`。

`PhysicalDeviceCatalogV1` 是引擎提供的 bounded metadata snapshot；ViewModel 只鏡像
Active render/capture 裝置，不自行枚舉或捏造裝置。可選 render 卡片以
`ControlMessageType.DeviceSwitch` 發送固定 288-byte request，帶 endpoint identity、格式
與 catalog sequence；在引擎 Ack 前 UI 只顯示 Preparing/Fading，不宣稱已同步。
`RefreshPhysicalDevicesAsync` 送出空 payload 的 `DeviceCatalogRequest`，只接受帶相同 request
ID 的 `DeviceCatalogSnapshot` 回覆；錯誤、逾時或過期快照會保留上一份 picker 狀態。

`DeviceSwitchModel` 的控制面狀態固定為 `Preparing → Fading → ReadyToCommit → Synced`；
未完成暖機或 crossfade 時 `Commit` 必須失敗，Rollback 保留上一個 active device。實際
endpoint bind、30 ms equal-power crossfade 與回復仍由 C++ sink worker 負責。

`RouteHealthCardV1` 是保守的唯讀投影：shell 初始只顯示 `Pending` 或明確的
`Bypassed`，不能把 process-level loopback 當成瀏覽器單分頁，也不能把 vendor ASIO／
WASAPI Exclusive 說成已受控。`ControlStatusSnapshot` 可透過 `ApplyControlStatusSnapshot`
以 bounded、唯一 ID 的快照取代預設值；無效或重複身份必須整批拒絕。

`VolumeSafetyStateV1` 同時呈現 `requestedDb`、`safetyCeilingDb`、`effectiveDb`、mute、
generation、origin 與 actuator。UI 永遠分開顯示使用者要求和實際有效值；安全截頂時說明
「安全限制已介入」，狀態 generation 倒退或數值不合法則保留上一個狀態，不回寫 Windows。

`PrepareIrAsync` 只接受使用者指定的 bounded local path 與目前 phase policy；ViewModel 先檢查
檔案存在與 64 MiB 上限，再送出固定 288-byte `IrPrepareCommand`。正式殼層與 Compatibility Preview
都提供「選擇並準備 IR WAV 檔」入口，經系統檔案挑選器取得使用者指定路徑。Engine Preview 的
control worker 在 pipe thread 之外讀取 WAV、解碼、phase-transform 並準備 convolver，只有成功才回 Ack。
Bypass、過期／不存在檔案、格式或 tap 上限錯誤都 fail-closed；這個 Ack 仍不是 graph commit、
實體 sink 或 audible playback 證據。

Hello 與裝置 catalog 成功後，ViewModel 會以序列化的 `ControlStatusRequest` 取得一次完整
狀態；status store 未掛載時顯示控制狀態暫不可用，但不把整個音訊連線誤判為失敗。


視窗大小與位置會在正常關閉時保存到 `%LOCALAPPDATA%\Hibiki DSP\window-placement-v1.json`，
下次啟動前先還原；讀取或寫入失敗時使用內建預設 1080x720 佈局，不影響引擎連線。
位置與尺寸都經 bounded clamp，避免還原到螢幕外或小於最小可用尺寸。
## 失敗與安全

- 沒有輸出群組時 One-Tap Enhance fail-closed，不產生 SceneApply。
- pipe 連線、Hello、命令回覆任何一步失敗，UI 顯示 Degraded，保留上一個
  已提交 graph；不得假裝已控制，也不得重試成無限迴圈。
- 音量拖曳不可並行寫入 named pipe；上一個尚未送出的值可取消，最終值必須在
  bounded debounce 後送出，避免 OSD／UI event storm。
- ViewModel 的 async pipe 工作不在 audio callback 執行；RT thread 不等待 UI、
  COM、named pipe 或檔案系統。
- 視窗關閉時必須釋放 pipe client。未來若加入自動重連，必須另立 ADR，並採
  bounded backoff 與明確使用者狀態提示。
- Expert 摘要不可宣稱 Matrix/VST3/equal-loudness 校正已提交；沒有對應版本化 IPC command 時，
  UI 必須維持唯讀並顯示「未認證／未校準」狀態。
- Shell 不得宣稱 VST3 時間軸已同步到 worker、plugin 或持久儲存；任何未來編輯面都必須
  明確標示本機邊界，且 commit 只能改變 managed published snapshot。
- 路由健康卡片不得以顏色作為唯一狀態訊息；每張卡片都要有可讀的狀態標籤與邊界說明。
- 音量安全投影不得把 `requestedDb` 當成實際輸出；`effectiveDb` 必須不高於要求值與
  safety ceiling，並拒絕過期 generation。
- Theme preference 不是控制狀態；解析失敗時必須 fail-soft 回到淺色，不能阻塞 shell
  啟動、改寫音量或送出任何 engine 命令。

## 可及性與易用性

- Root、連接、一鍵改善、輸出群組、Expert、音量、靜音與詳細控制面板都必須有
  明確的 `AutomationProperties.Name`；需要上下文的控制項提供 `HelpText`。
- 深色模式切換與等響度補償曲線都必須有明確的 `AutomationProperties.Name`；狀態文字
  使用 polite live region，圖例不得只靠顏色區分。
- `tools/winui-shell-check.ps1` 必須枚舉 `MainWindow.xaml` 內的 `Button`、`ComboBox`、
  `Slider`、`ToggleSwitch`、`CheckBox`、`TextBox` 與 `NumberBox` opening elements，任何
  缺少或空白的 `AutomationProperties.Name` 都要 fail closed；固定 anchor 檢查只是額外
  的 binding gate，不得取代完整 control 掃描。
- 場景按鈕的可及性名稱與說明從 `SceneCard` 綁定，不依賴視覺排版或顏色傳達狀態。
- 狀態文字以 polite live region 告知連線／控制結果，避免螢幕閱讀器被高頻音量事件打斷。
- 頁面切換的進場動效以頁面層級容器為主；共用 section card 不得再疊加獨立進場動效，
  避免導覽時前一頁文字或卡片殘影短暫覆蓋新頁面。動效只是裝飾，不得是理解狀態或完成控制的必要條件。
- 主要導覽的每個 `NavigationViewItem` 都必須提供非空且語意可辨識的 Fluent icon；
  圖示只作為視覺路由提示，文字與 `AutomationProperties.Name` 仍是完整語意來源。
- 主要導覽提供 Ctrl+1 到 Ctrl+6 的快捷鍵，依序切換六個頁面；每個導覽項目都有
  明確的 AutomationProperties.Name，鍵盤使用者不需依賴視覺位置或顏色即可到達任一頁。
- XAML 靜態 gate 必須檢查上述 binding；目標 Windows App SDK 環境仍需做真正的鍵盤、
  螢幕閱讀器、高對比與文字縮放驗收。

## 相容性

ViewModel 命令只使用 `IpcEnvelopeV1` 與既有 payload；新增欄位不可改變已知
bytes。WinUI shell 可在沒有引擎時啟動，並以 Degraded 狀態呈現；control-model
仍可在沒有 WinUI SDK 的環境由獨立 check project 驗證。

Compatibility Preview 在純 .NET SDK 主機保留 `EnableDefaultApplicationDefinition=false`、
`EnableDefaultPageItems=false`，且必須維持 Core MRT resource tooling 停用：dotnet CLI
建置不會載入 Visual Studio 的 Appx packaging 工作（MSBuild host boundary），因此
MrtCore.PriGen 會以 MSB4062 失敗；即使主機已安裝 VS2026 與相關工作也一樣。此模式不產生 PRI；
缺漏的主題資源由 fail-soft resolver 忽略，視窗可啟動但呈現未套樣式的降級外觀。
Compatibility Preview 的建置與啟動 smoke 不構成樣式、資源載入、無障礙或正式 XAML 證據。

## 驗收

1. Fresh clone 的 control-model check 通過，並覆蓋固定輸出群組、連線狀態、
   One-Tap／Scene／音量／DeviceSwitch 命令、目錄 stale/unplugged rejection、Expert
   Matrix/DSP/VST3/校正摘要、ControlStatusSnapshot route／volume round-trip、duplicate
   rejection、音量安全截頂／過期 generation、status request correlation 與 bounded debounce。
2. WinUI source-only shell 只引用 control-model；git history、CI artifacts
   與 release 不包含編譯產物。
3. 沒有 engine pipe 時，按連接會在 bounded timeout 後顯示 Degraded，UI 不崩潰；
   連線成功時 Hello 必須先收到 Ack。
4. UI 關閉或連線失敗不會寫回 Windows Master，也不會把音量恢復到 100%。
5. WinUI source gate 通過 accessibility names/help text/live-region 檢查；目標環境再補
   UI Automation 與螢幕閱讀器實機證據。
6. 正式 preview 實際驗證淺色與深色主題切換；等響度補償卡在未連線時顯示安全的離線狀態，
   連線後只隨確認的 `EqVisualSnapshotV1` 更新。
