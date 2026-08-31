# ---
# id: ADR-0009
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-31
# last_reviewed: 2026-08-31
# review_after_days: 90
# related_specs: [SPEC-0005, SPEC-0025]
# source_globs: ["schemas/source-release-manifest-v1.schema.json", "tools/release-provenance-check.ps1", "docs/specs/SPEC-0025-source-tag-manifest-provenance.md", "SOURCE_POLICY.md"]
# ---

# ADR-0009：Source-Tag Manifest 與外部 Package Manifest 邊界分離

## Status

Accepted

## Context

本專案採 source-only 發布模式，官方 GitHub 只存放原始碼與純文字 metadata，不發布任何二進位執行檔、驅動程式套件或安裝檔。在先前的設計中，`ReleaseManifest v1`（`schemas/release-manifest-v1.schema.json`）包含 `driver_package` 與 `installer` 欄位，適用於外部建置系統或離線安裝程式對外部交付套件的驗證。

然而，當官方 GitHub 在建立 source tag 時，若使用該 schema 作為 tag metadata，則必須為 repository 內根本不存在的二進位產物捏造 SHA-256 雜湊，造成不實聲明與 provenance 無法驗證。

## Decision

1. **契約分離**：
   - 官方 source tag 使用專屬的 `SourceReleaseManifest v1`（`schemas/source-release-manifest-v1.schema.json`）。該 manifest 僅包含可從 Git source commit 獨立比對的純文字宣告（包括 toolchain lock、dependency lock、SPDX SBOM、release notes、notices 與 source files 的路徑與 SHA-256），並明確記錄 `release_kind: source-only` 與 driver／installer 均為 `not-published`。
   - 保留 `ReleaseManifest v1` 供外部安裝程式（`HibikiSetup.ps1`）在使用者外部交付套件時使用，兩者職責分明。
2. **嚴格 Provenance 驗證**：
   - Source tag provenance 檢查（`tools/release-provenance-check.ps1`）直接驗證 annotated tag topology、metadata commit 單親節點、`release/manifests/<tag>.json` 的 schema 符合性、tag/version、`distribution_id` 與 source profile 的一致性，以及 manifest 內所有宣告路徑與 SHA-256 雜湊與父 source commit 的 raw blob 內容完全吻合。
3. **無捏造產物**：
   - Source tag manifest 絕不包含任何未發行的驅動、installer 或 catalog 欄位。

本 ADR 僅取代 ADR-0006／ADR-0007 中把官方 source-tag metadata 稱為
`ReleaseManifest v1` 的狹義描述；其 source-only、無簽章與公開重建的決策維持不變。

## Consequences

- 官方發布的 source tag 完全可由任何人透過公開 Git 歷史獨立重現與檢驗。
- 專案維持 source-only 原則，不做出任何不實的二進位、驅動簽章或安裝套件宣告。
