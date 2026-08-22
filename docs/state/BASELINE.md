# Hibiki DSP baseline

## 已完成（有 commit 與 evidence）

- AI handoff now has a short canonical entry, Git-ancestry/document gate and source-only local
  preview command. A Compatibility Preview builds and completes a launch smoke on the non-target
  host using the same `EasyControlViewModel`; the formal WinUI XAML build now completes on a
  VS2026-class host (`visual-studio-msbuild` + `/restore`, 0 warnings/errors) and its shell
  launches with a UIA-recorded observable control tree, while hardware soak still awaits the
  locked target toolchain.
- The explicit `WinUICompat` target now skips the non-packaged `XamlControlsResources` merge that
  crashed during startup on the local host. With Microsoft Windows App Runtime 1.7 x64 installed,
  `tools/build-preview.ps1 -Target WinUICompat -SmokeTest` now keeps a `WinUI Desktop` window alive;
  this is a compatibility launch check only, not formal XAML/accessibility evidence.
- A C++ Engine Preview now owns the local control named pipe and passes a cross-process v1 Hello/Ack
  plus ControlStatusSnapshot smoke; the status exposes four conservative route states and the
  canonical volume mirror. `tools/run-preview.ps1 -Build` uses the runtime-aware `Ui=Auto` launcher:
  it selects WinUICompat when Windows App Runtime 1.7 is available and otherwise uses the self-contained
  Desktop Compatibility UI. It is deliberately driver-free and keeps the physical sink disabled by
  default; explicit `--enable-wasapi-output` binds the existing shared-mode worker and publishes
  `main-output` readiness without claiming driver or full playback evidence.
- The C# `EasyControlViewModel` now refreshes ControlStatus after acknowledged volume and Scene
  commands. `tools/control-model-engine-smoke.ps1` proves −18 dB/generation readback and Game
  One-Tap SceneApply across the real named pipe; this remains a user-space control proof only.
  Evidence is recorded in `evidence/0000-foundation/control-model-engine-v1.json`.
- `tools/live-system-volume-check.ps1 -WriteTest` now starts Engine Preview with
  `--enable-system-volume`, sends a volume notification through named-pipe IPC, verifies
  approximately −3 dB on the local endpoint via broker readback, and restores the original dB/mute
  state. It is opt-in user-space write-through evidence, not driver or WaveRT proof.
- `tools/live-session-volume-check.ps1 -WriteTest` now starts Engine Preview with session routing,
  creates an inaudible shared-mode test session, discovers it through the bounded catalog, sends a
  generation-scoped session handle through IPC/control queue/COM worker, verifies approximately −3 dB
  readback, and restores the original dB/mute state. This closes the target-session COM readback
  boundary; it also sends a bounded `SessionRouteCommand` and requires route catalog `Ready`. It
  remains user-space control evidence; physical per-App capture/re-send and DSP delivery are still
  unverified.
- The control-model Engine Preview smoke now exercises the full IR prepare → Scene IR clear
  round-trip and retries temporary fixture cleanup for bounded transient Windows file-indexer
  locks. Three consecutive session-routing runs are recorded in
  `evidence/0000-foundation/control-model-engine-ir-clear-v1.json`; this remains a user-space
  reliability proof only.
- Desktop Compatibility Preview now exposes a scene selector, route-health summary and volume
  origin/actuator text; scene selection is disabled until the engine is connected and remains
  command/Ack/status-refresh based. While connected it polls the bounded ControlStatusSnapshot once
  per second (coalesced while a command is busy) so external engine/Windows-volume changes can be
  reflected without touching the audio thread. It also displays the local render/capture catalog
  counts and default-render metadata; physical sink activation remains an explicit Engine Preview
  opt-in and switching/playback evidence remains out of scope.
- Both the formal WinUI source shell and Desktop Compatibility Preview now expose the bounded IR
  phase policy: Game minimum-phase/0 ms, Balanced mixed-phase/80 ms maximum, Movie linear-phase/
  160 ms maximum and Bypass. The fixed command now reaches Engine Preview and attaches the prepared
  IR to the user-space graph through an explicit prepare/commit transaction; no physical-sink or
  audible-device claim is made.
- The control-plane now decodes bounded RIFF/WAVE IR files (IEEE Float32 and signed PCM16/24/32),
  rejects malformed/non-finite/oversized input, and prepares a channel-major `IrConvolverV1` bank
  without file I/O on the RT thread. This is a file/import contract only; it does not derive
  minimum/mixed/linear kernels or prove physical-sink playback. Evidence:
  `evidence/0000-foundation/ir-wav-decoder-v1.json`.
