# ---
# id: ADR-0006
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-25
# last_reviewed: 2026-08-25
# review_after_days: 90
# supersedes: [ADR-0003]
# related_specs: [SPEC-0005]
# source_globs: ["installer/**", "tools/**", ".github/**", "schemas/release-manifest-v1.schema.json"]
# ---

# ADR-0006：無 HLK、無簽章的 source-only 發行

## Status

Accepted

## Date

2026-08-25

## Context

ADR-0003 曾把正式交付建立在 Microsoft driver signing、Authenticode installer、Gumroad 付費通道與 HLK／WHCP 審查之上。maintainer 已於 2026-08-25 決定：Hibiki 不需要 HLK，也不需要任何簽章；相關帳號、憑證、付費通道與審查流程不再是產品目標、驗收條件或發行前置。

## Decision

- 專案不以 HLK／WHCP、Microsoft signing、Authenticode、test-signing 或任何憑證作為開發、驗收或發行要求。
- `ReleaseManifest` 只描述可重建內容：source identity、toolchain／dependency digests、payload SHA-256 清單、driver package/catalog 內容 hash 與測試紀錄；不得要求 Microsoft signature thumbprint、installer signer thumbprint、RFC3161 timestamp 或 signed payload hash 欄位。
- 公開 repository 維持 source-only：不提交 EXE/DLL/SYS/MSI/MSIX/VST3/PE-COFF、簽章檔或金鑰；public CI 仍不得上傳 artifact、package 或執行簽章。
- 使用者是否載入本機建置的 kernel driver 由使用者自行決定並自行處理機器狀態與相容性；這不是官方支援的消費者安裝路徑，也不是驗收門檻。

## Consequences

- 開發與驗收不再被外部帳號、憑證、付費通道或 HLK 流程卡住；文件不再把這些項目列為未完成缺口。
- Secure Boot/HVCI 環境可能拒絕載入未簽章 driver；這是已知平台限制，不是待辦的 signing 工作。
- ADR-0003 的付費簽章交付模式及其 Gumroad／Partner Center 決策由本 ADR 取代；GPL 授權與 source-only 政策不變。
