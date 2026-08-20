# Hibiki DSP baseline

## 已完成（有 commit 與 evidence）

- 公開 monorepo 文件、component license map 與 source-only paid-release policy。
- AI 接手規則、fresh-clone 流程與 source-only policy。
- `OutputGroupVolumeState` 與 ISO compensation public C++ boundary 的初始骨架。
- Scene graph、device-switch transaction 與初始 CMake/CTest 驗證入口。
- Immutable RT graph snapshot、2/6/8 聲道 mapping、IPC frame codec、ASIO stream model、VST
  quarantine model 與 MV3 tab-capture source prototype。
- Caller-owned output ring buffer、clock-drift estimator、bounded linear SRC prototype、
  Apache C driver ABI 與 portable driver validator。
- Source-only PowerShell installer bootstrapper with manifest/hash dry-run gate.
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
- `handle_control_frame_v1` validates Hello/Volume/Scene/graph lifecycle commands before passing
  them to a host-owned typed sink; malformed or rejected commands receive Error without touching
  the graph.
- `ControlCommandQueueV1` provides a fixed 64-slot SPSC pipe-worker to control-worker handoff;
  overflow is fail-closed with a dropped counter and no allocation/lock/wait.
- `EngineControlWorkerV1` consumes that queue and applies the four Easy Scene presets through
  AudioEngine Validate → Prepare → Commit; invalid scene IDs leave the last committed graph
  and revision unchanged, while volume commands share the same Group Master path.
- `RtLaneSnapshotV1` now carries fixed-size output-group bytes and exposes a group-filtered render
  path; four-lane fixtures verify that selecting one group does not mix the other three.
- `AudioEngineModel` facade connecting graph transaction, Windows volume notification and RT
  processing with one Group Master gain.
- `AudioEngineModel` 的 RT Group Master 已改讀 release/acquire 64-bit Q16.16 dB/mute word；
  mutable Windows volume state 僅留在 control plane，避免 callback/worker 與 RT 讀寫競態。
- `VolumeRampProcessorV1` 已接入 AudioEngine：8 ms 一般音量、5 ms mute、15 ms unmute，
  並以 8/48 kHz fixture 驗證單調 ramp 與完成後的精確 gain。
- `TruePeakLimiterV1` 已接在非 Strict Direct render 尾端：固定 8-channel、非有限值歸零、
  −1 dBTP bounded inter-sample guard；目前仍不宣稱正式 ITU/BS.1770 conformance。
- Windows-only `IAudioEndpointVolume` broker with non-blocking callback snapshot, dB/mute
  read-back and event-context write path; no physical endpoint was exercised on this machine.
- Windows-only `IMMNotificationClient` watcher with bounded default/add/remove/property event
  snapshots, consumed by a worker-side transactional recovery coordinator with safe-start mute
  after endpoint invalidation or Audio Service restart.
- Parameterized ISO 226:2023 SPL-from-phon formula using caller-supplied legal parameters;
  the 1 kHz invariant and phon bounds are covered by CTest without embedding the licensed
  29-point coefficient table.
- MS-PL WaveRT endpoint control-state core with fixed-format validation, Q16.16 dB safety
  ceiling, mute/generation ordering and Strict Direct behavior; the PortCls miniport is not
  yet wired and no `.sys` is built here.
- Persistent no-allocation linear SRC with phase and boundary-frame carry across output blocks;
  insufficient output capacity is rejected before partial consumption.
- VST host control model now requires trusted/certified same-channel descriptors and quarantines
  a lane on crash or missed bounded heartbeat; the source-only worker now exercises bounded
  Hello/Heartbeat/ProcessBlock passthrough/Shutdown IPC, while actual VST3 SDK hosting remains
  pending.
- `AudioSessionRegistry` keys Windows sessions by endpoint plus session-instance ID, preserves
  user routing on metadata refresh, and supports independent lane/output-group/gain-owner binding;
  Windows `IAudioSessionManager2` worker enumeration now populates it; callbacks remain
  non-blocking and only publish a sequence.
- `SessionRouteGraphBuilderV1` compiles active bound sessions into graph lanes with explicit
  WindowsSession versus HibikiInternal gain ownership; two Chrome-session fixtures render into
  separate output groups without cross-talk.
- `AudioSessionRegistry::set_makeup_gain_db` now provides the bounded per-session gain mutator;
  metadata refresh keeps that value and rejects values outside −144..+12 dB.
- `ProgramAwareLevelControllerV1` provides an allocation-free, slow RMS-proxy content level
  controller with silence gate, bounded boost/cut and dB-per-second rate limiting; it is not a
  BS.1770/K-weighted conformance implementation.
- `process_tab_capture_lane_v1` can apply that controller to one user-gesture-gated browser tab
  before graph processing, with a sample-rate match check and fail-closed behavior; no automatic
  microphone capture or denoising model is implied.
