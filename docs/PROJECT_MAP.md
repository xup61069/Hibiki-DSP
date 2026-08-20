# Hibiki DSP 專案地圖

| 子系統 | 目的 | 目前狀態 | 主要契約 |
| --- | --- | --- | --- |
| `driver/` | 固定虛擬端點、KS volume/mute、私有 control IPC | ABI boundary | ADR-0002/SPEC-0003 |
| `src/` | C++20 RT graph、AudioEngine、volume state、ISO math、IPC framing | 可測試 user-space 骨架 | SPEC-0001/0002 |
| `apps/` | WinUI 3 control plane、Easy/Expert UX | 待建立 | SceneProfile |
| `asio/` | Hibiki ASIO bridge 與 DAW lane | stream model | SceneProfile |
| `vst-host/` | Out-of-process VST3 host | quarantine model | plugin contract |
| `extensions/` | Chrome/Edge MV3 tab capture | MV3 source prototype | browser lane |
| `installer/` | GPL bootstrapper、driver transaction、rollback | 待建立 | release manifest |
| `sdk/` | driver／engine 的 Apache-2.0 C ABI 與 schema boundary | v1 header | driver-control-v1 |
| `schemas/` | IPC、Graph、Scene、volume、PEQ、ISO status、handoff、release JSON schemas | 初始骨架 | versioned v1 |
| `build/` | 固定 VS／SDK／WDK 與可重建條件 | 初始鎖定 | toolchain-lock.yml |
| `tools/` | doctor、probe、verify、docs/source policy gates | 初始骨架 | AGENTS.md |

即時執行緒只能讀 immutable snapshots；control plane 以 Validate → Prepare →
Commit/Rollback 修改 graph。跨子系統不得硬編碼個人裝置名稱、Endpoint ID 或磁碟機路徑。
