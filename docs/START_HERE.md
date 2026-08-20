# Hibiki DSP：AI 接手入口

本 repository 才是 Hibiki 的長期記憶。不要依賴上一個 AI、上一台電腦或
未提交的聊天內容。

## Fresh clone 流程

1. 讀 root `AGENTS.md`、本檔與 `docs/PROJECT_MAP.md`。
2. 確認 branch、HEAD、working tree 與 dependency lock。
3. 執行 `pwsh -File tools/doctor.ps1 -CheckOnly`。
4. 執行 `pwsh -File tools/probe-environment.ps1`，環境資料只寫入 `.local/`。
5. 找到要處理的 GitHub Issue，讀 `docs/tasks/active/<issue>.md`。
6. 讀 handoff 指定的 Spec、ADR、source、tests 與 evidence。
7. 先執行 handoff 的 baseline smoke test；結果不一致時先標記 stale/conflict。
8. 修改後執行 `tools/verify.ps1`、`tools/docs-check.ps1` 與
   `tools/source-policy.ps1`；若改動 extension、installer 或 control model，再執行
   `tools/extension-check.ps1`、`tools/installer-check.ps1`、`tools/control-model-check.ps1`。
   任何 identity/config 變更都必須再執行 `tools/distribution-check.ps1`。

## 文件權威順序

產品行為看 accepted Spec；架構理由看 accepted ADR；實際完成狀態看 source、
tests 與 evidence；main 的合併狀態看 `docs/state/BASELINE.md`；分支工作看
Issue 與 handoff。兩份權威文件衝突時停止修改，建立 `DOC-CONFLICT`，不要猜。

## 換 AI／換電腦

更新 active handoff，記錄 base commit、環境 fingerprint、已完成內容、失敗測試、
剩餘工作、`Next safe action` 與最多五個 resume commands。建立 WIP commit 並 push
branch。真實裝置資料與 calibration 留在 `.local/`，只提交 schema 和匿名 fixture。

## 尚未完成的主要區域

- `driver/`：SYSVAD-derived WaveRT/KS 虛擬端點與 Windows volume nodes。
- `src/`：即時 graph、Matrix、ISO fit、scene safety、device switch 與 output sinks。
- `apps/`：WinUI 3 易用模式與 Expert matrix/graph UI。
- `asio/`：預設為 stream model；需要本機 pinned ASIO SDK 時可開啟 optional native COM
  transport（不進 public CI，也不提交 DLL）。`vst-host/`、`extensions/`、`installer/` 仍依
  Spec 漸進加入。
