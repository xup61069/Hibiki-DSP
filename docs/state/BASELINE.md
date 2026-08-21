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
  into one bounded snapshot frame without introducing COM or RT work. The remaining platform
  step is to feed this publisher from a worker-owned Windows endpoint enumeration.
- `DeviceCatalogSnapshotStoreV1` now serializes complete control-plane snapshot publication and
  replies, rejecting empty or invalid frames while retaining the previous safe snapshot. The
  Windows `PhysicalDeviceCatalogServiceV1` joins this store to worker refresh transactions; it
  never performs COM work from the IPC reply callback and commits catalog state only after wire
  publication succeeds.
- `WindowsPhysicalDeviceCatalogWorker` now owns the COM enumerator on a worker thread, maps
  render/capture state, friendly names, mix format and device period into a candidate catalog,
  and commits only after snapshot encoding succeeds. `WindowsPhysicalDeviceCatalogCoordinator`
  bridges watcher notifications to worker polling; `DeviceCatalogRequest` now has an explicit
  snapshot-reply provider path. Live target/driver enumeration remains unverified here.
- The opt-in `tools/live-device-catalog-check.ps1` probe now built and ran the worker against the
  local `IMMDeviceEnumerator`: 14 endpoints were enumerated, sequence 1 and a 5,840-byte snapshot
  decoded successfully. It prints counts only; this is local Windows 22631 evidence, not target
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
- Parameterized ISO 226:2023 SPL-from-phon formula using caller-supplied legal parameters;
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
  VST3 SDK plugin host、WinUI 3 SDK/build and accessibility validation、physical sink clock
  fixtures 與 signed package delivery。
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

目前驗證摘要：`verify.ps1` 的 1 個 CTest 通過；`docs-check.ps1` 的 75 個必要入口與
17 份 Spec 通過；`source-policy.ps1` 掃描數量以最新 gate 輸出為準且無 blocked
binary/secret；
`extension-check.ps1`、`installer-check.ps1`、`control-model-check.ps1`、`winui-shell-check.ps1` 與
`distribution-check.ps1`、`driver-source-check.ps1` 與 `driver-signability-check.ps1` 通過；34 個 repository JSON 檔案均可解析。C++/C# DeviceSwitch
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
只在 `.local/`。`doctor.ps1 -CheckOnly` 明確顯示本機低於鎖定的 Windows/VS/WDK 版本，因此
沒有把 driver、HLK、簽章、真實 endpoint 或第三方 plugin certification 結果誇大為已驗收。C++
與 C# grouped-volume payload round-trip、legacy payload compatibility、selected group resolver
及 custom Scene card mirror 的 JSON save/load、atomic replace、malformed rollback 亦已通過本機
contract/control-model checks。
本次 control-status-snapshot additions 的 source commit 是 `e97fb90`；
session-route health 接入與避免同端點重綁的最新 source commit 是 `5f8dbcb`；
control-model route-health／volume-safety additions 的 source commit 是 `7d43e67`，
對應 handoff/evidence 更新 commit 是 `e13cfd8`；最後一次 live session evidence 更新是
`2ba5299`。
