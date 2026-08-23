# Hibiki DSP AI 工作規則

本檔是 AI 與貢獻者的唯一規則索引。規則分三層：

1. **硬性限制**：任何情況不得違反（安全、法律、隱私、誠實）。
2. **產品與流程預設**：可以討論、可以用新 ADR/Spec 取代的預設值。
3. **驗證門檻**：依變更範圍觸發的檢查；簽章與發行屬 release 階段，不是日常開發前置。

開始工作前先讀本檔、`docs/START_HERE.md` 與 `docs/AI_HANDOFF.md`；聊天紀錄、AI memory
與 IDE 規則都不是專案真值。

## 第一層：硬性限制（不可協商）

- 目標平台：Windows 11 24H2+ x64；C++20 即時核心、C# WinUI 3 UI。
- RT audio thread 不配置、不取得 mutex、不等待、不呼叫 COM/UI/檔案系統。
- driver（MS-PL）與 GPL user-space 只能透過版本化 IPC 互動；不得靜態或動態連結。
- 廠商 ASIO、WASAPI Exclusive、RAW 路徑不可宣稱受 Hibiki 控制。
- 不提交 EXE、DLL、SYS、MSI、MSIX、VST3、PE/COFF、簽章憑證或私密金鑰。
- 真實裝置 ID、校正檔、序號、私人路徑放 `.local/`，不得進 Git。
- 不反編譯或繞過閉源軟體保護；只用開源程式、官方文件與合法 black-box 觀察。
- ISO 226 授權文件、掃描、完整表格與受限資料不可放入 repo、Issue、prompt 或 RAG。
- 不宣稱未驗證的能力：user-space probe 不是 driver/WaveRT/HLK/Microsoft signing evidence；
  控制命令入列不是已完成音訊套用。

## 第二層：產品與流程預設（可依 ADR/Spec 演進）

- 每個工作切片對應一個 GitHub Issue、一個隔離 worktree、一個 branch、Issue body 內的
  `<!-- hibiki:handoff-v1 -->` handoff block 與一個 draft PR。詳細協定見
  `docs/ai/MULTI_AGENT.md`。
- 寫入需要被指派（Issue assignee + lifecycle label）；唯讀偵察不需要認領。首次可審閱的
  commit push 後就開 draft PR，不需要空認領 commit。
- 衝突判定以 handoff block 的 `scope_globs`、語意契約 ownership 與 open Issue/draft PR
  為準。`docs/ai/MULTI_AGENT.md` 的目錄 lane 表只是路由提示，不是全域單寫者瓶頸。
- `docs/state/BASELINE.md` 的計數由 `tools/docs-check.ps1` 即時量測；切片不需維護
  counter 檔（`build/baseline-counters.json` 已於 #197 廢除）。
- 全域快照（`docs/AI_HANDOFF.md`、`docs/state/BASELINE.md`、`docs/PROJECT_MAP.md`、
  root `README.md`）由 integrator 在合併時單寫；feature AI 維護自己的 handoff、Spec、
  tests 與 evidence。
- Accepted ADR 不可改寫；新決策建立新 ADR 並標示 supersedes。
- 修改 public API、schema、DSP 順序、安全規則或建置方式時，同一 PR 更新對應 Spec/ADR
  與 evidence。
- 換 AI 或電腦前：更新 Issue body handoff block、建立 WIP commit、push branch、寫明
  下一個安全動作。

## 第三層：驗證門檻（core + conditional）

所有 gates 用 PowerShell 7（`pwsh`）執行；沒有 `pwsh` 先
`winget install --id Microsoft.PowerShell`。多數 gate 提供 `-SelfTest` 離線自檢。

### 核心（每個切片必跑）

```powershell
pwsh -File tools/doctor.ps1 -CheckOnly
pwsh -File tools/handoff-check.ps1 -Issue <n>
pwsh -File tools/verify.ps1
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
pwsh -File tools/source-only-ci-check.ps1
```

### 條件式（範圍或驗收需要時才跑）

| 觸發條件 | 額外 gates |
| --- | --- |
| 改 UI／control model | `build-preview.ps1`（DesktopCompat 或鎖定機上的 `-Target WinUI`）、`winui-shell-check.ps1` |
| 改 engine/control plane 整合 | `build-engine-preview.ps1`、`engine-preview-smoke.ps1`、`control-model-check.ps1`、`control-model-engine-smoke.ps1` |
| 改 extensions | `extension-check.ps1` |
| 改 installer／distribution identity | `installer-check.ps1`、`distribution-check.ps1` |
| 改 driver source boundary | `driver-source-check.ps1` |
| driver release 驗收（WDK package 存在時） | `driver-signability-check.ps1`（必要時加 `-PackageRoot <package> -RequireInf2Cat`） |
| 明確 opt-in 的 live probe | `live-*-check.ps1` 系列；只輸出匿名資料，不改變機器狀態（`-WriteTest` 變體除外），結果不等於 driver/HLK/簽章 evidence |

driver 安裝、載入、HLK 與 Microsoft 簽章屬於 release 階段工作：在取得鎖定 target 機器
與正式憑證之前，沒有任何日常 gate 可以或應該宣稱完成這些項目。

遇到環境差異先記錄 fingerprint 並更新 handoff block，不要自行重生
`config/distribution-profile.yml` 裡的 endpoint、ASIO、IPC GUID。
