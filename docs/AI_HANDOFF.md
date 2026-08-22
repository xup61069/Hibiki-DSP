# Hibiki DSP：下一個 AI 的 90 秒入口

這一頁是交接摘要，不取代 Spec、ADR、source 或 evidence。遇到衝突時，以
`docs/START_HERE.md` 所列權威順序處理，不要依聊天紀錄猜測。

## 先做這七件事

```powershell
git fetch --all --prune
git status --short --branch
gh issue view <issue>
gh pr list --state open
pwsh -File tools/doctor.ps1 -CheckOnly
pwsh -File tools/handoff-check.ps1 -Issue <issue>
pwsh -File tools/context-pack.ps1 -Issue <issue> -NoSource
```

工作樹不是乾淨狀態、handoff check 失敗，或 target toolchain 不符合時，先在 Issue body 的
handoff block 記錄事實；不要直接改 DSP、driver、永久 ID 或 release 設定。
Foundation integration Issue 是 foundation handoff，只由 integrator 更新，不是所有 AI 共用的工作單。

## 多 AI 並行入口

- 完整規則見 `docs/ai/MULTI_AGENT.md`：一個 AI 工作切片對應一個 Issue、獨立 worktree、
  branch、Issue body handoff block 與 draft PR。同一 Issue／branch 同時只能有一個 writer。
- 修改前以 Issue assignee／lifecycle label／linked draft PR 完成 live claim，並在 Issue body handoff block 宣告
  `scope_globs`、`shared_paths` 與 `depends_on`。scope 重疊時先停止，由 integrator 指定 owner。
- feature AI 只更新自己的 handoff、目標 Spec、tests 與 evidence；全域摘要與 Issue 0 由
  integrator 在整合時單次更新。每個 active claim 各自只有一個 `Next safe action`，但不同
  Issue 可以在不重疊的 scope 內並行。

## 現在的真實位置

- `main` 已有可重跑的 C++ user-space contract baseline、C# control model、source-only WinUI
  shell 與 public source policy gates。
- Expert per-App route preset 已保存、會對選取 App 做規則預覽，並透過版本化 command 等待
  engine Ack；這不是已驗證的實體 per-App capture/re-send。
- 本機 Windows 26200／Visual Studio 2026 18.9 已可產生 target-class formal WinUI 證據：
  `tools/build-preview.ps1 -Target WinUI` 經 `visual-studio-msbuild` + `/restore` 完成 XAML
  建置（App.xbf／MainWindow.xbf／PRI，0 警告 0 錯錯誤；證據
  `winui-m1-formal-build-v1.json`，PR #357）；formal shell 可啟動並以
  `tools/winui-a11y-smoke.ps1` 記錄 UIA 控制樹（55–59 controls；PR #361）；整合啟動器
  `tools/run-preview.ps1 -Ui FormalWinUI -SmokeTest` 已端到端驗證（PR #364）。driver、
  簽章與 Windows 11 24H2 hardware soak 仍必須在鎖定 target 環境重新驗證。
- 本機 Desktop Compatibility Preview 已可由 `tools/build-preview.ps1 -Target DesktopCompat` 建置並通過
  啟動 smoke；它和正式 shell 共用 `EasyControlViewModel`，自帶 .NET runtime、不依賴 Windows App Runtime，
  現在包含場景選擇、路由健康摘要與音量來源／致動器顯示，但不是 XAML、無障礙、driver 或
  release evidence；連線後每秒輪詢一次 bounded ControlStatusSnapshot，命令忙碌時會合併輪詢。
 目前也顯示本機 physical catalog 的 render/capture 數量與預設輸出 metadata；預設不提供實體
  sink 啟用或切換。若明確傳入 `--enable-wasapi-output`，Engine Preview 會在 catalog 找到
  支援的 active default render endpoint 後啟動既有 dedicated shared-mode WASAPI sink worker，
  並把 worker 的 `Pending/Ready/Degraded` 狀態送進 `main-output` route-health card。這只是
  user-space output boundary；沒有 source block 時 worker 只保持安全 silence，不能寫成完整
  physical playback、WaveRT 或 per-App DSP delivery。
