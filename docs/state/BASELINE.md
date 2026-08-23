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

第四波之後的增量已合併：SceneProfileV1 支援在相符場景切換時保留 referenced IR——schema
新增 ir reference 形狀，hub contracts/engine 驗證並在 engine control 更新中攜帶 referenced
IR identity，contract tests 涵蓋保留行為，evidence 記錄於 scene-ir-reference-v1.json
（Issue #423 / PR #461）；所有 accepted ADR 補上結構化 frontmatter 且 docs-check 強制
檢查 ADR frontmatter 欄位（Issue #453 / PR #459）；run-preview.ps1 在 Start-Process 前
fail-closed 驗證 engine/UI launch target 必須位於 repo .local root 內，拒絕 root 外路徑、
reparse point 與非檔案目標，-SelfTest 新增五個 launch-target 安全案例
（Issue #455 / PR #457）。皆為 user-space/source evidence。

第六波 tooling 增量已合併：winui-a11y-smoke.ps1 在 Start-Process 前 fail-closed 驗證
supplied WinUI accessibility smoke 輸出目錄必須位於 repo .local root 內、為真實目錄且
parent 安全，Hibiki.WinUI.exe 必須位於該輸出目錄之下、為真實檔案且 target／祖先皆非
reparse point；-SelfTest 從 4 個案例擴充至 11 個離線案例，不寫檔、不載入
UI Automation、不啟動程序（Issue #470 / PR #472）。皆為 tooling/source evidence。

第七波 tooling 增量已合併：build-engine-preview.ps1 在 CMake configure 前與 build
命令前雙重驗證固定 .local/engine-preview build root，拒絕 root 外路徑、既有
reparse point 祖先／target 與非目錄 build root，同時保留正常 missing-root 行為；
-SelfTest 從 5 個案例擴充至 7 個離線案例，不呼叫 CMake 也不寫檔（Issue #474 /
PR #477）。皆為 tooling/source evidence。

第八波 docs／tooling 增量已合併：engine-preview-smoke.ps1 在啟動 Engine Preview
執行檔或寫入暫存 IR WAV 前新增離線 path-safety 驗證，wrapper self-test 擴充且不啟動
程序、不呼叫 CMake、不寫 repo 輸出（Issue #482 / PR #483）；README 工具鏈文字對齊
accepted ADR-0005（SDK/WDK >= 10.0.26100 最低基線），並把 V1 gap/milestone 快照更新為
已合併的本地 kernel-mode PortCls adapter .sys build、Inf2Cat packaging 與 self-signed
test-sign evidence，同時明確保留未宣稱的安裝／載入、runtime audio、HLK、Microsoft
signing 與 consumer release 界限（Issue #478 / PR #480）。

第九波 tooling／retention 增量已合併：control-model-engine-smoke.ps1 對固定 Engine
Preview 執行檔、工作目錄、project path 與 .local/control-model-engine-smoke 輸出 root
新增 fail-closed 驗證，build/launch 前驗證並在 dotnet run 前重新驗證 project/output
路徑，保留既有 missing output-root 建立行為，launcher self-test 以離線合成路以離線合成路徑案例
擴充（Issue #489 / PR #490）；另移除切片合併後殘留的 docs/tasks/active/423.md 與
docs/tasks/active/466.md handoff 檔案，依 Issue #341/#346 前例新增
retention-final-v2.json evidence（Issue #485 / PR #487）。

第十波 tooling 增量已合併：docs-check.ps1 新增 markdown 相對連結驗證——target 缺失時
fail-closed，URL、anchor 與 fenced code block 略過，-SelfTest 加入離線連結案例
（Issue #496 / PR #497）；live-audio-session-check.ps1 對固定 build root 與 probe path
新增 fail-closed 驗證，拒絕 root 外路徑、既有 reparse point 祖先／target 與非目錄／
非檔案形狀，保留 missing build root 建立行為，wrapper self-test 擴充且不呼叫 CMake、
不執行 probe、不寫 repo 檔案（Issue #492 / PR #494）。皆為 tooling/source evidence。

