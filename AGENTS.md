# Hibiki DSP AI 工作規則

本檔是 AI 與貢獻者的唯一規則索引。規則分三層：

1. **硬性限制**：任何情況不得違反（安全、法律、隱私、誠實）。
2. **產品與流程預設**：可以討論、可以用新 ADR/Spec 取代的預設值。
3. **驗證門檻**：依變更範圍觸發的檢查；簽章與發行屬 release 階段，不是日常開發前置。

開始工作前只先讀本檔與 `docs/START_HERE.md`，取得指派後再讀對應 Issue handoff 與其指定的
Spec／ADR。`docs/AI_HANDOFF.md`、`docs/state/BASELINE.md`、`docs/PROJECT_MAP.md` 與完整
`docs/ai/MULTI_AGENT.md` 依角色與當前問題按需查閱，不得在每個視窗預載或重複貼入；聊天紀錄、
AI memory 與 IDE 規則都不是專案真值。

## 第一層：硬性限制（不可協商）

- RT audio thread 不配置、不取得 mutex、不等待、不呼叫 COM/UI/檔案系統。
- driver（MS-PL）與 GPL user-space 只能透過版本化 Apache ABI／IPC 互動；不得靜態或動態
  連結。高流量 audio path 可使用契約化的固定容量 shared-memory/ring，不需要把 samples
  送進變長 control message。
- 廠商 ASIO、WASAPI Exclusive、RAW 路徑不可宣稱受 Hibiki 控制。
- 不提交 EXE、DLL、SYS、MSI、MSIX、VST3、PE/COFF、簽章憑證或私密金鑰。
- 真實裝置 ID、校正檔、序號、私人路徑放 `.local/`，不得進 Git。
- 不反編譯或繞過閉源軟體保護；只用開源程式、官方文件與合法 black-box 觀察。
- ISO 226 授權文件、掃描、完整表格與受限資料不可放入 repo、Issue、prompt 或 RAG。
- 不宣稱未驗證的能力：user-space probe 不是 driver/WaveRT/HLK/Microsoft signing evidence；
  控制命令入列不是已完成音訊套用。

## 第二層：產品與流程預設（可依 ADR/Spec 演進）

- 對 maintainer 的進度與完成回報必須先用白話說明：現在讓產品多了／修好了什麼、使用者會
  感覺到什麼、如何確認，以及還缺什麼。不要用 push、commit、branch、PR、merge 或 CI 當標題
  或主要敘事；這些只在影響風險、阻擋、驗證可信度，或 maintainer 明確詢問時，放在末尾的短版
  開發紀錄。
- 對 maintainer 的進度與完成回報必須先用白話說明：現在讓產品多了／修好了什麼、使用者會
  感覺到什麼、如何確認，以及還缺什麼。不要用 push、commit、branch、PR、merge 或 CI 當標題
  或主要敘事；這些只在影響風險、阻擋、驗證可信度，或 maintainer 明確詢問時，放在末尾的短版
  開發紀錄。省略對話中的 Git 細節不會取消下列內部協作與交接規則。可貼用的視窗分工與 `/goal`
  啟動詞見 `docs/ai/CODEX_GOALS.md`。
- 唯讀偵察不需要認領。寫入需要 maintainer／orchestrator 明確指派、GitHub Issue、非 `main`
  branch、Issue body 內的 `<!-- hibiki:handoff-v1 -->` block 與 write scope。人類 maintainer
  對目前 session 的直接要求算明確指派；active orchestrator 可在檢查 overlap 後建立並正式
  claim Issue，worker 不得自行挑選 backlog。正式 claim 前可使用非授權的 `claim-pending`
  標記；`claim-pending` 不授予寫入權，必須由序列化的 claim-admission workflow 在全域
  overlap/audit/readback 完成後才轉成 `claimed`。
- 有其他 writer、branch 已被 worktree 佔用或 occupancy 不確定時，必須使用獨立 worktree；
  確認只有單一 writer 時仍建議隔離，但不是文件小改的硬性前置。
- 首次可重建的 WIP/reviewable commit push 後立即開 draft PR，不需要空認領 commit。
  詳細協定見 `docs/ai/MULTI_AGENT.md`。
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
- 新 evidence 使用 `evidence_format: 2` 的 append-only manifest，以非 evidence Git blob 的內容摘要
  綁定來源；不得預填尚不存在的 squash commit，也不得覆寫 legacy evidence。更正時新增
  `supersedes` 紀錄。
- 換 AI 或電腦前：更新 Issue body handoff block、建立 WIP commit、push branch、寫明
  下一個安全動作。

## 第三層：驗證門檻（always-run + conditional）

所有 gates 用 PowerShell 7（`pwsh`）執行；沒有 `pwsh` 先
`winget install --id Microsoft.PowerShell`。多數 gate 提供 `-SelfTest` 離線自檢。

### Always-run（每個寫入切片必跑）

```powershell
pwsh -File tools/handoff-check.ps1 -Issue <n>
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
git diff --check
```

### 條件式（範圍或驗收需要時才跑）

| 觸發條件 | 額外 gates |
| --- | --- |
| 需要 build／toolchain evidence | `doctor.ps1 -CheckOnly` |
| 改 C/C++、CMake、schema、contract 或 tests | `verify.ps1` |
| 改 workflow、公開 release／artifact policy | `source-only-ci-check.ps1` |
| 新增或更正 evidence | `evidence-audit.ps1`（`change` 模式以 `-DescribeCurrentChange`、evidence-only `snapshot` 模式以 `-DescribeSnapshotSourceSet` 產生來源摘要） |
| 改 UI／control model | `control-model-check.ps1`、`build-preview.ps1`（DesktopCompat 或鎖定機上的 `-Target WinUI`）、`winui-shell-check.ps1` |
| 改 engine/control plane 整合 | `build-engine-preview.ps1`、`engine-preview-smoke.ps1`、`control-model-check.ps1`、`control-model-engine-smoke.ps1` |
| 改 extensions | `extension-check.ps1` |
| 改 installer／distribution identity | `installer-check.ps1`、`distribution-check.ps1` |
| 改 driver source boundary | `driver-source-check.ps1`；需要 WDK build evidence 時另跑 `build-driver.ps1` |
| driver release 驗收（WDK package 存在時） | `driver-signability-check.ps1`（必要時加 `-PackageRoot <package> -RequireInf2Cat`） |
| 明確 opt-in 的 live probe | `live-*-check.ps1` 系列；只輸出匿名資料，不改變機器狀態（`-WriteTest` 變體除外），結果不等於 driver/HLK/簽章 evidence |

driver 安裝、載入、HLK 與 Microsoft 簽章屬於 release 階段工作：在取得鎖定 target 機器
與正式憑證之前，沒有任何日常 gate 可以或應該宣稱完成這些項目。

遇到環境差異先記錄 fingerprint 並更新 handoff block，不要自行重生
`config/distribution-profile.yml` 裡的 endpoint、ASIO、IPC GUID。
