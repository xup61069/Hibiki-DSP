---
id: SPEC-0015
status: accepted
owner: hibiki-maintainers
authority: platform-boundary
last_reviewed: 2026-08-24
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/**", "tests/unit/**", "schemas/physical-device-catalog-v1.schema.json", "schemas/device-catalog-snapshot-v1.schema.json", "docs/specs/SPEC-0015-physical-device-catalog.md"]
ui_source_globs: ["apps/winui-shell/**", "tools/winui-shell-check.ps1"]
---

# SPEC-0015：實體裝置目錄與切換前資料契約

## 成功條件

Windows worker 從 `IMMNotificationClient`／WASAPI 枚舉到的實體 endpoint，必須先進入
固定容量的 control-plane catalog，經過格式、身份與狀態驗證後，才可以提供給裝置切換
交易的 `Prepare`。目錄變更不能直接觸碰 RT thread，也不能把已拔除的 endpoint 當成可用。

## In / Out

In：最多 32 個 render/capture endpoint、穩定 endpoint ID、顯示名稱、LPCM 聲道／取樣率／
buffer 能力、Active／Disabled／Unplugged／Unknown 狀態、每個 flow 唯一 default、單調
事件 sequence、可選取判定與 bounded remove/upsert、worker-owned Windows COM enumeration，
以及引擎到 UI 的版本化 catalog snapshot。

Out：PortCls／WaveRT driver、physical sink 開啟、實體硬體能力測量與正式 UI 顯示。COM
enumerator 的 source adapter 由 Engine Preview 在 COM-initialized user-space worker 呼叫，
只能產生 metadata snapshot；本契約不宣稱已完成可載入 driver、目標 Windows/WDK 驗證或
真實裝置 soak test。

## 介面與資料流

`PhysicalDeviceDescriptorV1` 是 control-plane DTO，schema version 固定為 1。endpoint ID
最多 260 bytes、名稱最多 128 bytes，均拒絕控制字元；聲道只接受 1／2／6／8，取樣率只接受
44.1／48／96／192 kHz，buffer 介於 16–4096 frames。`PhysicalDeviceCatalogV1::upsert`
以 endpoint ID 做 replace；新 default 會清除同一 flow 的舊 default。Windows watcher 的
sequence 以單調規則更新；較舊的 descriptor／狀態事件直接拒絕，防止通知亂序回退狀態。
跨程序／跨 AI 的 JSON 交換使用 `schemas/physical-device-catalog-v1.schema.json`；schema
不允許把私人裝置資料或未定義欄位靜默帶入正式 handoff。
WinUI／engine 的 `DeviceSwitch` request 使用 `schemas/device-switch-request-v1.schema.json`
描述的欄位，再以 288-byte IPC payload 傳輸；UI 只有在 catalog entry 為 Active render 時
才產生 request，engine 未回 ACK 前不顯示已同步。
`endpoint_id` 維持非空且最長 260 字元的契約；持久化 schema 和實體目錄／DeviceSwitch wire
encoder 一樣拒絕 C0/C1 控制字元與 DEL，讓外部驗證在進入 transport 前就 fail-closed。
DeviceCatalogSnapshot 與 DeviceSwitch 的 `catalog_sequence` 必須非零；零值保留給
未初始化或沒有 freshness 的狀態，C++／C# encode、decode 與 JSON schema 使用同一規則。
UI 可送出空 payload 的 `DeviceCatalogRequest` 取得當前快照；control service 只有在註冊
snapshot provider 時才回傳 `DeviceCatalogSnapshot`，否則回 Error，避免把空目錄誤報為成功。
正式 WinUI 殼層提供「重新掃描裝置」入口，只呼叫 ViewModel 的 bounded IPC refresh；UI 不
枚舉 Windows endpoint。未連線、逾時、Error、格式錯誤或過期 sequence 都 fail-closed 並
保留上一份 picker 狀態；成功時只以最新 Active render 數量更新狀態文字。
引擎提供的 `DeviceCatalogSnapshot` v1 使用固定 16-byte header 與每筆 416-byte wire entry
（最多 32 筆），由 control worker 產生；C# 端必須驗證 reserved bytes、UTF-8、格式、重複
身份、default 互斥與非零 catalog sequence，通過後才以 atomic replace 更新 picker。快照過期或
格式錯誤時保留上一份目錄，不得清空或捏造裝置。
`WindowsPhysicalDeviceCatalogWorker` 在刷新時先建立 candidate catalog，再一次產生 snapshot
並提交 catalog／sequence；任一 flow、ID、friendly name、mix format、device period 或
publisher 驗證失敗，都保留上一份狀態並回傳 HRESULT。通知 callback 不得直接呼叫此 worker。
`WindowsPhysicalDeviceCatalogServiceV1` 將這個 worker 交易接到 control service：先完成候選
快照的 wire validation，再發布到 `DeviceCatalogSnapshotStoreV1`，最後才交換已提交 catalog；
`device_catalog_snapshot_reply_v1` 只複製最近完整 frame，沒有快照時回 Error，不執行 COM。

只有 `Active` endpoint 才能是 default；只有 `Active render` endpoint 才能
`selectable` 或由 UI `mark_default`。render 與 capture 各自最多一個 default。進入
Disabled／Unplugged／Unknown 時，catalog 清除該 flow 的 default；切換 worker 必須回到上一個
已同步 endpoint，或使用 safe-start／Degraded，而不是重試到 100% 音量。

## 失敗／安全／相容性

- 非法 descriptor、容量超過 32、非 Active default、過期 sequence 或 allocation 失敗時，既有
  catalog 保持不變。
- 格式錯誤的 `DeviceCatalogSnapshot` 必須先在候選快照完成所有 entry 驗證；decode 回傳失敗時
  caller output 保持完整預設值，不得留下已驗證的前段 entry。
- 移除未知 ID 回傳 `NotFound`；狀態／flow 不合法回傳 `InvalidState`。
- catalog 是 UI／worker snapshot，不得在 audio callback 讀取可變字串或呼叫其 mutator。
- snapshot store 的 mutex／vector 複製只存在 control-plane；不得從 RT callback 呼叫 store，
  也不得把 store 的 mutable catalog reference 傳入音訊執行緒。
- `DeviceSwitchTransaction` 仍負責 Prepare → crossfade → Commit／Rollback；catalog 只回答
  「是否存在且可選」，`DeviceRecoveryCoordinator::begin_rebind(catalog, endpoint_id)` 會在
  建立 target 前套用這個判定，不取代 sink handoff。
- v1 不把 endpoint ID 寫入 SceneProfile；Scene 仍綁定 logical output group，裝置切換
  保持 Scene／volume／calibration 的語意連續。

## 驗收

1. CTest 覆蓋 render default 互斥、capture／render 分流、拔除後不可選、亂序 sequence、
   非法 descriptor、移除、容量邊界、snapshot wire round-trip／reserved rejection，以及
   recovery 不得 rebind 到不可選 endpoint。
2. Windows adapter 將 watcher snapshot 交給 worker 時，不在 callback 配置、等待或釋放 COM；
   worker 的 COM enumeration／candidate rollback／snapshot publisher 有 source boundary，
   真實 hotplug／Audio Service restart／WASAPI soak 仍由 driver release gate 驗證。
3. `tools/live-device-catalog-check.ps1` 是 opt-in、只在 Windows 執行的本機 probe；它只輸出
   數量、sequence、payload size 與 wire pass，不輸出或提交 endpoint ID／friendly name。
4. `DeviceCatalogSnapshotStoreV1` 的空 store、合法發布、callback reply、壞 frame 保留舊快照與
   Windows service 未 bind fail-closed 行為由 CTest 覆蓋。
5. 跨 AI handoff 必須記錄 catalog contract 的 source commit；不得提交真實私人 endpoint
   ID 或顯示名稱到 repository。
