# Hibiki DSP 專案地圖

| 子系統 | 目的 | 目前狀態 | 主要契約 |
| --- | --- | --- | --- |
| `driver/` | 固定虛擬端點、KS volume/mute、私有 control IPC | ABI + MS-PL endpoint control core + source-only INF package template；PortCls 尚未接線 | ADR-0002/SPEC-0003 |
| `src/` | C++20 RT graph、AudioEngine、per-output-group render、dB-domain volume ramp、session registry/Windows session adapter、Windows volume/device watcher/recovery、ISO math、IR phase policy、IPC codec/named-pipe worker、固定 SPSC control queue、Easy Scene control-worker adapter、persistent SRC、scene safety policy、sink crossfade、ASIO/外部 Lane block API、WASAPI Float32 output boundary、Virtual Mic privacy/reference contract | 可測試 user-space；實體端點/driver 尚未 soak | SPEC-0001/0002/0003/0006/0007 |
| `apps/` | WinUI 3 control plane、Easy/Expert UX | UI-independent .NET control model + One-Tap/scene/device contracts + C# IPC codec/named-pipe client；WinUI shell待建立 | SceneProfile / IPC v1 |
| `asio/` | Hibiki ASIO bridge、可選原生 COM transport 與 DAW lane | stream model + optional unsigned DLL target | SceneProfile / distribution profile |
| `vst-host/` | Out-of-process VST3 host | trusted/certified model + Job Object supervisor + tested frame codec/named-pipe boundary + bounded passthrough worker；SDK dispatch pending | SPEC-0008 / plugin contract |
| `extensions/` | Chrome/Edge MV3 tab capture + HIBT AudioWorklet packetizer、loopback WebSocket、TabCaptureQueue SPSC、graph lane adapter | user-gesture capture source + bounded queue + shared graph/Group Master adapter；noise-reduction model 待接 | SPEC-0009 / browser lane |
| `installer/` | GPL bootstrapper、driver transaction、rollback | PowerShell source bootstrapper with dry-run, hash and traversal-safe manifest gate；signed package pending | release manifest |
| `sdk/` | driver／engine 的 Apache-2.0 C ABI 與 schema boundary | v1 header | driver-control-v1 |
| `schemas/` | IPC、Graph、Scene、volume、PEQ、ISO status、handoff、release JSON schemas | 初始骨架 | versioned v1 |
| `build/` | 固定 VS／SDK／WDK 與可重建條件 | 初始鎖定 | toolchain-lock.yml |
| `tools/` | doctor、probe、verify、docs/source policy gates | 初始骨架 | AGENTS.md |

即時執行緒只能讀 immutable snapshots；control plane 以 Validate → Prepare →
Commit/Rollback 修改 graph。跨子系統不得硬編碼個人裝置名稱、Endpoint ID 或磁碟機路徑。