- `build_ir_phase_kernel_v1` now performs the bounded control-plane phase transform: real-cepstrum
  minimum-phase reconstruction and source-magnitude causal linear-phase targeting for mixed/linear
  strength. Tests cover delayed impulses, independent channels, source-strength zero and Bypass
  fail-closed behavior. `IrPrepareCommand v1` now carries a bounded local path; C# and Desktop
  Compatibility Preview send it to Engine Preview, which reads/decodes/prepares on its control
  worker, calls `AudioEngineModel::prepare_ir` and commits the attachment before ACK. The RT render
  applies the immutable fixed-capacity convolver before Group Master/limiter; physical sink playback
  and device/driver evidence remain pending.
- SceneApply now prepares and commits an IR clear in the same control transaction because
  `SceneProfileV1` does not yet carry a file/reference. A previously prepared calibration cannot
  silently survive a scene switch; a new IR must be explicitly prepared afterwards.
- 公開 monorepo 文件、component license map 與 source-only paid-release policy。
- AI 接手規則、fresh-clone 流程與 source-only policy。
- `hibiki_driver_control_transport_v1` now provides a fixed 136-byte little-endian
  endpoint-state/volume-notification packet ABI. The GPL `DriverVolumeLinkV1` decodes it,
  suppresses registered event contexts and applies requested dB/mute through the canonical
  output-group safety path; its 16-byte header-only Hello/Ack/Error request-correlation path
  is also contract-tested. This remains source/control evidence and does not claim a loadable
  or signed driver.
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
- `SessionCatalogSnapshot` v1 now publishes a bounded App/session selection list through the
  worker-owned route coordinator. Generation-scoped ephemeral handles, safe metadata fallback,
  C++/C# codec/store/handler correlation and stale ViewModel replacement are covered; this is
  still a selection boundary rather than proof of physical per-App delivery.
- The source-only WinUI Expert view now renders the safe App session catalog, sequence and route
  state without exposing raw Windows session identity; App volume/routing commands use validated
  generation-scoped handles, while physical per-App capture/re-send remains unverified.
- `SessionVolumeCommand` v1 now carries only a generation-scoped handle, catalog sequence, dB
  and mute. C++/C# codecs, EngineControl callback, Windows runtime/coordinator stale guards and
  the C# ViewModel command builder are covered; the opt-in live session-volume probe now verifies
  IPC/control-queue delivery, target-session COM readback and restoration through the Engine Preview
  worker. Physical per-App capture/re-send remains unverified.
- Active catalog entries now opportunistically expose worker-read `ISimpleAudioVolume` dB/mute
  availability; expired/inactive/unreadable sessions remain visible with volume unavailable.
- `SessionRouteCommand` v1 now carries only handle/sequence/lane/output labels. The coordinator
  builds and validates a candidate registry/graph before commit, increments generation on success,
  and republishes status/catalog. The live Engine Preview probe now verifies the queued command and
  `Ready` catalog readback; physical process-loopback delivery remains unverified.
- Session volume/route runtime adapters now validate and enqueue from the EngineControl thread;
  a fixed 64-slot SPSC `SessionCommandQueueV1` is drained only by the COM worker after refresh.
  Direct synchronous read/write APIs still fail closed with `RPC_E_WRONG_THREAD`, while normal UI
  commands no longer touch Windows session COM objects from pipe/control callbacks.
- Expert source UI now allows selecting an App catalog entry and entering lane/output labels;
  it also mirrors available per-App dB/mute into bounded controls and sends a separate session
  volume command; disconnected, unavailable or stale submission remains visibly fail-closed.
- `SessionRouteRuleCommand` v1 now provides fixed 480-byte Upsert/Remove/Clear operations with
  bounded printable UTF-8 matchers, priority, makeup gain and gain-owner semantics. C++/C# codec,
  EngineControl callback, COM-worker queue handoff and candidate rule-store/route-graph transaction
  are contract-tested; physical active-session delivery remains unverified.
- Expert control model now provides a bounded 64-entry per-App route-rule catalog with atomic
  JSON persistence, stable priority ordering, validation/rollback on malformed files, and WinUI
  editor bindings. It can build SPEC-0023 commands only after a non-zero App catalog sequence;
  local save without an engine sync is explicitly shown as not yet applied. Selecting a session
  now previews the same case-insensitive App ID/name resolver as the engine; equal-priority
  ambiguity is fail-closed and never silently chooses a rule.
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
- Engine Preview now has an explicit `--enable-system-volume` opt-in: it binds the current default
  render endpoint's `IAudioEndpointVolume`, mirrors external dB/mute notifications into Main Group
  Master, and writes UI volume requests back with the UI event-context GUID. The default Preview
  remains non-mutating; status-only broker smoke coverage does not send a volume command.
