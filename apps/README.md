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
future WinUI 3 code can reuse the same behavior contract.
