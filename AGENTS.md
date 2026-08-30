# Hibiki DSP AI 工作規則

只先讀本檔與 `docs/START_HERE.md`；任務真值依序是 Issue handoff、指定 Spec／ADR、source、tests
與 evidence。聊天、AI memory、IDE 規則不是專案真值。

## 不可違反

- RT audio thread 不配置、mutex、等待或呼叫 COM／UI／檔案系統。
- MS-PL driver 與 GPL user-space 僅以版本化 Apache ABI／IPC 互動；不得連結。高流量 path 可用固定容量
  shared-memory／ring。
- 不宣稱控制廠商 ASIO、WASAPI Exclusive 或 RAW；user-space／source／build／CI 不是 driver、WaveRT
  或實體音訊證據，控制命令入列也不是已套用音訊。HLK／簽章不是待辦、blocker 或驗收門檻。
- Git 只存 source：不得提交 binaries、PE／COFF、installer、VST3、憑證或私鑰；裝置 ID、校正檔、序號
  與私人路徑只放 `.local/`。
- 不反編譯或繞過閉源保護；只用開源程式、官方文件與合法 black-box 觀察。受限 equal-loudness 文件、掃描、
  完整表格與資料不得進 repo、Issue、prompt 或 RAG。

## 交付或交接

- 修正／實作要求預設是交付要求：`fix`、`continue`、`cleanup` 都要實作、驗證並整合到 `main`。Issue、branch、
  PR 與 CI 只是協作／稽核記錄，不是成果或停止點；除非 maintainer 要求 backlog，
  不得建立 candidate、TBD、pre-claim 或排隊 Issue，只在立即開始時建立一張完整 execution Issue。
- 寫入須有明確指派、完整 handoff、非 `main` branch、獨占 `scope_globs` 與 `claimed`；maintainer 直接要求
  算指派。`claim-pending` 不授予權限。唯有具體 safety、permission、scope 或 external blocker 才能暫停，且
  必須 commit、push、更新 handoff 與唯一下一步。
- 有其他 writer、occupied branch 或不確定性就用獨立 worktree；不得越 scope 或修改、reset、rebase、cleanup、
  force-push 別人的 branch／worktree。語意契約重疊也是衝突。
- 首個可重建 push 後立刻開 draft PR，持續到 acceptance、fresh exact-head green、ready、merge。Integrator 先
  drain 安全 green PR，再 readback `main` 與 Issue closed，安全清理 residue。全域快照只由 integrator 單寫。
  Accepted ADR 不可改寫；public API／schema／DSP order／safety／build contract 變更要同步 Spec／ADR、tests、
  evidence。evidence 是 append-only `evidence_format: 2`，綁非-evidence blob；更正用 `supersedes`。

## 驗證

所有 gate 用 PowerShell 7（`pwsh`）；缺少時記錄 permission／external blocker；
未經 maintainer 明確授權不得安裝系統套件。每個寫入切片：

```powershell
pwsh -File tools/handoff-check.ps1 -Issue <n>
pwsh -File tools/delivery-audit.ps1 -Issue <n>
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
git diff --check
```

- build／toolchain：`doctor.ps1 -CheckOnly`；C／C++、CMake、schema、contract、tests：`verify.ps1`。
- workflow／public release policy：`source-only-ci-check.ps1`；evidence：`evidence-audit.ps1`。
- UI／control model：`control-model-check.ps1`、`build-preview.ps1`、`winui-shell-check.ps1`；engine integration
  另加 `build-engine-preview.ps1`、`engine-preview-smoke.ps1`、`control-model-engine-smoke.ps1`。
- extensions：`extension-check.ps1`；installer／distribution：`installer-check.ps1`、`distribution-check.ps1`；
  driver boundary：`driver-source-check.ps1`，需 WDK evidence 才跑 `build-driver.ps1`。
- `live-*-check.ps1` 僅明確 opt-in；非 `-WriteTest` 不改機器，結果不等於 driver／實體音訊 evidence。環境差異
  記錄 fingerprint，不得重生 `config/distribution-profile.yml` 的永久 ID。