- WinUI 與 Desktop Preview 都有 IR phase policy controls（Game/Balanced/Movie/Bypass）與明確的
  0/80/160 ms delay semantics；控制面會把 bounded IR WAV 送入 Engine Preview。
- C++ control-plane 已有 bounded RIFF/WAVE IR importer，支援 Float32/PCM16/24/32、finite/tap/file
  bounds 與 channel-major convolver prepare；`AudioEngineModel::prepare_ir`／`commit_ir`／
  `prepare_ir_clear`／`rollback_ir` 會把它接到固定 output-group graph render，IR 在 Group Master
  與 limiter 前執行；SceneApply 會以同一 transaction 清除舊 IR，見
  `evidence/0000-foundation/ir-graph-attachment-v1.json`。
- `build_ir_phase_kernel_v1` 已補上 control-plane 的 real-cepstrum minimum-phase 與
  source-magnitude causal linear-phase transform；C# `PrepareIrAsync`、Desktop Preview 與 Engine
  Preview 已能透過固定 288-byte `IrPrepareCommand` 完成 WAV→kernel prepare、graph attachment
  commit Ack。這仍不能宣稱已連接實體 sink、WaveRT driver 或完成聲學校正。
- C++ Engine Preview 已可由 `tools/build-engine-preview.ps1` 建置；`tools/engine-preview-smoke.ps1`
  會啟動它並驗證 v1 named-pipe Hello/Ack request correlation 與 ControlStatusSnapshot 回覆。
  `tools/control-model-engine-smoke.ps1` 另外以 C# `EasyControlViewModel` 驗證 −18 dB 音量
  往返、引擎快照讀回、Game One-Tap SceneApply Ack，以及本機 Windows render/capture
  catalog（目前環境 14 筆）的跨程序 snapshot。Engine Preview 預設只枚舉 metadata、保留
  bounded snapshot 與 watcher poll；只有 `--enable-wasapi-output` 才啟動既有 shared-mode
  WASAPI sink worker，且不改 Windows 音量。
  `tools/run-preview.ps1 -Build` 預設使用 `-Ui Auto`：有符合版本的 Windows App Runtime 1.7
  就啟動 WinUICompat，否則自動退回不依賴 Runtime 的 Desktop Compatibility UI；兩者都只
  提供 user-space control host，不代表 WaveRT、實體輸出或 Windows session routing 已完成。
  若要固定 DesktopCompat，加入 `-Ui DesktopCompat`；若要明確啟動現有 shared-mode sink，加入
  `-EnableWasapiOutput`；若要明確驗證 Windows endpoint 音量聯動，使用
  `tools/run-preview.ps1 -Build -EnableSystemVolume`；預設不會寫入系統音量，且
  `tools/engine-preview-smoke.ps1 -EnableSystemVolume -StatusOnly` 只檢查 broker Ready、不送音量命令。
  `tools/engine-preview-smoke.ps1 -EnableWasapiOutput -StatusOnly` 只檢查 sink route state，
  不把 status-only 結果當成實體音訊播放證據。
- Engine Preview 另有獨立的 `--enable-session-routing` opt-in：它在 COM worker 綁定
  `IAudioSessionManager2`，發布 bounded App/session catalog，並把 App 音量、lane/output 與
  route-rule 命令送入固定 queue；`-EnableSessionRouting` 可與系統音量旗標同時使用。這是
  控制面與 Windows session volume 的可重跑邊界，不是實體 per-App capture/re-send 或 DSP
  delivery 證據；Desktop Preview 只顯示安全摘要與明確的 delivery unverified 警示。
- ISO 226 只保留合法 formula/derived boundary；禁止把受限標準文件、完整表格、掃圖或其內容
  放進 source、Issue、prompt、RAG、fixture 或 evidence。
- `evidence/0000-foundation/winui-compat-preview-v1.json` records the local Microsoft App Runtime
  1.7 installation and the successful `WinUICompat -SmokeTest`; it is only a compatibility launch
  proof and must not be promoted to formal XAML/accessibility evidence. Formal-class XAML
  build/launch/UIA-tree evidence now lives in `winui-m1-formal-build-v1.json`,
  `winui-a11y-smoke-v1.json` and `run-preview-formal-winui-v1.json`.