第十一波 tooling 增量已合併：live-process-loopback-check.ps1 對固定 build root 與
probe path 新增 fail-closed 驗證，拒絕 root 外路徑、既有 reparse point 祖先／target 與
非目錄／非檔案形狀，保留 missing build root 建立行為，wrapper self-test 擴充且不呼叫
CMake、不執行 probe、不寫 repo 檔案（Issue #500 / PR #502）；docs-check 的 markdown
相對連結閘門交付實作——先前的 PR #497 僅含空 claim commit（已記錄的 race 事件），
現對 tracked markdown 缺失 target fail-closed、略過 URL/anchor/fenced blocks、回報
檢查數量並新增六個離線 self-test 案例（Issue #496 / PR #501）。皆為 tooling/source
evidence。

第十二波 tooling 增量已合併：live-wasapi-handoff-check.ps1 對固定 build root 與
probe path 新增 fail-closed 驗證，拒絕 root 外路徑、既有 reparse point 祖先／target 與
非目錄／非檔案形狀，保留 missing build root 建立行為，wrapper self-test 擴充且不呼叫
CMake、不執行 probe、不寫 repo 檔案（Issue #506 / PR #508）；
live-system-volume-check.ps1 與 live-session-volume-check.ps1 移植既有 live-probe
path-guard 模式，在任何 build、寫入或啟動程序前 fail-closed 驗證 build roots 與
engine executable（root 內、reparse point 掃描與形狀檢查），self-test 加入離線
合成屬性案例（Issue #505 / PR #511）。皆為 tooling/source evidence。

第十三波 tooling 與 UI 增量已合併：live-device-catalog-check.ps1 在建立 build tree
與執行 probe 前，對固定 .local root 新增 fail-closed 驗證（root 內、reparse point
掃描與目錄／檔案形狀檢查），self-test 擴充十一個離線合成路徑案例，且不呼叫 CMake、
不執行 probe、不存取 endpoint 或音訊狀態、不寫 repo 檔案（Issue #512 / PR #513）；
WinUI hero card 重構為自適應雙欄 Grid——Quick Start 面板整合連線按鈕、busy
ProgressRing、一鍵增強動作與狀態 chip，視窗寬度 840px 以下自動收合回單欄，
狀態文字改用膠囊 chip，連線按鈕從標題列移入 hero 面板，32 個互動控制項的
automation names 全數保留（Issue #495 / PR #514）。皆為 tooling/source evidence。

第十四波 tooling 與 UI 增量已合併：build-preview.ps1 在每次 Start-Process 前對
preview smoke 執行檔 fail-closed 驗證，拒絕 .local 外、缺失、目錄形狀與 reparse／
非目錄祖先 target，self-test 從 11 案例擴充至 17 個離線案例，且不建置、不啟動程序、
不寫 repo 或存取機器狀態（Issue #519 / PR #522）；WinUI shell 統一頂層卡片外觀
（CornerRadius 12、一致 24px padding），場景／自訂預設／音量保護標題加入 accent
icons，requested／effective 音量讀值改用 chip 且 SafetyStatusText 維持 polite-live，
IR phase 控制項收進帶說明的 tinted 子面板（mode 文字與 added-delay 以 chip 呈現），
32 個互動控制項與必要 bindings 全數保留（Issue #517 / PR #524）。皆為
tooling/source evidence。

第十五波 tooling 增量已合併：probe-environment.ps1 在建立 .local 或寫入
.local/context.json 前，把 fail-closed path-guard 家族延伸到 root／leaf 驗證
（必須位於 repo .local 樹內、祖先為真實目錄、leaf 與祖先皆非 reparse point，
保留 missing leaf 行為），self-test 在 12 個文件案例外新增 8 個離線合成路徑案例，
不探測機器也不寫 repo；該 PR 同時完成 PR #520 空白 claim commit 提前合併的補救
並依 #497/#507 前例記錄 race 事件（Issue #518 / PR #527）；
build-driver.ps1 在 New-Item 與後續 compiler/linker/package 寫入前，對 object 與
package 輸出目錄 fail-closed 驗證，拒絕 repo .local 外輸出、reparse point
target／祖先與非目錄 target，write-free self-test 新增 10 個離線輸出路徑案例
（Issue #525 / PR #528）。皆為 tooling/source evidence。

