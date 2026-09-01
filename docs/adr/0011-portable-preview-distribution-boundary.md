# ---
# id: ADR-0011
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-09-01
# last_reviewed: 2026-09-01
# review_after_days: 90
# supersedes: [ADR-0007, ADR-0010]
# related_specs: [SPEC-0005, SPEC-0025, SPEC-0026]
# source_globs: ["SOURCE_POLICY.md", "README.md", "schemas/portable-preview-package-manifest-v1.schema.json", "tools/portable-preview-package.ps1", "tools/portable-preview-package-check.ps1", "tools/build-preview.ps1", "tools/docs-check.ps1", "docs/specs/SPEC-0026-portable-user-space-preview-distribution.md"]
# ---

# ADR-0011：Portable User-Space Preview 發布邊界

## Status

Accepted

## Context

ADR-0007 與 ADR-0010 正確地把 Git source tag 與文字 provenance 和未經驗證的
driver/installer binary 分開。然而，純 source tag 要求一般使用者先準備完整開發工具鏈，
無法達到「下載後即可看看並操作 Hibiki 控制面」的最低可用體驗。

現有 DesktopCompat 是 self-contained、無 driver 的 Windows user-space preview。它可以成為
一個窄且可驗證的 portable download，但不能因此被誤稱為 driver、WaveRT、endpoint control
或實體音訊發行。

## Decision

- 本 ADR 只取代 ADR-0007 與 ADR-0010 中「GitHub 絕不提供任何 binary release asset」的部分。
  Git repository、source tag、CI cache 與 Actions artifacts 仍一律 source-only；`v1.0.0`
  tag 與其 `SourceReleaseManifest v1` 不得改寫。
- 維護者可手動在既有 source tag 的 GitHub Release 上傳**恰好一個** unsigned Windows x64
  portable DesktopCompat ZIP，以及該 ZIP 的單一 SHA-256 sidecar。它不是 installer，不能含
  driver、INF、CAT、SYS、service、VST3、package manager、automatic update 或 signing material。
- ZIP 必須含 `PortablePreviewPackageManifest v1`、使用說明與每個 regular payload file 的 SHA-256。
  self-contained .NET runtime 檔案是可歸屬的 user-space payload，必須和其他檔案一樣被清單、hash，
  並附帶 source tag 中的 `THIRD_PARTY.yml`；不得帶入不明來源或未宣告的 prebuilt dependency。
  package checker 必須 fail closed 驗證 source tag/commit、safe archive paths、沒有 reparse point、
  完整 file set、hash 與 self-contained entry point。
- 公開 CI 不得 build/upload/release package；產物只能由維護者在可重現的本機 source-tag checkout
  建置、驗證並手動上傳。這個例外不建立 binary custody、driver install 保證、signing requirement、
  HLK/WHCP requirement 或付費 channel。

## Consequences

- 一般 Windows x64 使用者能下載、驗證、解壓並啟動 driver-free preview，不需安裝 .NET runtime
  或要求 administrator。
- Release 頁與 package README 必須明示其僅為 user-space preview：不包含 driver，不能證明
  WaveRT streaming、endpoint control、實體音訊、正式 accessibility 或 consumer upgrade/rollback 行為。
- DesktopCompat package 不會啟動 Engine Preview；它可開啟離線控制面，連線狀態預設為 disconnected，
  並且只會留下使用者自己的 UI 偏好檔，絕不安裝或變更系統音訊元件。
- `SourceReleaseManifest v1` 保持 source-only；external portable package 有獨立 manifest，避免把
  不存在的 binary hash 寫進 tag provenance。
