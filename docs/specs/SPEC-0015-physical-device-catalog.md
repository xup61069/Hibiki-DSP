---
id: SPEC-0015
status: accepted
owner: hibiki-maintainers
authority: platform-boundary
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002, ADR-0004]
source_globs: ["src/hub/**", "tests/unit/**", "schemas/physical-device-catalog-v1.schema.json", "docs/specs/SPEC-0015-physical-device-catalog.md"]
---

# SPEC-0015：實體裝置目錄與切換前資料契約

## 成功條件

Windows worker 從 `IMMNotificationClient`／WASAPI 枚舉到的實體 endpoint，必須先進入
固定容量的 control-plane catalog，經過格式、身份與狀態驗證後，才可以提供給裝置切換
交易的 `Prepare`。目錄變更不能直接觸碰 RT thread，也不能把已拔除的 endpoint 當成可用。

## In / Out

In：最多 32 個 render/capture endpoint、穩定 endpoint ID、顯示名稱、LPCM 聲道／取樣率／
buffer 能力、Active／Disabled／Unplugged／Unknown 狀態、每個 flow 唯一 default、單調
事件 sequence、可選取判定與 bounded remove/upsert。

Out：COM 枚舉、PortCls／WaveRT driver、WASAPI worker 啟動、實體硬體能力測量與 UI 顯示。
那些元件只能透過 catalog 的 descriptor 與 `DeviceSwitchTransaction` 交接；本契約不宣稱
已完成可載入 driver 或真實裝置 soak test。

## 介面與資料流

`PhysicalDeviceDescriptorV1` 是 control-plane DTO，schema version 固定為 1。endpoint ID
最多 260 bytes、名稱最多 128 bytes，均拒絕控制字元；聲道只接受 1／2／6／8，取樣率只接受
44.1／48／96／192 kHz，buffer 介於 16–4096 frames。`PhysicalDeviceCatalogV1::upsert`
以 endpoint ID 做 replace；新 default 會清除同一 flow 的舊 default。Windows watcher 的
sequence 以單調規則更新；較舊的 descriptor／狀態事件直接拒絕，防止通知亂序回退狀態。
跨程序／跨 AI 的 JSON 交換使用 `schemas/physical-device-catalog-v1.schema.json`；schema
不允許把私人裝置資料或未定義欄位靜默帶入正式 handoff。

只有 `Active` endpoint 才能 `selectable` 或 `mark_default`。進入 Disabled／Unplugged／
Unknown 時，catalog 清除 default；切換 worker 必須回到上一個已同步 endpoint，或使用
safe-start／Degraded，而不是重試到 100% 音量。

## 失敗／安全／相容性

- 非法 descriptor、容量超過 32、非 Active default、過期 sequence 或 allocation 失敗時，既有
  catalog 保持不變。
- 移除未知 ID 回傳 `NotFound`；狀態／flow 不合法回傳 `InvalidState`。
- catalog 是 UI／worker snapshot，不得在 audio callback 讀取可變字串或呼叫其 mutator。
- `DeviceSwitchTransaction` 仍負責 Prepare → crossfade → Commit／Rollback；catalog 只回答
  「是否存在且可選」，不取代 sink handoff。
- v1 不把 endpoint ID 寫入 SceneProfile；Scene 仍綁定 logical output group，裝置切換
  保持 Scene／volume／calibration 的語意連續。

## 驗收

1. CTest 覆蓋 render default 互斥、capture／render 分流、拔除後不可選、亂序 sequence、
   非法 descriptor、移除與容量邊界。
2. Windows adapter 將 watcher snapshot 套入 catalog 時，不在 callback 配置、等待或
   釋放 COM；真實 hotplug／Audio Service restart／WASAPI soak 仍由 driver release gate 驗證。
3. 跨 AI handoff 必須記錄 catalog contract 的 source commit；不得提交真實私人 endpoint
   ID 或顯示名稱到 repository。
