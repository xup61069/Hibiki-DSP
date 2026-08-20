---
id: SPEC-0002
status: accepted
owner: hibiki-maintainers
authority: product-behavior
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0002]
source_globs: ["src/**", "schemas/output-group-volume-v1.schema.json"]
---

# SPEC-0002：Windows volume link 與 ISO 226 boundary

## Windows volume

虛擬 endpoint 暴露 volume/mute node；VolumeBroker 只作控制面 mirror/ACK。引擎以 dB
作為 canonical value，透過 origin GUID 與 generation 防止 feedback loop。volume ramp、mute
fade、device crossfade 與 safety clamp 必須保留 last-known-safe gain。

目前 user-space contract 已提供 `apply_windows_notification`：只接受有限 dB 範圍與不倒退
的 generation，接受後將 origin 設為 Windows 並重新套用 safety。真正的
`IAudioEndpointVolume`／driver callback 仍待 SPEC-0003 的 Windows driver work。
Windows build 現在提供 `WindowsVolumeBroker`：control thread 可 bind/unbind
`IAudioEndpointVolume`、write canonical dB/mute with caller GUID、read-back 實際量化值，
並以 lock-free atomic snapshot 接收 callback；callback 本身不配置、不等待、不呼叫 COM。
`WindowsDeviceWatcher` 同樣只把 `IMMNotificationClient` 的 default/add/remove/state/property
事件複製到 bounded snapshot；實際 rebind 必須由 worker 讀取 snapshot 後執行，避免在 OS
callback 裡 unregister、release 或建立 COM 物件。
Driver ABI 另外使用 Q16.16 dB；`db_to_q16_16`／`q16_16_to_db` 將量化集中在 boundary，
避免 scalar 0–1 與 engine dB 互相漂移。
每個 driver notification 同時攜帶 endpoint 與 event-context GUID；VolumeBroker 可用固定
來源 context 做 ACK/read-back，而不靠浮點相等或時間猜測 feedback loop。

`AcousticAnchorV1` 會保存 test signal、endpoint gain、1 kHz SPL、uncertainty 與 F3；
只有 speaker／headphone-coupler 才能回報 calibrated phon，`headphone-estimated` 永遠
標示為估算，不能包裝成 HATS/coupler 測量。

## ISO compensation

對合法取得的兩條 ISO contour 以 1 kHz 正規化：

`G(f)=strength*((current(f)-current(1k))-(reference(f)-reference(1k)))`

`iso226_spl_from_phon` 實作 ISO 226:2023 的公式，但只接受 caller-supplied
`Iso226FormulaPointV1 {alpha_f, threshold_db, transfer_db}` 與 reference parameters；
repository 不包含標準的 29 點係數表。20–90 phon 以外回傳 invalid，1 kHz reference invariant
由測試固定。

預設 reference=80 phon。未校準時只能使用 Relative Compensation；校準模式必須保存 acoustic
anchor、測試信號、實測 SPL、gain path 與 uncertainty。ISO 只定 magnitude；phase 是 Hibiki 的
minimum/mixed/linear implementation choice。

Exporter 只接受 caller-supplied `PeqFilterV1`，目前可輸出 Hibiki JSON profile、Equalizer
APO、CamillaDSP YAML 與 REW filter list；不內嵌 ISO 授權表格，也不把估算耳機資料標成正式
量測結果。同一 exporter 也能把 caller-supplied interleaved impulse samples 寫成 32-bit
IEEE-float WAV IR；它只負責檔案格式，不替任何未授權量測資料背書。

`EqualLoudnessPolicyV1` 會驗證 mode、phon、strength、boost cap 與 calibrated anchor；
`Program-aware` 只保留 policy boundary，BS.1770 内容分析尚未加入。

## IR 相位／延遲滑桿

`IrPhasePolicyV1` 是獨立於 ISO magnitude 的 control-plane contract。`strength` 固定為
0..1，0 代表不增加 buffering；1 代表該模式允許的最高相位校正。模式語意固定如下：

- `minimum-phase`：Game 預設，IIR、0 ms 額外 buffering。
- `mixed-phase`：Balanced，最多 80 ms 的 mixed-phase FIR。
- `linear-phase`：Movie 預設，最多 160 ms 的 linear-phase FIR。
- `bypass`：Strict Direct，完全不掛 IR／校正鏈。

`resolve_ir_phase_policy` 只解析可承諾的最大延遲與是否需要 FIR，不假裝已經生成 FIR
係數；實際 IR 仍由 caller 提供，graph commit 前必須以量測的實際 latency 取代預估值。
UI 顯示「0 ms 額外緩衝」「最高 80/160 ms」等可驗證文字，不使用「零延遲完美相位」宣稱。
Scene 若省略 `ir_phase` 欄位，向後相容地採用 `minimum-phase/strength=0`。

公開 repository 不得包含 ISO 授權文件、掃圖、完整受限表格或未核准 golden data。正式係數加入
GPL source 前必須由人類 reviewer 完成法務 gate。

## Safety

一般 boost 上限 +6 dB，Expert calibrated 上限 +12 dB；低於實測 F3 不 boost；最後一級 −1 dBTP
limiter。曲線以 immutable coefficient bank 與 100–200 ms crossfade 更新。