- Engine Preview now has an independent `--enable-session-routing` opt-in: it binds the current
  default render endpoint's `IAudioSessionManager2`, publishes a bounded `SessionCatalogSnapshot`,
  and drains App volume, lane/output and route-rule commands through the COM-worker-owned fixed
  queue. Desktop Compatibility Preview exposes the catalog and explicit Expert controls without
  raw Windows identity. This proves the selection/command and Windows session-volume control-plane
  boundary only; physical per-App capture/re-send and DSP delivery remain explicitly unverified.
- Engine Preview now has an independent `--enable-wasapi-output` opt-in: it resolves the active
  default render descriptor from the physical catalog, starts the existing dedicated shared-mode
  WASAPI sink worker, and projects its `Pending/Ready/Degraded` state into `main-output`. The normal
  launcher remains sink-disabled; the opt-in is a user-space output-boundary smoke only, with no
  claim of WaveRT, full graph playback, per-App delivery or target-device soak.
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
  into one bounded snapshot frame without introducing COM or RT work. Engine Preview now feeds
  it from a COM-initialized, worker-owned Windows endpoint enumeration at startup and polls the
  watcher for metadata changes; the default path still does not open a physical sink, while the
  explicit WASAPI opt-in uses the same catalog to start the dedicated shared-mode worker.
- `DeviceCatalogSnapshotStoreV1` now serializes complete control-plane snapshot publication and
  replies, rejecting empty or invalid frames while retaining the previous safe snapshot. The
  Windows `PhysicalDeviceCatalogServiceV1` joins this store to worker refresh transactions; it
  never performs COM work from the IPC reply callback and commits catalog state only after wire
  publication succeeds.
- `WindowsPhysicalDeviceCatalogWorker` now owns the COM enumerator on a worker thread, maps
  render/capture state, friendly names, mix format and device period into a candidate catalog,
  and commits only after snapshot encoding succeeds. `WindowsPhysicalDeviceCatalogCoordinator`
  bridges watcher notifications to worker polling; `DeviceCatalogRequest` now has an explicit
  snapshot-reply provider path. Engine Preview binds this service and exposes the local catalog
  to the Desktop Compatibility Preview; target 24H2/driver/hotplug soak remains unverified.
