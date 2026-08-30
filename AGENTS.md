# Hibiki DSP AI 工作規則

本檔是唯一規則索引：第一層不可協商，第二層可由新 ADR／Spec 演進，第三層依變更觸發。
開始只讀本檔與 `docs/START_HERE.md`；取得任務後再讀 Issue handoff 與指定 Spec／ADR。聊天、
AI memory 與 IDE 規則不是專案真值。

## 第一層：硬性限制

- RT audio thread 不配置、不取得 mutex、不等待、不呼叫 COM／UI／檔案系統。
- MS-PL driver 與 GPL user-space 只透過版本化 Apache ABI／IPC 互動，不得靜態或動態連結；
  高流量 audio path 可用契約化固定容量 shared-memory／ring。
- 不宣稱 Hibiki 控制廠商 ASIO、WASAPI Exclusive 或 RAW 路徑。
- Git 只存 source：不提交 EXE、DLL、SYS、MSI、MSIX、VST3、PE／COFF、憑證或私密金鑰。
- 真實裝置 ID、校正檔、序號與私人路徑只放 `.local/`，不得進 Git。
- 不反編譯或繞過閉源保護；只用開源程式、官方文件與合法 black-box 觀察。
- equal-loudness 授權文件、掃描、完整表格與受限資料不得進 repo、Issue、prompt 或 RAG。
- 不誇大 evidence：user-space／source／build／CI 不是 driver、WaveRT 或實體音訊證據，控制命令入列
  不是已套用音訊。專案不需要 HLK 或任何簽章，不得把它們列為待辦、blocker 或驗收門檻。

## 第二層：execution-first 與 ownership

- 修正／實作要求預設是交付要求：在授權 scope 內實作、跑匹配驗證並整合到 `main`。Issue、branch、
  PR 與 CI 只是協作／稽核記錄，不是成果或停止點。回報先講產品變化、使用者影響、驗證與缺口。
- 除非 maintainer 明確要求規劃 backlog，AI 不得建立 candidate、TBD、pre-claim 或排隊 Issue。
  只有 writer 立即開始時才建立一張完整 execution Issue。寫入須有明確指派、完整 handoff、非
  `main` branch、獨占 `scope_globs` 與 `claimed`；maintainer 的直接要求算指派。`claim-pending`
  只可作為序列化 admission 暫態，不授予寫入權。
- 其他 writer 活躍、branch occupied 或狀態不確定時使用獨立 worktree。不得越過 scope，或修改、
  reset、rebase、cleanup、force-push 別人的 branch／worktree；語意契約重疊也算衝突。
- 首次可重建 WIP/reviewable push 後開 draft PR，持續到 acceptance、fresh exact-head green、ready 與
  merge；只有具體 safety、permission、scope 或 external blocker 才能暫停並寫回 handoff。
  Integrator 先 drain 安全 green PR；合併後 readback target／`main` 與 Issue closed，再依
  `docs/ai/MULTI_AGENT.md` 安全清除 lifecycle／assignee／refs／worktree residue。
- Accepted ADR 不可改寫。Public API、schema、DSP 順序、安全或 build contract 變更時，同步 Spec／
  ADR、tests 與 evidence。新 evidence 使用 append-only `evidence_format: 2`、綁定非 evidence Git
  blob；不預填 future squash commit、不覆寫 legacy record，更正用 `supersedes`。
- 全域快照只由 integrator 單寫；feature writer 只維護自己的 handoff、Spec、tests 與 evidence。
  換 writer／電腦前須 commit、push、更新 handoff 並留下唯一下一步。完整協定見 SPEC-0004 與
  `docs/ai/MULTI_AGENT.md`。

## 第三層：驗證門檻

所有 gate 用 PowerShell 7（`pwsh`）。缺少時記錄 permission／external blocker；未經 maintainer
明確授權不得安裝系統套件。

每個寫入切片必跑：

```powershell
pwsh -File tools/handoff-check.ps1 -Issue <n>
pwsh -File tools/delivery-audit.ps1 -Issue <n>
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
git diff --check
```

範圍觸發的額外 gate：

- build／toolchain evidence：`doctor.ps1 -CheckOnly`；C／C++、CMake、schema、contract、tests：`verify.ps1`。
- workflow／公開 release policy：`source-only-ci-check.ps1`；新／更正 evidence：`evidence-audit.ps1`。
- UI／control model：`control-model-check.ps1`、`build-preview.ps1`、`winui-shell-check.ps1`；
  engine 整合另跑 `build-engine-preview.ps1`、`engine-preview-smoke.ps1`、`control-model-engine-smoke.ps1`。
- extensions：`extension-check.ps1`；installer／distribution：`installer-check.ps1`、
  `distribution-check.ps1`；driver boundary：`driver-source-check.ps1`，需 WDK evidence 才跑 `build-driver.ps1`。
- `live-*-check.ps1` 只在明確 opt-in 時跑；只輸出匿名資料，非 `-WriteTest` 不改機器狀態，結果仍不
  是 driver／實體音訊 evidence。詳細參數與邊界見 `docs/START_HERE.md` 與各工具註解。

環境不同時記錄 fingerprint 並更新 handoff；不得重生 `config/distribution-profile.yml` 的永久 ID。