- `evidence/0000-foundation/control-model-engine-ir-clear-v1.json` records three consecutive
  session-routing control-model runs, including IR prepare → Scene IR clear and bounded temporary
  fixture cleanup. It is user-space reliability evidence only; it does not prove physical playback.
- `pwsh -File tools/live-system-volume-check.ps1 -WriteTest` is the explicit live volume probe:
  it starts Engine Preview with `--enable-system-volume`, sends a volume notification through the
  named pipe, verifies the default endpoint moved by about 3 dB via a separate broker readback, and
  restores the original value. It prints no endpoint identity and remains user-space write-through
  evidence only; do not run it silently or treat it as driver/WaveRT evidence. `-DirectBroker` is a
  diagnostic-only bypass.
- `pwsh -File tools/live-session-volume-check.ps1 -WriteTest` is the explicit per-App/session
  volume probe: it starts Engine Preview with session routing, creates one silent shared-mode
  session, discovers it through the bounded `SessionCatalogSnapshot`, sends the generation-scoped
  handle through the IPC/control queue, reads the same `ISimpleAudioVolume` value back after the COM
  worker applies it, and restores the original dB/mute before exit. It does not print endpoint/session
  identity and remains user-space control-plane evidence; physical per-App capture/re-send and DSP
  delivery are still unverified. `-DirectCoordinator` is a diagnostic-only bypass. The same probe
  then sends a bounded `SessionRouteCommand` and requires the route catalog to read back `Ready`;
  then sends a bounded `SessionRouteRuleCommand` upsert/remove and verifies candidate rule readback (`Ready → Pending`).
  This verifies candidate graph commit and rule transaction in the worker, not physical audio rerouting.
- C++ VST host supervisor now includes `Vst3TimelineEditorV1` (`vst-host/include/hibiki/vst3_timeline_editor.hpp`),
  providing fail-closed parameter point upsert, bounded remove/set-value, and validated commit against
  `validate_vst3_parameter_timeline_v1`.