第十六波 tooling 與 UI 增量已合併：WinUI 自訂場景表單改為自適應雙欄 Grid
（Scene ID／名稱並排、說明跨欄），視窗寬度 720px 以下自動疊回單欄；「加入自訂
預設」升級為全寬 accent 主按鈕（更高的觸控目標），頁尾提示改為呼應卡片外觀語言的
caption chip，控制模型與引擎行為不變（Issue #529 / PR #534）；
evidence/0000-foundation/probe-environment-path-guard-v1.json 補齊先前空白的
validation 陣列，記錄 Issue #518 path guard 的實際離線驗證命令與結果
（self-test 案例、docs/source/source-only 閘門與 diff check），
滿足 evidence 驗收契約（Issue #533 / PR #535）。皆為 tooling/source evidence。

第十七波 tooling 防護與文件增量已合併：control-model-check.ps1 在任何 dotnet run 前
將 fail-closed path-guard 模式延伸到自身建置輸出，專案檔必須存在，且 BaseOutputPath／
MSBuildProjectExtensionsPath 必須留在 repository root 內，拒絕語彙路徑外 target、
非目錄 target 與 reparse point target／祖先，-SelfTest 新增離線合成屬性案例，並以
evidence/0000-foundation/control-model-check-path-guard-v1.json 記錄驗證
（Issue #536 / PR #537）；verify.ps1 在 New-Item 與 CMake 使用前重新驗證固定的
.local/build target 與 .local parent，拒絕語彙路徑外 target、檔案與 reparse point，
write-free self-test 從 14 案例擴充到 22 案例（Issue #531 / PR #539）；
README 新增 opt-in live-device-catalog-check 與 live-process-loopback-check 探針文件
（匿名彙整輸出、unavailable 時如實記錄），M0 里程碑列補記 Hyper-V VM 隔離載入測試
環境建置中的主張而不誇大完成度，並新增
evidence/0000-foundation/readme-live-probe-surface-v1.json（Issue #540 / PR #545）；
probe-environment.ps1 對既有路徑的屬性檢查改為僅接受 ObjectNotFound 為
「路徑不存在」，leaf 或 parent 的其他檢查錯誤一律 fail-closed，self-test 新增
leaf／parent 兩個合成檢查錯誤案例（Issue #543 / PR #546）；build-engine-preview.ps1
同步採用 Get-Item -ErrorAction Stop 檢查模式，僅 ObjectNotFound 視為缺失，建置根或
父目錄損壞／不可存取時在 CMake 前 fail-closed，self-test 從 7 案例擴充到 9 案例
（Issue #551 / PR #552）；live-system-volume-check.ps1／live-session-volume-check.ps1
與 live-wasapi-handoff-check.ps1 同步採用「僅 ObjectNotFound 視為路徑不存在」的
檢查語意並各自新增離線拒絕案例，分別以 volume-probe-inspection-guard-v1.json 與
wasapi-handoff-inspection-guard-v1.json 記錄（Issue #554 / PR #555、
Issue #561 / PR #562）；engine-preview-smoke.ps1 對 engine executable、工作目錄與
IR 目錄／檔案套用同一檢查語意，損壞或不可存取時在啟動前 fail-closed
（Issue #559 / PR #560）；docs/ai/MULTI_AGENT.md 文件化 TBD pre-claim handoff 流程
（建立 Issue 時 issue／branch 先標 TBD，正式認領前 handoff-check 跳過該草稿）
並附 tbd-preclaim-docs-v1.json（Issue #556 / PR #558）；
WinUI Expert 界面完成卡片化：Expert expander 內容改用與其他介面一致的圓角描邊
卡片，route health／App sessions／route presets／Matrix／DSP graph／VST3 lanes／
calibration 分組進 tinted sub-panel，route health、session、Matrix gain 與 VST3
狀態讀值改用緊湊 caption chips，route preset 欄位雙欄平衡排列，
AutomationProperties 名稱全數保留（Issue #542 / PR #563）；
WinUICompat 啟動崩潰修復：MainWindow.CompatibilityPreview.cs 的七個
Application.Current.Resources 直接索引查找改為 TryGetValue-based fail-soft resolver，
缺失 framework 主題資源不再讓視窗啟動 fail-fast（0xC000027B stowed exception），
formal XAML 路徑與 DesktopCompat 行為不變（Issue #548 / PR #571）。
皆為 tooling/source/UI/docs evidence。