- The opt-in `tools/live-device-catalog-check.ps1` probe now built and ran the worker against the
  local `IMMDeviceEnumerator`: 14 endpoints were enumerated, sequence 1 and a 5,840-byte snapshot
  decoded successfully. It prints counts only; this is local Windows 26200 evidence, not target
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
- The C# wire/model boundary now accepts one Active default per flow, including capture; only
  Active render entries remain selectable. This matches Windows endpoint metadata and prevents a
  valid default capture endpoint from invalidating the entire snapshot.
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
- `WindowsAudioSessionRouteCoordinatorV1` now exposes bounded per-session volume read/write
  control through `ISimpleAudioVolume`, requiring a currently enumerated ephemeral session
  instance ID and finite −144…+12 dB input. Runtime/coordinator unbound, unknown, stale and
  invalid requests fail closed; this is a worker control boundary, not physical per-App rerouting.
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
- Issue #394 已在本機 26100 家族 WDK 工具鏈完成第一次 kernel-mode WaveRT PortCls adapter 建置：
  tools/build-driver.ps1 動態選擇最新含 km headers 的 kit、以核心模式編譯 portable C cores
  與 driver/wdk/**、連結 HibikiVirtualAudio.sys，INF 複製後由 genuine Inf2Cat 產生 .cat，
  並以 driver-signability-check.ps1 -PackageRoot .local/driver-package -RequireInf2Cat 重驗
  封裝（evidence/0000-foundation/driver-sys-build-v1.json）。輸出全部留在 ignored .local/；
  這是 build/package/signability evidence，仍無安裝、載入、runtime audio、HLK 或 Microsoft
  signing 宣稱。
- Issue #433 已在本機完成 WaveRT adapter 的測試簽章封裝 evidence：以 .local/certs 下自建的
  self-signed code-signing certificate（PFX/CERT 留在 ignored .local/certs，私密金鑰不進 Git）
  以 signtool /fd SHA256 同時簽署 HibikiVirtualAudio.sys 與 .cat，signtool verify /pa 如預期
  停在 untrusted root（evidence/0000-foundation/driver-load-test-v1.json）。憑證匯入
  Trusted Root／TrustedPublisher、TESTSIGNING 開機旗標與 pnputil 安裝仍是需要使用者同意的
  下一步；此 evidence 不宣稱 endpoint 出現或任何音訊行為。
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

toolchain lock 已依 ADR-0005 對 SDK/WDK 改採最低基線 >= 10.0.26100：目前開發機是
Windows build 26200、VS 2026／SDK-WDK 26100 家族，符合基線。user-space tests 可通過，
但本機結果仍不是 driver 安裝、載入、runtime audio、HLK 或簽章的 target evidence。

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

VST3 自動化時間軸資料鏈已合併（皆為 user-space source contract）：
`9f5f02e` 加入 supervisor 端 bounded `Vst3TimelineEditorV1` 編輯交易（draft/commit/
discard，upsert 以同一 (parameter, position) 取代）；`8a041a2` 把編輯交易綁進
`Vst3SceneAutomationSchedulerV1`（begin/commit/cancel + `timeline_snapshot` 讀回，
編輯中拒刪 slot）；`ff46e15` 提供 canonical fail-closed JSON 持久化
（`vst3-parameter-timeline-v1.schema.json`，64 KiB 上限、逐位元組穩定 round-trip）；
`dbafc4f` 加入 fixed-capacity per-timeline 檔案儲存 `Vst3TimelineFileStoreV1`
（嚴格檔名安全 ID、Windows 保留名拒絕、temp-write-then-replace）；
`9cca03d` 加入 store→scheduler 的 `sync_timeline_store_to_scheduler_v1`
（單項失敗計 skipped 不中斷）；`b7e2ea8` 加入唯讀內省
（排序 `timeline_ids` 與不可變 binding views）。UI 編輯介面、side-chain/multi-bus
worker process 與第三方 certification 仍待完成；以上不宣稱任何實體音訊或 driver 能力。

VST3 supervisor 端 selection-aware 編輯 surface 已合併（Issue #351 / PR #377）：
`Vst3TimelineSupervisorSurfaceV1` 把 `Vst3TimelineEditorV1` 和非擁有的
`Vst3TimelineFileStoreV1` handle 組合成單一 fail-closed facade；attach/detach、
select(id)、編輯轉發與 save_selected() 在未 attach 或未選取時一律拒絕，dirty
狀態由已發布 snapshot 與載入/儲存 baseline 比較推導。證據為
`evidence/0000-foundation/vst3-supervisor-surface-v1.json`；SPEC-0008 已加入對應小節。
這是 headless 控制面契約，不宣稱 UI 編輯器或實體音訊能力。

流程 gate 本週新增：#21 讓 BASELINE 摘要計數 fail-closed 對照 git 實測；
#51 把 docs-check 改成 merge-ref 感知（PR 未動 BASELINE 時容忍自身 tracked/JSON
漂移、push-to-main 與本機維持嚴格），並附 `-SelfTest`；#25 讓 active handoff 的
scope_globs 重疊直接 fail-closed。gate 腳本需要 PowerShell 7（PS 5.1 無法執行）。

SDK／tooling 增量已合併：C# control model 加入 Vst3TimelineSurfaceModelV1 surface model、
binding-state notifications 與 observable timeline editor view model（含 RemoveSelectedRow
與 undo-after-remove coverage）；supervisor surface 轉發 bounded history introspection 與
ClearHistory()；sandbox 啟動失敗以 redacted diagnostic reason codes／incident summary 記錄。
對應 evidence 有 vst3-timeline-surface-model-v1.json、vst3-timeline-binding-state-v1.json、
vst3-timeline-editor-viewmodel-v1.json、vst3-timeline-editor-remove-selected-row-v1.json、
vst3-timeline-remove-selected-row-undo-v1.json、vst3-supervisor-history-introspection-v1.json、
vst3-timeline-clear-history-v1.json 與 vst3-sandbox-redacted-diagnostic-v1.json。SDK 邊界同步
fail-closed 收緊：ASIO shared-memory transport 拒絕非零 reserved bytes；driver-stream packet
拒絕零 sequence/generation；driver-control 拒絕零 request ID。工具面增量：handoff-check 改用
UTF-8 編碼、extension gate 加入 tabCapture owner guard 與 tab-only media constraints、
driver-source-check 接受兩代 PortCls notification-buffer naming、verify workflow 以 concurrency
group 取消被取代的 run。以上皆為 user-space/source evidence，不新增實體音訊或 driver 載入宣稱。

第二波 tooling／UI 增量已合併：build-driver.ps1 改為從腳本位置錨定 repo root、把 obj/package
輸出固定在 .local 內並加 containment 檢查，新增 -SelfTest 驗證 root 探索、輸出範圍與 source
boundary（Issue #444 / PR #445）；handoff-check 可解析 TBD pre-claim draft 並驗證其 issue/branch
欄位（newline-stable self-test，Issue #439 / PR #442）；WinUI MainWindow.xaml 改用 Mica BaseAlt
backdrop、Fluent theme 筆刷與 type ramp styles（含 AccentButtonStyle）取代硬編碼色彩
（Issue #438 / PR #443）。皆為 source/tooling evidence，不宣稱正式 XAML build 或視覺驗收。

第三波 tooling／UI 增量已合併：WinUI shell 完成標題列整合——48px drag-region 列含 app
icon/name 固定在左、connection-status pill（圓角 Border + theme brushes）與連接按鈕固定在右
且不與 caption buttons 重疊，ExtendsContentIntoTitleBar 由 code-behind 接線（完整 shell 限定，
Desktop Compatibility Preview 維持自己的 chrome），標題改用 Fluent type ramp
（Issue #446 / PR #450）；verify.ps1 -Clean 在刪除前 fail-closed 驗證 target 必須完全符合
repo-local build root、必須是真實目錄且 target／parent 都不是 reparse point，self-test 涵蓋
mismatch/reparse 拒絕（Issue #448 / PR #452）。皆為 source/tooling evidence。

目前驗證摘要：`verify.ps1` 的 3 個 CTest（contract_tests、asio_transport_selftest、tab_bridge_selftest）通過；`docs-check.ps1` 的 80 個必要入口與
24 份 Spec 通過；`source-policy.ps1` 掃描 tracked paths 且無 blocked
binary/secret；volatile 計數（tracked paths、repository JSON）由 docs-check 即時量測；
`extension-check.ps1`、`installer-check.ps1`、`control-model-check.ps1`、`winui-shell-check.ps1` 與
`distribution-check.ps1`、`driver-source-check.ps1` 與 `driver-signability-check.ps1` 通過；repository JSON 檔案均可解析。C++/C# DeviceSwitch
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
只在 `.local/`。ADR-0005 改採 >= 10.0.26100 最低基線後，本機 `doctor.ps1 -CheckOnly` 已通過；
本機結果仍不把 driver 安裝／載入、HLK、簽章、真實 endpoint 或第三方 plugin certification
結果誇大為已驗收。C++
與 C# grouped-volume payload round-trip、legacy payload compatibility、selected group resolver、
VST3 timeline editor 交易、C# CalibrationModel 資料契約／PEQ 編譯器
及 custom Scene card mirror 的 JSON save/load、atomic replace、malformed rollback 亦已通過本機
contract/control-model checks。
本次 CalibrationModel C# control model 與 bounded PEQ compiler 的 source commit 是 `5ef674f`；
本次 Vst3TimelineEditor supervisor-side parameter timeline editing transaction 的 source commit 是 `9f5f02e`；
本次 live SessionRouteRuleCommand upsert/remove readback transaction probe 的 source commit 是 `d1ad93e`；
本次 live Engine Preview system volume write-through IPC probe 的 source commit 是 `c42cabd`；
本次 Expert per-App volume controls 的 source commit 是 `ebb80a3`；
本次 Session command worker queue 的 source commit 是 `6f9d6b1`；
本次 App route selection controls 的 source commit 是 `d4862d9`；
COM worker-thread guard 的 source commit 是 `cbc860e`；
SessionRouteCommand graph boundary 的 source commit 是 `662abbb`；
本次 session catalog volume availability projection 的 source commit 是 `b1538b1`；
SessionVolumeCommand handle boundary 的 source commit 是 `6c4a8b7`；
本次 WinUI App session catalog projection 的 source commit 是 `66f6298`；
ephemeral App session catalog additions 的 source commit 是 `68cf466`；
本次 control-status-snapshot additions 的 source commit 是 `e97fb90`；
Engine Preview opt-in Windows system-volume link、safe default launcher 與 status-only smoke 的 source commit 是 `6a04764`；
session-route health 接入與避免同端點重綁的最新 source commit 是 `5f8dbcb`；
volume broker unchanged-endpoint result 修正的最新 source commit 是 `ca8ea40`；
volume node 與 session-route 獨立重綁的最新 source commit 是 `ef4af32`；
control-model route-health／volume-safety additions 的 source commit 是 `7d43e67`，
對應 handoff/evidence 更新 commit 是 `e13cfd8`；最後一次 live session evidence 更新是
`2ba5299`；Engine Preview opt-in session-routing vertical slice、Desktop Compatibility Expert
controls 與 smoke evidence 的 source commit 是 `d668982`。