- The supervisor timeline chain now has a selection-aware editing facade:
  `Vst3TimelineSupervisorSurfaceV1` (`vst-host/include/hibiki/vst3_supervisor_surface.hpp`,
  Issue #351 / PR #377) composes the editor with a non-owned `Vst3TimelineFileStoreV1`
  handle; all operations fail closed while detached or unselected, and dirty state is derived
  by comparing the published snapshot to the loaded/saved baseline. Evidence:
  `evidence/0000-foundation/vst3-supervisor-surface-v1.json`. This is a headless control-plane
  contract and proves nothing about UI editors, physical audio, or driver delivery.
- C# control model now includes `CalibrationModel.cs` (`apps/control-model/`), providing strongly-typed
  calibration response data contracts, deterministic bounded PEQ correction compiler matching SPEC-0011,
  atomic JSON persistence, and Equalizer APO/CamillaDSP/REW/Hibiki profile exporters.
- The next driver-facing source milestone is now the Apache `driver_control_transport_v1` fixed
  136-byte little-endian endpoint-state/volume-notification packet plus the GPL
  `DriverVolumeLinkV1` adapter. It is contract-tested and ready for a future WDK/SYSVAD project
  to consume; it does not provide a `.sys`, PortCls wiring, HLK result or Microsoft signature.
- The same transport now has a 16-byte header-only Hello/Ack/Error request-correlation contract;
  it intentionally has no unbounded error payload. Evidence for this increment is
  `evidence/0000-foundation/driver-control-handshake-v1.json`.
- The vst-host supervisor timeline chain is merged end to end (all user-space source
  contracts): bounded editing transactions `Vst3TimelineEditorV1` (`9f5f02e`, Issue #14),
  scheduler-bound edit sessions with `timeline_snapshot` readback (`8a041a2`/`5c15b8e`,
  Issue #22), canonical fail-closed JSON persistence for
  `vst3-parameter-timeline-v1.schema.json` (`ff46e15`, Issue #35), a fixed-capacity
  per-timeline file store with strict filename-safe IDs (`dbafc4f`, Issue #63),
  store→scheduler sync via `sync_timeline_store_to_scheduler_v1` (`9cca03d`, Issue #72)
  and sorted timeline-ID plus immutable binding-view introspection (`b7e2ea8`, Issue #78).
  The supervisor UI editing surface, side-chain/multi-bus worker process and third-party
  certification remain open; none of this proves physical audio or driver delivery.
- Process gates learned this week: gate scripts require PowerShell 7 (PS 5.1 cannot run
  the UTF-8-no-BOM tooling; install via winget), multiple gates expose `-SelfTest`
  (`tools/docs-check.ps1 -SelfTest` is merged), the volatile BASELINE summary counters are
  now measured live by `tools/docs-check.ps1` via `git ls-files` and fail closed without a
  committed counter file — #197 retired `build/baseline-counters.json` and the
  `-WriteCounters` chore, and the #25 scope-overlap gate rejects overlapping active
  `scope_globs` across handoffs.
- Kernel-mode driver milestone merged: the local 26100-family WDK toolchain produced the first
  kernel-mode WaveRT PortCls adapter .sys — tools/build-driver.ps1 discovers the newest
  km-enabled kit, links HibikiVirtualAudio.sys, then genuine Inf2Cat packaging passes the
  signability re-check (driver-sys-build-v1.json, Issue #394 / PR #410). Artifacts stay under
  ignored .local/; there is still no install/load/runtime-audio/HLK/Microsoft-signing claim.
- Toolchain minimum baseline: ADR-0005 relaxes the SDK/WDK lock to >= 10.0.26100 with on-disk
  directory floors and 10.1.26100.* package-family metadata checks (Issue #430 / PR #432);
  doctor self-tests cover family-match, missing-metadata, wrong-family and below-minimum cases
  and accept newer-than-floor kits (Issue #434 / PR #441).
- SDK transports fail closed on malformed correlation fields: ASIO shared memory rejects non-zero
  reserved bytes (asio-transport-reserved-zero-v1.json, Issue #420 / PR #425), driver-stream
  packets reject zero sequence/generation (driver-stream-freshness-v1.json, Issue #411 /
  PR #414), and driver-control rejects zero request IDs (driver-control-request-id-v1.json,
  Issue #417 / PR #418).
- Tooling/gate increments: handoff-check runs with UTF-8 encoding (#386), extension checks add a
  tabCapture owner guard (#388) and tab-only media constraints (#391), driver-source-check accepts
  either PortCls notification-buffer naming generation (#426 / PR #428), RemoveSelectedRow has
  undo-after-remove coverage (#419 / PR #422), and the verify workflow cancels superseded runs
  through concurrency groups (#427 / PR #431). Evidence files live under evidence/0000-foundation/.
- Test-sign packaging milestone merged: the WaveRT adapter .sys and .cat were signed with a locally
  created self-signed code-signing cert kept under ignored .local/certs (driver-load-test-v1.json,
  Issue #433 / PR #440); signtool verify /pa ends at the expected untrusted root. Cert import,
  TESTSIGNING boot flag and pnputil install remain explicit user-consented steps; no endpoint or
  audio behavior claim.
- Tooling hardening merged: build-driver.ps1 anchors its repo root from PSScriptRoot, keeps
  obj/package outputs inside .local with containment checks and gained a -SelfTest for root
  discovery/output containment/source boundaries (#444 / PR #445); handoff-check parses TBD
  pre-claim drafts and validates their issue/branch fields (#439 / PR #442).
- WinUI shell modernization merged: MainWindow.xaml now uses a Mica BaseAlt backdrop, Fluent theme
  brushes/type-ramp styles (TitleTextBlockStyle/SubtitleTextBlockStyle) and AccentButtonStyle
  instead of hard-coded colors (#438 / PR #443). Source-level XAML only; title-bar/status-pill
  follow-up is Issue #446.
- WinUI title-bar integration merged: a 48px drag-region row keeps the app icon/name on the left
  and pins the connection-status pill plus connect button away from the caption buttons;
  ExtendsContentIntoTitleBar is wired from code-behind for the full shell while Desktop
  Compatibility Preview keeps its own chrome, and headings use Fluent type-ramp styles
  (#446 / PR #450).
- Verify clean guard merged: verify.ps1 -Clean fails closed unless the delete target exactly matches
  the repository-local build root, is a real directory, and neither the target nor its parent is a
  reparse point; self-tests cover mismatch and reparse-point rejections (#448 / PR #452).
- Scene IR reference preservation merged: SceneProfileV1 keeps a referenced IR across
  matching scene switches - the schema adds an ir reference shape, hub contracts/engine
  validate it and carry referenced IR identity through engine control updates, contract
  tests cover the behavior, and evidence records the slice in scene-ir-reference-v1.json
  (Issue #423 / PR #461).
- ADR frontmatter enforcement merged: accepted ADRs gained structured frontmatter and
  docs-check now validates required ADR frontmatter fields (Issue #453 / PR #459).
- Run-preview launch safety merged: run-preview.ps1 fails closed unless engine/UI launch
  targets stay inside the repository .local root, rejecting outside-root paths, reparse-
  point files or parents and non-file launch targets; -SelfTest covers five launch-target
  safety cases (Issue #455 / PR #457).
- WinUI accessibility smoke launch safety merged: winui-a11y-smoke.ps1 fails closed unless
  the smoke output directory stays under the repository .local root as a real directory
  with safe parents and Hibiki.WinUI.exe stays beneath it as a real file without reparse-
  point target or ancestor; -SelfTest grew from 4 to 11 offline cases with no file writes,
  UI Automation loads or process launches (Issue #470 / PR #472).
- Engine preview build-root guard merged: build-engine-preview.ps1 validates the fixed
  .local/engine-preview build root before CMake configure and again before the build,
  rejecting outside-root paths, existing reparse-point ancestors/targets and non-directory
  roots while keeping normal missing-root behavior; -SelfTest grew from 5 to 7 offline
  cases with no CMake invocation or file writes (Issue #474 / PR #477).
- Engine preview smoke path hardening merged: engine-preview-smoke.ps1 validates output
  paths offline before launching the Engine Preview executable or writing the temporary
  IR WAV; the wrapper self-test grew without starting a process, invoking CMake or
  writing repository output (Issue #482 / PR #483).
- README driver/toolchain milestones refreshed: toolchain wording aligns with accepted
  ADR-0005 (SDK/WDK >= 10.0.26100 floor) and the V1 gap snapshot records the merged local
  kernel-mode PortCls adapter build, Inf2Cat packaging and self-signed test-sign evidence
  while keeping install/load, runtime audio, HLK, Microsoft signing and consumer release
  limits explicit (Issue #478 / PR #480).
- Control-model smoke path hardening merged: control-model-engine-smoke.ps1 fails closed
  on the fixed Engine Preview executable, working directory, project path and the
  .local/control-model-engine-smoke output root, validating before build/launch and
  revalidating before dotnet run while keeping existing missing-output-root creation;
  launcher self-test grew with offline synthetic path cases (Issue #489 / PR #490).
- Handoff retention cleanup merged: residual per-slice files docs/tasks/active/423.md and
  docs/tasks/active/466.md were removed after their slices merged, with
  retention-final-v2.json evidence following the Issue #341/#346 precedent
  (Issue #485 / PR #487).
- Docs link gate merged: docs-check.ps1 validates markdown relative links and fails closed
  on missing targets while skipping URLs, anchors and fenced code blocks; -SelfTest gained
  offline link cases (Issue #496 / PR #497).
- Live audio-session probe hardening merged: live-audio-session-check.ps1 fails closed on
  build-root and probe-path validation, rejecting outside-root paths, existing reparse-
  point ancestors/targets and non-directory/non-file shapes while preserving missing
  build-root creation; wrapper self-test grew with no CMake, probe execution or repository
  writes (Issue #492 / PR #494).
- Timeline surface increments merged: `Vst3TimelineSurfaceModelV1.ClearHistory()` (and the
  ViewModel wrapper) clears undo/redo stacks with history-only notifications while leaving
  published snapshots, dirty baselines and open drafts untouched; empty-history calls are safe
  (`vst3-timeline-clear-history-v1.json`, Issue #403 / PR #404). A redacted sandbox incident
  diagnostic (`Vst3SandboxDiagnosticV1`) records initial/invalid-launch/reset/Windows-setup-failure
  assertions as a sanitized summary without private payload
  (`vst3-sandbox-redacted-diagnostic-v1.json`, Issue #408 / PR #409).

## 目前整合主線

在 Windows 11 24H2+ x64、Visual Studio 2026、SDK/WDK >= 10.0.26100（ADR-0005 最低基線）的
乾淨機器上，先完成 source-only WinUI XAML build 與 accessibility smoke evidence；成功後才進行
第一個 loadable WaveRT endpoint 的 WDK build/signability 工作。不要先做 Microsoft signing、
Gumroad 上傳、發佈 binary 或宣稱 consumer preview。

目標機器可用 `pwsh -File tools/build-preview.ps1 -Target WinUI` 產生不追蹤的本機 UI preview；
這不是 installer，也不代表虛擬 driver 已完成。

非 target 機器要看同一控制模型，可用 `pwsh -File tools/build-preview.ps1 -Target DesktopCompat`；輸出
只在 ignored 的 `.local/preview/DesktopCompat/`，不可加入 Git 或發布。若要直接打開可連線的本機
  預覽，使用 `pwsh -File tools/run-preview.ps1 -Build`；它會先啟動 user-space Engine Preview，再
  開啟自帶 .NET runtime 的桌面 UI，關閉 UI 後引擎會一併停止。若已安裝 Windows App Runtime
  1.7 x64，可用 `pwsh -File tools/build-preview.ps1 -Target WinUICompat -SmokeTest` 驗證
  WinUI 相容殼；它仍不是正式 XAML/accessibility evidence。沒有 Runtime 時使用 DesktopCompat。
Windows 使用者也可雙擊 repository 根目錄的 `Start-HibikiPreview.cmd`；這只是上述命令的來源入口，
不會把任何編譯物加入 Git。
若需要明確啟動 shared-mode WASAPI sink，使用同一層的 `Start-HibikiPreview-Wasapi.cmd`；它仍是
user-space opt-in，不代表 driver 或完整播放驗收。

## 必讀順序

1. [AGENTS.md](../AGENTS.md)：硬限制與每次必跑命令。
2. [START_HERE.md](START_HERE.md)：fresh clone、權威順序與資料邊界。
3. Foundation integration Issue：已完成、最後驗證、風險與下一步（見 GitHub）。
4. [baseline](state/BASELINE.md)：main 已合併能力與限制。
5. 對應的 [Spec index](specs/INDEX.md) 與 [evidence](../evidence/0000-foundation/)。

## 不可自行做的事

- 不重生 `config/distribution-profile.yml` 的 endpoint GUID、driver hardware ID、ASIO CLSID、IPC namespace。
- 不把 `.local/`、`bin/`、`obj/`、PE/COFF、簽章檔、金鑰、真實 endpoint/session ID 或私人校正檔加入 Git。
- 不宣稱 vendor ASIO、WASAPI Exclusive、RAW、Atmos/DTS:X 或未經使用者手勢的 Chrome tab capture 已受 Hibiki 控制。
- `PhysicalDeviceCatalog` 的 capture default 允許存在，但只有 Active render 才能被 UI 選取；
  catalog readiness 不等於 physical sink 已開啟。
- 不把「控制命令已入列」或「預設已保存」寫成「已完成引擎／實體音訊套用」；session
  routing smoke 只代表 catalog／queue／Windows session 邊界可用。

## 交接前最小完成條件

每一個後續 AI 在換機前必須：只更新自己 Issue body handoff block 的 owner、base commit、驗證、限制與
下一步，建立 WIP commit、push 自己的 branch，並跑與改動範圍相符的 gate。所有 public contract
變更都要同步更新 Spec、tests 與 evidence；`docs/state/BASELINE.md` 等全域整合快照由 integrator
在合併時更新。
