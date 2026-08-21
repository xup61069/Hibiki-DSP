# Changelog

## Unreleased

### Added

- Windows Audio Scene OS 的 source-only foundation：C++ user-space graph、固定輸出群組、
  volume safety、IPC、ASIO/VST3/browser/driver source boundaries、校正 exporter 與 contract tests。
- Expert per-App route presets：bounded local catalog、原子保存、優先級 resolver、同優先級歧義
  fail-closed、版本化 route-rule command 與 source-only WinUI editor。
- AI 交接入口與 machine-checkable handoff gate；fresh clone 可由文件找到目前限制、證據與唯一下一步。

### Changed

- README 現在明確區分可重跑 source evidence、尚未驗證的硬體能力與 source-only release policy。

### Not yet released

- 沒有可安裝 preview、WaveRT driver、Microsoft 簽章安裝器或 GitHub binary release。目標
  Windows 11 24H2/VS 2026/WDK、WinUI XAML build 與實體音訊硬體驗收仍是 release blocker。
