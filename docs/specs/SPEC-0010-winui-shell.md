---
id: SPEC-0010
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["apps/control-model/**", "apps/winui-shell/**"]
---

# SPEC-0010：WinUI 3 易用控制殼

## 成功條件

第一次開啟時，普通使用者只需要連接引擎、選擇輸出群組並按「一鍵改善」；
所有操作都能顯示目前是已控制、繞過或降級狀態。Expert 開關只改變可見的
控制面板，不會改變已提交的音訊 graph，也不會讓 UI 執行緒直接碰 RT graph。

## In / Out

In：WinUI 3 視窗、Easy/Expert 顯示、固定輸出群組卡片、場景卡片、Windows
音量與 IR 相位滑桿、版本化 named-pipe Hello／SceneApply／VolumeNotification
命令、連線失敗回復；音量拖曳使用 40 ms bounded debounce 與 command serialization，
只送出最新的控制值。

Out：WaveRT/PortCls 驅動、實體裝置枚舉、音訊處理、VST3 UI、校正量測與
任何編譯後的 EXE／DLL。這些能力仍由各自 Spec 與 worker 負責。

## 介面與資料流

`EasyControlViewModel` 是唯一 binding surface。WinUI 只讀寫其公開屬性與
非同步命令；ViewModel 透過 `NamedPipeControlClientV1` 建立 local-only
versioned IPC，Hello 成功後才可送出 SceneApply 或 VolumeNotification。

固定輸出群組 ID 為 `main`、`low-latency`、`surround`；它們是 UI 選擇值，
不是實體 Endpoint ID。場景 ID 延用 `game`、`movie`、`voice`、`studio`。

`DeviceSwitchModel` 的控制面狀態固定為 `Preparing → Fading → ReadyToCommit → Synced`；
未完成暖機或 crossfade 時 `Commit` 必須失敗，Rollback 保留上一個 active device。實際
endpoint bind、30 ms equal-power crossfade 與回復仍由 C++ sink worker 負責。

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

## 相容性

ViewModel 命令只使用 `IpcEnvelopeV1` 與既有 payload；新增欄位不可改變已知
bytes。WinUI shell 可在沒有引擎時啟動，並以 Degraded 狀態呈現；control-model
仍可在沒有 WinUI SDK 的環境由獨立 check project 驗證。

## 驗收

1. Fresh clone 的 control-model check 通過，並覆蓋固定輸出群組、連線狀態、
   One-Tap／Scene／音量命令的 binding surface 與 bounded debounce。
2. WinUI source-only shell 只引用 control-model；git history、CI artifacts
   與 release 不包含編譯產物。
3. 沒有 engine pipe 時，按連接會在 bounded timeout 後顯示 Degraded，UI 不崩潰；
   連線成功時 Hello 必須先收到 Ack。
4. UI 關閉或連線失敗不會寫回 Windows Master，也不會把音量恢復到 100%。
