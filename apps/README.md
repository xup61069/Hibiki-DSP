# Hibiki control app

The WinUI 3 control plane will present Easy mode first: output, scene, one-tap
Enhance and safe volume. Expert mode reveals lanes, Matrix, DSP graph, IR phase,
calibration and VST3. The UI talks to the engine through versioned control messages
and must show Controlled, Bypassed or Degraded status explicitly.

The current user-space `make_easy_scene` factory supplies the first four
one-click defaults (Game, Movie, Voice and Studio/Strict Direct). The WinUI
layer will render these defaults first and expose lane/Matrix/IR details only in
Expert mode.

`control-model/` is a UI-independent .NET model for Easy/Expert mode, preset
cards, explicit Controlled/Bypassed/Degraded status, dB presentation and
transactional device selection. It now also defines fail-closed One-Tap Enhance
and scene selection behavior; `control-model-check/` runs without WinUI so
future WinUI 3 code can reuse the same behavior contract. A successful enhance
also records the trimmed active output-group identity for status rendering.

`IpcProtocol.cs` mirrors the C++ `IpcFrameV1` little-endian envelope, including
the 1 MiB bound, explicit decode errors and request-ID correlation. The check
project includes a cross-language known-byte fixture; transport lifecycle is
kept separate from the UI model. `NamedPipeControlClientV1` adds an asynchronous
bounded client for the stable `HibikiDSP_v1_control` logical pipe.
`ControlCommandFactoryV1` emits Hello, VolumeNotification, SceneApply,
GraphCommit and GraphRollback envelopes so the UI does not handcraft control
payloads.
`EasyControlViewModel` is the binding-ready surface for a future WinUI 3 shell:
it keeps Easy mode fail-closed, exposes Expert mode explicitly, and emits the
same versioned commands for engine transport.
