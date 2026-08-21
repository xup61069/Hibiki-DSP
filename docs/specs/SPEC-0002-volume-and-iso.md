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

`AudioEngineModel` 的 RT Group Master 已實際使用 dB-domain ramp：一般變更 8 ms、mute
5 ms、unmute 15 ms；control worker 只發布 Q16.16 target，audio thread 自己推進固定狀態，
因此不會在音量鍵或 Scene 結束時產生 block-level hard step。實體 device crossfade 仍由
`OutputCrossfade` 的 30 ms sink handoff 負責。

每個已註冊的 output group 都有自己的 canonical `requested_db`／mute／generation、safety
reconcile 與 RT ramp；`OutputGroupVolumeBankV1` 固定最多 32 組，群組在 graph commit 前
註冊，RT 只讀該群組 immutable label 對應的 atomic Q16.16 word，不會把 Main 的音量誤套到
Movie、Surround 或其他 sink。舊的 `apply_windows_volume(notification)` 與 `volume()` 仍
代表 `main`，新的 overload 可指定 group。Strict Direct graph 直接跳過 Group Master，
避免把系統音量／校正誤標成 bit-perfect。

控制 pipe 保留 16-byte legacy Main volume payload，並新增 48-byte grouped payload（16-byte
volume header + 31-byte UTF-8 output-group label）。C++ 與 C# 兩端都必須接受 legacy、產生
grouped target；未知 label、非零 padding、非法 UTF-8 或格式長度一律拒絕。UI 選定的 group
會使用 grouped target，避免滑桿看似控制 Surround 卻實際回寫 Main。

Group render 完成後，非 Strict Direct Scene 會經過 `TruePeakLimiterV1` 的 −1 dBTP
bounded guard；它以三個線性 inter-sample probes 作保守估算並採 block-coherent gain，
目前不宣稱 ITU/BS.1770 conformance，正式 meter oracle 仍是 release gate。

目前 user-space contract 已提供 `apply_windows_notification`：只接受有限 dB 範圍與不倒退
的 generation，接受後將 origin 設為 Windows 並重新套用 safety。真正的
`IAudioEndpointVolume`／driver callback 仍待 SPEC-0003 的 Windows driver work。
Windows build 現在提供 `WindowsVolumeBroker`：control thread 可 bind/unbind
`IAudioEndpointVolume`、write canonical dB/mute with caller GUID、read-back 實際量化值，
並以 lock-free atomic snapshot 接收 callback；callback 本身不配置、不等待、不呼叫 COM。
`WindowsControlRuntimeV1` 會在 control/COM worker 綁定目前 eRender/eConsole default endpoint，
提供 `refresh_default_volume`、`read_volume`、`write_volume` 與 non-blocking `poll_volume`；
另提供 `refresh_default_volume_if_changed`：它以 endpoint ID 比對，未切換時保留既有
callback registration，只有 ID 改變才重綁；失效或錯誤仍由 `refresh_default_volume` 強制
rebind。這些方法只能由 control worker 呼叫，caller 必須把
snapshot 轉成 Group Master state，audio callback 不得直接碰 COM。`WindowsVolumeLinkV1`
是明確的 control-thread adapter：它把 snapshot 送入指定 output group 的
`AudioEngineModel` canonical volume bank，拒絕非法 dB、回報 stale generation，並可登記
Hibiki UI/Scene/Safety 寫入所使用的 event-context GUID；匹配自家 GUID 的 callback 會回報
`IgnoredSelf` 而不重複套用。adapter 不寫回 COM、不配置，也不在 queue/RT thread 執行。
四個預設來源（UI、Safety、Scene、Session）固定寫在
`config/distribution-profile.yml`，`WindowsVolumeLinkV1` 建構時自動註冊；換電腦或換 AI
不得重生這些 GUID。其他整合來源只能透過明確的 `add_ignored_context` 加入，並須在
handoff/evidence 中記錄用途。
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
由測試固定。`build_formula_compensation` 會以同一組 caller-supplied points 計算
current/reference phon 的 1 kHz 正規化增益、F3/boost 限制與 `limited` 診斷；缺少 1 kHz
anchor 或任何公式點 invalid 時整批 fail-closed。

預設 reference=80 phon。未校準時只能使用 Relative Compensation；校準模式必須保存 acoustic
anchor、測試信號、實測 SPL、gain path 與 uncertainty。ISO 只定 magnitude；phase 是 Hibiki 的
minimum/mixed/linear implementation choice。

Exporter 只接受 caller-supplied `PeqFilterV1`，目前可輸出 Hibiki JSON profile、Equalizer
APO、CamillaDSP YAML 與 REW filter list；不內嵌 ISO 授權表格，也不把估算耳機資料標成正式
量測結果。`compile_bounded_peq_correction_v1` 可把 caller-supplied 的 measured/target
頻響點編成最多 16 段 bounded PEQ，並保留 clipped/unrepresented `limited` 狀態；這是
校正檔 compiler baseline，不是房間聲學 optimizer。編譯後可交給同一組 exporter。
同一 exporter 也能把 caller-supplied interleaved impulse samples 寫成 32-bit
IEEE-float WAV IR；它只負責檔案格式，不替任何未授權量測資料背書。

`PeqProcessorV1` 會把最多 16 個 `PeqFilterV1` 編譯成 RBJ peaking biquad，固定支援 1–8
聲道；係數在 control side 準備，`process_interleaved` 只使用固定 state，不配置、不等待，
並對非有限輸入 fail-safe。單分頁 adapter 可選擇在 Graph 前套用這組 PEQ；filter 的 sample
rate／聲道不符時整個 lane block fail-closed。這是 per-lane EQ，不代表已完成 VST3 或正式
ISO 係數 fit。

`IrConvolverV1` 提供最多 4096 taps、mono 或逐聲道 kernel 的固定容量 direct FIR；它保存
跨 block history，會檢查 `IrPhaseResolutionV1` 已經 valid、sample rate／聲道一致與所有
係數 finite。它只執行 caller 提供的 IR，不自行推導 minimum/mixed/linear-phase kernel；
宣告的 delay 仍須由實際量測驗證。

`decode_ir_wav_v1` 是 control-plane 的 bounded RIFF/WAVE importer：接受 IEEE Float32 與 signed
PCM16/24/32、最多 8 聲道與 4096 frames，檢查 RIFF container/chunk 邊界、block alignment、
sample-rate、finite samples 與記憶體上限；它不讀檔、不配置在 RT thread。`prepare_ir_convolver_from_wav_v1`
會把 interleaved file samples 轉成 convolver 的 channel-major kernel，並在 graph commit 前
套用已解析的 phase policy。這仍不代表已推導 minimum/mixed/linear kernel，或已在實體 sink
上套用校正。

`EqualLoudnessPolicyV1` 會驗證 mode、phon、strength、boost cap 與 calibrated anchor；
`Program-aware` 另有 `ProgramAwareLevelControllerV1` 的慢速內容音量控制：預設保留無配置的
RMS 代理；`KWeightedProxy` 會在固定容量狀態內串接高通與高頻 shelf 兩段 K-weighting，並
可選排除呼叫端標示的 LFE channel。兩者都使用 3 秒分析窗、靜音門、增益上限與 dB/s
速率限制，適合由單一 Lane 明確選用。K-weighted 路徑仍不是正式 BS.1770 meter（沒有
完整 gated loudness、合法 oracle 與 true-peak conformance），UI／文件必須顯示 proxy；它
也不會在 `Relative`／`Calibrated` ISO 曲線中偷偷改變音色。正式 BS.1770 analyzer 與 oracle
仍是 release gate。

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