- `PeqProcessorV1` compiles up to 16 RBJ peaking filters into fixed per-channel state, and the tab
  lane can apply PEQ before program-aware level correction with matching sample-rate/channel checks.
- Worker-side session volume read/write now uses `ISimpleAudioVolume` with dB↔scalar conversion,
  event-context GUIDs and readback; unbound/exclusive/vendor ASIO paths remain explicit bypasses.
- `OutputSinkModel` now joins clock-drift estimation to persistent SRC per sink, preserving
  phase while applying bounded `base_step / sink_source_ratio` correction; physical clock fixtures
  are still pending.
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
  with Hello/Heartbeat/Process/Shutdown/Error types, exact Float32 payload validation and finite
  sample checks. Named-pipe transport plus a source-only worker loop are present; SDK dispatch
  remains pending.
- `VirtualMicRouteModel` now defines fixed 1/2-channel capture/reference blocks, fail-closed
  privacy mute and explicit echo-reference enablement; it intentionally does not claim AEC/NS or a
  loadable virtual capture driver.
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
- Apache-2.0 `hibiki_asio_transport_v1` now provides a fixed-layout SPSC shared-memory ring. The
  optional native ASIO DLL writes eight-channel Float32 blocks after callbacks, and
  `AsioTransportConsumerV1` creates/owns `Local\\HibikiDSP_v1_asio` for an allocation-free pop;
  `AudioEngineModel::process_asio_transport` now runs that block through the selected graph lane
  and Group Master. Physical sink/WaveRT delivery remains pending.
- `WindowsWasapiOutputV1` plus `WindowsWasapiSinkWorkerV1` now supply a Windows shared-mode
  Float32 physical render boundary: one dedicated sink-worker apartment owns COM bind/start/stop,
  event waits, bounded SPSC blocks, silence underrun fill, persistent SRC and clock-observation
  updates. The graph RT thread never calls this COM API; real-device/hotplug soak remains pending.
- `IpcNamedPipeServerV1` provides the Windows control-plane worker boundary with overlapped,
  bounded read/write, local-only pipe validation, request decoding and callback response framing;
  a Windows loopback contract test exercises Ack/request-ID round-trip.
- `driver/inf/HibikiVirtualAudio.inf` is a source-only MS-PL package template with stable Root
  hardware identity, four endpoint GUIDs and service boundary; it references only the future
  signed SYS/CAT and remains non-installable from a fresh clone.
- `process_virtual_mic_lane_v1` applies the fail-closed privacy gate before sending caller-owned
  capture blocks through the shared lane graph; it still does not claim AEC/NS or a loadable
  capture driver.

## 尚未開始

- 可載入的 WaveRT/SYSVAD-derived driver、ASIO physical sink delivery、out-of-process VST3 SDK
  plugin dispatch、WinUI 3 UI、physical sink clock fixtures 與 signed package delivery。
- ISO 226:2023 合法係數來源與正式 conformance oracle（公式本身已完成，但係數資料仍待
  授權／法務確認）。
- Microsoft driver signing、Gumroad release artifact 與 production installer。

目前開發機是 Windows build 22631、VS 17／SDK 10.0.26100.0，低於鎖定的 driver 目標
Windows 26100+、VS 2026／SDK-WDK 10.0.28000.2526；因此 user-space tests 可通過，但
不能把本機結果當成 driver-target evidence。

## 最近驗證

初始 foundation evidence 已寫入 `evidence/0000-foundation/initial.json`，目前對應
Windows volume/device、ISO formula、recovery、driver control-core/INF template、persistent SRC、
VST worker、control pipe/payloads、session volume adapter、sink clock pipeline、optional native
ASIO transport/ring、tab/Virtual Mic lane adapter、session-route/output-handoff 與 control-model
baseline commit `60b7a01`；
新 AI 接手時仍必須確認
working tree 與該 scope 是否一致。

目前驗證摘要：`verify.ps1` 的 1 個 CTest 通過；`docs-check.ps1` 的 43 個必要入口與
9 份 Spec 通過；`source-policy.ps1` 掃描 196 個路徑且無 blocked binary/secret；
`extension-check.ps1`、`installer-check.ps1`、`control-model-check.ps1` 與
`distribution-check.ps1` 與 `driver-source-check.ps1` 通過；18 個 JSON 檔案均可解析。以本機 pinned ASIO SDK
另行執行的 optional CMake target `hibiki_asio_native` unsigned build 亦通過；該輸出只在
`.local/`，未提交或發布。`doctor.ps1 -CheckOnly` 明確顯示本機低於鎖定的 Windows/VS/WDK
版本，因此沒有把 driver、HLK、簽章或真實 endpoint 結果誇大為已驗收。