第十八波整合與修復增量已合併：移除 PR #564 誤提交的全部 809 個
.opencode/opencode-loop/** 檔案（loop log、session 排程狀態與 corrupt 快照），
新增根錨定 .opencode/ 的 .gitignore 規則防止復發，不重寫歷史，純衛生切片
（Issue #568 / PR #569）；WinUICompat 啟動崩潰修復的實際交付落地：
CompatibilityPreview code-behind 七個主題資源直接索引查找改為 fail-soft
TryGetValue resolver，缺失 framework 主題資源不再讓視窗啟動 fail-fast
（0xC000027B stowed exception），本機 build-preview -Target WinUICompat -SmokeTest
從必當機轉為通過，evidence winuicompat-launch-fix-v1.json
（Issue #548 / PR #571）；GitHub handoff CI 可靠性改造：PR verify 只驗證該
branch 擁有的 Issue，repository 全域 handoff 健康改由獨立 handoff-audit
workflow（事件＋排程）稽核，AI task 範本改發有效 TBD pre-claim 且不自動加
claimed，SPEC-0004 同步更新，evidence github-handoff-ci-isolation-v1.json
（Issue #565 / PR #566）。皆為 hygiene/UI/docs/CI evidence。

第十九波恢復與防護增量已合併：build-preview.ps1 與 build-driver.ps1 的既有路徑
檢查改為只把 ObjectNotFound 視為不存在，其他檢查錯誤在建置／寫入前 fail-closed，
各自補離線 self-test 案例（Issue #586 / PR #590、Issue #598 / PR #599）；
README「給 AI 協作者」的必跑 gates 清單對齊 AGENTS.md 單一真值，並把工具鏈需求
段落改為指向同一清單（Issue #591 / PR #594）；WinUI 場景卡片系統經兩次空合併後
由 recovery PR 重新落地：新增 Styles/SceneCard.xaml 共用卡片樣式（圓角、描邊、
tinted 背景、hover/pressed/focused 狀態），MainWindow 三組標題統一 Subtitle 層級、
safety badge 加無障礙標籤，32 個 automation bindings 全數保留（Issue #593 /
PR #595 空合併，Issue #603 / PR #605 re-land 完成）；live-device-catalog-check.ps1
同樣補上 fail-closed 路徑檢查（PR #597 空合併，Issue #592 / PR #601 recovery
完成，含 leaf/parent 檢查錯誤拒絕共 13 個離線案例）；VST3 supervisor surface
remove_selected() 經 PR #583 空合併遺失後由 Issue #600 / PR #602 re-land：
native 端在 detached/unselected/edit-session-open 時拒絕、成功時透過 store 移除並
清除狀態，C# Vst3TimelineSurfaceModelV1.RemoveSelected 鏡像與 ViewModel wrapper
同步，contract test 修正 list_ids 字典排序斷言（64 字元長 ID 排在最前）
（vst3-surface-remove-timeline-v1.json）；GitHub Issue intake 改為結構化表單必填：
.github/ISSUE_TEMPLATE/config.yml 停用空白 Issue、保留安全通報聯絡連結與 GitHub
官方空白 Issue 逃生口（Issue #604 / PR #606）。皆為 tooling/UI/VST3 surface/docs
evidence。

第三十三波整合增量已合併：官方 bootstrapper 新增經 manifest 與 SHA-256 驗證的交易式
user-space payload staging，失敗會回復既有安裝且不碰 `%LocalAppData%/Hibiki DSP` 使用者
資料；9 個離線功能案例涵蓋有效 staging、路徑逃逸拒絕、hash mismatch、rollback 與 backup
清理。另新增只移除 manifest 所列 payload、失敗時回復、保留使用者資料的 uninstall 路徑；
後者目前仍是 source/boundary evidence，尚未執行真實 `-Apply`／`-Uninstall`，交易式 uninstall
功能自檢由後續 Issue #785 補強（Issue #745 / PR #759、PR #770；Issue #766 / PR #777）。

driver host 工具統一 Inf2Cat 探測順序（明確 `WDK_BIN`、Windows Kits tree、PATH），本機已建置
package 可在不手動設定環境變數時重跑 signability；build-driver 使用唯一 source/object plan，
`guids.cpp` 只編譯一次，compiler 與 linker warning 都以 `/WX` fail-closed（Issue #764 /
PR #769、Issue #774 / PR #776）。另新增只讀匿名 PnP 診斷工具，只鎖定
`ROOT\HIBIKIDSP`／`HibikiVirtualAudio`，記錄 typed problem/service/driver 欄位及有界、遮蔽後的
SetupAPI 摘要到 `.local/`；AST 自檢拒絕安裝、啟停、移除裝置或修改開機設定的命令
（Issue #781 / PR #783）。上述皆為 host-side source/build/self-test 或 unavailable-path evidence，
不代表 driver 已在 guest 安裝、載入、PnP start、出聲、通過 HLK 或取得 Microsoft signing。

瀏覽器 popup 從隱藏回到可見時，會透過既有 service-worker query 重新取得實際捕捉狀態，且
不覆蓋先前需保留到下一次使用者操作的錯誤（Issue #762 / PR #773）。DesktopCompat 與
Compatibility Preview 都改用 control model 組合出的完整 route-health accessible summary，
不再只顯示名稱與短狀態（Issue #765 / PR #771）。正式 WinUI 的本機自訂 Scene 卡加入具名的
移除操作，儲存失敗會回復卡片與選取；實體輸出 picker 加入重新掃描操作，只送出既有 bounded
`DeviceCatalogRequest`，未連線、逾時、錯誤或 stale snapshot 都保留上一份清單
（Issue #767 / PR #775、Issue #779 / PR #784）。這些是 source、contract-model、source-gate
或非目標 preview evidence；未執行正式 runtime UIA／螢幕閱讀器或瀏覽器自動化，也不代表
實體音訊已送達。

Basic noise gate 在原關閉 threshold 上加入 2 dB reopening hysteresis；訊號在 threshold 附近
擺動時不再每個區塊快速重開，既有 attack/release 語意維持不變。實作保持 `noexcept`、每聲道
固定狀態且 RT thread 無配置、鎖、等待或 I/O，contract regression 與完整 verify 通過
（Issue #778 / PR #780）。這是 user-space DSP source/contract evidence，不是實體音訊量測。

第三十二波整合增量已合併：SPEC-0009 補上 popup 無障礙政策的權威文件條目，記錄 PR #744
引入並由 PR #750 擴充的 aria-label／aria-labelledby 強制檢查與 fail-closed 行為
（Issue #753 / PR #755）。winui-shell-check 新增兩個回歸防護：DesktopCompat Preview 的
互動控制項必須在 Program.cs 宣告非空 AccessibleName，正式 shell MainWindow.xaml 必須保留
IR WAV 載入按鈕及其 AutomationProperties.Name 和 OnPrepareIrClick handler，缺漏會讓 gate
fail-closed（Issue #752 / PR #757）。正式 shell route-health 卡片將控制模型的完整無障礙
摘要投射給輔助技術而非僅視覺片段，Expert 狀態變更改用 polite live region 公告
（Issue #756 / PR #758）。前者為文件 evidence，後者分別為 UI source-gate 與 source-only
accessibility projection evidence。不宣稱正式 XAML/accessibility runtime audit、螢幕閱讀器
runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。

第三十一波整合增量已合併：Desktop Compatibility Preview 的 14 個互動控制項全部補上非空
AccessibleName，涵蓋 session 選擇器、音量滑桿、路由欄位、場景與 IR 控制以及本機建立的
output-group 下拉和連線按鈕；名稱沿用可見的繁中介面用語，Release build 與啟動 smoke 通過
（Issue #747 / PR #749）。extension popup gate 追加 aria-labelledby 目標解析檢查：引用 ID
必須存在於 popup markup 且解析到非空文字，壞引用即使同時提供 aria-label 也會 fail-closed，
離線自檢覆蓋有效引用、缺失目標與空文字目標（Issue #748 / PR #750）。前者為 compat preview
source/build/launch evidence，後者為 extension source/policy evidence。不宣稱正式
XAML/accessibility runtime audit、螢幕閱讀器 runtime automation、實體音訊、driver
安裝/載入/HLK 或 Microsoft signing。

第三十波整合增量已合併：瀏覽器單分頁捕捉的 Start／Stop 失敗訊息保留到下一次使用者操作，
不會被立即的狀態重繪蓋成 Idle；extension gate 同步要求 status.dataset.error 邊界並在
自檢中驗證（Issue #724 / PR #727）。Compatibility Preview 開放與正式 shell 相同的
「刪除目前時間軸」動作，重用 fail-closed ViewModel handler 並帶無障礙名稱（Issue #723 /
PR #729）。offscreen natural-end release gate 追加 handler 定義移除時 fail-closed 的
自檢，避免 ended listener 靜默指向未定義函式（Issue #733 / PR #735）。winui-shell-check
掃描 Compatibility Preview C# 控制建構的 AutomationProperties.Name，缺漏或空值會讓
檢查失敗（Issue #732 / PR #734）。分別為 extension source/policy evidence、compat preview
source/build/launch evidence 與 UI source-gate evidence。不宣稱正式 XAML/accessibility
runtime audit、瀏覽器 runtime automation、實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。

第二十九波整合增量已合併：Compatibility Preview 的 MRT 紀錄修正為 fail-soft 邊界——
WinUICompat 建置不啟用 Core MRT 資源工具，主題資源走既有 TryGetValue 回退路徑，
預覽以無樣式但可啟動的方式執行；審計文字同時澄清 VS2026 Appx packaging tasks 存在於
本機而 dotnet CLI build 不會載入它們（Issue #703 / PR #709、Issue #720 / PR #721）。
瀏覽器單分頁捕捉的 popup Start／Stop handler 攔截訊息通道錯誤，顯示實際錯誤並重新
查詢真實捕捉狀態後才恢復控制項（Issue #702 / PR #704）；後續修正讓失敗文字在背景狀態
刷新時保留，直到使用者下一次操作才更新（Issue #724 / PR #727）。extension gate 強制
popup 檢查 response.ok、如實回報錯誤，並要求以 error 標記保護此訊息；自檢在缺少任一
邊界時 fail-closed（Issue #715 / PR #719、Issue #727）。
offscreen 在來源串流自然結束時釋放捕捉 graph、通知 service worker 並自動關閉 document，
不需要使用者手動停止（Issue #706 / PR #707、Issue #714 / PR #716）。SPEC-0009 記錄失敗回應與
stream-ended 邊界，README 補上捕捉生命週期與 bridge 狀態說明；compat preview smoke 改為從
乾淨輸出目錄執行，避免殘留 binary 造成假通過（Issue #710 / PR #711、Issue #712 / PR #718、
Issue #713 / PR #717）。extension/UI 項目分別為 extension source/policy evidence 與 compat
preview source/build/launch evidence。不宣稱正式 XAML/accessibility、實體音訊、driver
安裝/載入/HLK 或 Microsoft signing。
第二十八波整合增量已合併：瀏覽器單分頁捕捉的 offscreen start／stop listener 保留非同步
回應通道，成功啟動不再因回應通道提早關閉而被誤報為失敗（Issue #681 / PR #692）。
Compatibility Preview 的 WinUICompat target 恢復編譯與啟動 smoke，隨後清除暫時 dead-code
TextBox seam 且無運行行為變化（Issue #687 / PR #694、Issue #697 / PR #699）。每個註冊 output sink 現在擁有獨立 TruePeakLimiterV1 狀態，一個輸出
群的尖峰不會壓低另一群後續安靜區塊；graph commit 仍重置所有 limiter（Issue #683 / PR #695）。
Engine Preview 新增有界 opt-in soak harness，離線 SelfTest 覆蓋參數邊界、IPC frame、聚合結果
與清理決策；預設三循環 smoke 以 Hello/Ack 加 Main volume 往返驗證並產生匿名報告（Issue #672 /
PR #685）。extension/UI/DSP 項目分別為 source、policy/build/contract evidence；soak 為本機
user-space process evidence。不宣稱正式 XAML/accessibility、實體音訊、driver 安裝/載入/HLK 或
Microsoft signing。
第二十七波整合增量已合併：TruePeakLimiterV1 在 graph commit 時重置回 unity gain，
前一個 graph 累積的恢復衰減不會延續到新 graph 的安靜段落；新 graph 中超過上限的
峰值仍然立即衰減（Issue #678 / PR #678）。瀏覽器單分頁捕捉的 start/stop 回應改為
反映真實成敗：成功啟動不再被誤報為失敗，失敗會帶回實際錯誤，policy gate 同步
涵蓋回應邊界（Issue #681 / PR #684）。皆為 user-space source、contract test、
extension source 與 policy gate evidence；不宣稱實體音訊、driver 安裝/載入/HLK 或
Microsoft signing。
第二十六波整合增量已合併：WinUI Expert shell 開放本機 VST3 時間軸編輯器，
支援時間軸選取、草稿開始／提交／捨棄、復原／重做與事件增刪／數值編輯，
Compatibility Preview 補上相同的選取 seam（Issue #667 / PR #669、Issue #673 /
PR #677）。瀏覽器單分頁捕捉新增使用者控制的 Stop，popup 開啟時反映真實捕捉
狀態，啟動失敗如實回報錯誤而非假裝成功（Issue #671 / PR #676）。皆為
control-model／shell source、extension source、policy check 與 SPEC-0009／SPEC-0010
evidence；不宣稱實體音訊或 driver 能力。
第二十五波整合增量已合併：IpcNamedPipeServerV1 在兩次連線之間的空檔收到 stop() 時，
會重複取消當下註冊的 server handle I/O 直到 worker 結束，Engine Preview 關閉不再
固定等待完整 idle timeout；connected-idle prompt-stop 行為維持不變，IPC framing 與
ownership 語意不變（Issue #655 / PR #661）。TruePeakLimiterV1 恢復上限從每區塊 +6 dB
改為以經過音訊時間（dB domain，約 +6 dB 每毫秒）計算並跟隨引擎取樣率，20 個 48-frame
區塊與單一 960-frame 區塊在相同時間跨度內恢復曲線一致，需要壓低時仍立即反應
（Issue #659 / PR #666）。皆為 user-space source／contract test／SPEC-0001、SPEC-0002
evidence；不宣稱實體音訊、driver 安裝/載入/HLK 或 Microsoft signing。
第二十四波整合增量已合併：driver adapter 配置改為 ExAllocatePool3，paged／non-paged pool 類別與 'ibiH' tag 不變，建置定義升至 NTDDI_WIN10_VB 以取得宣告；GetHWLatency 把每 buffer 延遲估計寫入 HWLatency->CodecDelay，PortCls 讀到有效硬體延遲而不是被丟棄（Issue #652 / PR #660）。NODE_MUTE 名稱改指向 WDK 既有 KSAUDFNAME_MASTER_MUTE，移除未用的 null fallback，讓系統屬性頁與音效工具顯示標準「靜音」名稱（Issue #662 / PR #663）。皆為 source／local WDK build／Inf2Cat evidence；不宣稱 guest 安裝／載入／PnP start／實體音訊／HLK／Microsoft signing。

第二十三波整合增量已合併：TruePeakLimiterV1 記錄上一區塊套用增益，恢復上限固定為 2x/區塊（約 +6 dB），attenuation 仍立即套用；reset() 一併重置 recovery 狀態，並新增連續區塊回歸測試確保安靜後不會瞬間跳回（Issue #647 / PR #648）。屬 user-space DSP 契約證據，不宣稱 ITU/BS.1770 或 certified true-peak conformance、實體音訊、driver 安裝/載入/HLK/Microsoft signing。

第二十二波整合增量已合併：BasicNoiseSuppressorV1 修正開／關增益方向——attack_ms 控制開啟速度、release_ms 控制關閉速度——並新增 closed-to-open 回歸測試（Issue #636 / PR #640）；handoff 稽核新增「多個 active Issue 共用同一 branch」fail-closed 自檢（Issue #643 / PR #644）；IpcNamedPipeServerV1 在 worker 啟動前註冊同步初始 pipe handle，stop() 可立即取消等待中的 ConnectNamedPipe 而非等 idle timeout（Issue #637 / PR #645）；driver 端每個 endpoint 成對註冊 PortCls Topology 與 WaveRT filter、INF 介面一致並接上 bridge 實體連線，本機 WDK 建置與 Inf2Cat 通過（Issue #462 / PR #638）。DSP/IPC/handoff 項目為 user-space/source evidence；WaveRT 配對為本機建置證據，不宣稱 guest 安裝/載入/PnP start/實體音訊/HLK/Microsoft signing。

第二十一波整合增量已合併：Engine Preview 的 canonical 控制管線加入單一擁有權 fail-closed，無法取得管線時以離開碼 3 退出（Issue #628 / PR #631）；PortCls adapter start path 新增分階段 DbgPrintEx 診斷（Issue #633 / PR #635）；這僅為 control/start-path 診斷與 user-space 證據，不宣稱 driver 已安裝/載入/HLK/Microsoft signing。

第二十波指令面與證據增量已合併：AGENTS.md 改寫為三層規則索引（硬性限制／產品與流程
預設／core+conditional 驗證門檻），driver 安裝、載入、HLK 與 Microsoft 簽章明確歸屬
release 階段，README gates 清單同步拆分核心與條件式兩層，AI_HANDOFF 從長文壓縮為短入口、
MULTI_AGENT 認領流程改為「指派後即開工、首個可審閱 commit 開 draft PR」並把 directory
lane 表降級為路由提示，CONTRIBUTING 移除已廢除的 baseline counter 檔指引，退役的
task-handoff schema 以 SPEC-0004 記錄並由 CHANGE_CONTRACT.yml 的 validation_tiers 取代，
evidence ai-instruction-audit-v2.json（Issue #610 / PR #616）；README live probe 文件對齊：
live-wasapi-handoff-check.ps1 與 live-audio-session-check.ps1 兩個 opt-in 探針從公開入口頁
可被發現（匿名彙整輸出與 unavailable 如實記錄語意），docs-check -SelfTest 範例補進 gates
說明，evidence readme-live-probe-docs-v1.json（Issue #613 / PR #615）；隔離 Hyper-V VM
WaveRT 載入測試 evidence：official Win11 media 建置 Gen2 guest、testsigning 開啟、Root
cert 匯入成功但 TrustedPublisher store 缺失導致 Driver Store 以 0xE0000242 拒絕 catalog，
guest 內 Import-Certificate 恢復後 pnputil 成功 staging 為 oem2.inf，devcon 建立
Root\HibikiDSP 且服務安裝但 PnP start 可重現失敗（CM_PROB_FAILED_START /
NTSTATUS 0xC000000D，含 disable/enable 重試），明確不主張實體音訊、WaveRT streaming、HLK
或 Microsoft signing，evidence driver-vm-load-test-v1.json（Issue #462 / PR #614）。皆為
docs/evidence 增量。

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
