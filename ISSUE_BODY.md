## Objective

WinUI shell modernization follow-up: restore the scene-card treatment that was lost in the PR #576 empty merge and extend it into a cohesive modern card system for the Easy shell.

## Acceptance

- Scene cards use a shared rounded, stroked, tinted card style with clear hover, pressed, focused and selected affordances instead of the plain default button look.
- Section headings across Scenes / Custom presets / Volume protection use one consistent Subtitle scale.
- Scene cards expose a safety badge with accessible label text (not icon-only) while preserving all required automation names and bindings.
- Volume chips rows are compact caption chips; no control model changes; scope limited to apps/winui-shell.
- The slice explicitly restores the files listed in the empty-merge record on #574 (App.xaml merge of Styles/SceneCard.xaml, MainWindow.xaml scene-card template and heading hierarchy).

## Constraints

- apps/winui-shell scope only.
- winui-shell-check.ps1 required bindings and all interactive AutomationProperties names must remain present.

<!-- hibiki:handoff-v1
schema_version: 2
issue: 593
branch: codex/593-scene-card-system
target_branch: main
base_commit: 44b6149
role: worker
owner: xup61069
updated_at: 2026-08-23T13:20:00+08:00
environment_fingerprint: windows-26200-amd64-vs2026-18.9-dotnet-10.0.400-pwsh-7.6.4
scope_globs: ["apps/winui-shell/**"]
shared_paths: []
depends_on: []
specs: []
adrs: []
next_safe_action: "Implement Styles/SceneCard.xaml, App.xaml merge and MainWindow.xaml scene card template; run gates."
resume_commands: ["pwsh -NoProfile -File tools/winui-shell-check.ps1"]
-->

