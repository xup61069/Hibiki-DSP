---
id: SPEC-0025
status: accepted
owner: hibiki-maintainers
authority: release-policy
last_reviewed: 2026-08-31
review_after_days: 30
related_adrs: [ADR-0006, ADR-0007, ADR-0008, ADR-0009]
source_globs: ["tools/release-provenance-check.ps1", "schemas/source-release-manifest-v1.schema.json", "SOURCE_POLICY.md"]
---

# SPEC-0025：Source-Tag Manifest Provenance 契約

## 目標與範圍

定義官方 GitHub `v*` annotated source tag 所掛載之純文字 manifest 契約。所有宣告內容必須可從 Git 歷史中的直接 parent commit 完全獨立驗證，不得包含未發行或不存在的二進位檔案雜湊。

## 契約規範

1. **Tag 與 Commit 拓撲**：
   - 官方 tag 必須為 annotated tag（`type tag`），直接指向單一 commit（`type commit`）。
   - Tag 所指向的 metadata commit 必須恰好有一個 parent commit（即 source commit），且 metadata commit 唯一的變更必須為 `release/manifests/<tag>.json`。
2. **Manifest 內容結構（SourceReleaseManifest v1）**：
   - `schema_version`: 必須為 `1`。
   - `product_version`: 1–64 字元非空可列印字串。
   - `source_tag`: 與 Git tag 名稱完全一致。
   - `source_commit`: 與 metadata commit 的 parent commit SHA 完全一致（40 字元 hex）。
   - `distribution_id`: 1 字元以上可列印字串。
   - `toolchain_lock`: 包含 `path` 與 `sha256`。
   - `dependency_lock`: 包含 `path` 與 `sha256`。
   - `sbom`: 包含 `path` 與 `sha256`。
   - `release_notes`: 包含 `path` 與 `sha256`。
   - `notices`: 包含 `path` 與 `sha256`。
   - `source_files`: 宣告之主要原始碼檔案清單，每項包含 `path` 與 `sha256`。
   - `tests`: 測試標籤字串陣列。
3. **內容驗證（Content-Verifiable）**：
   - Provenance 驗證工具必須在 Git 歷史中直接以 `git cat-file blob <source_commit>:<path>` 計算 raw blob SHA-256，並確認與 manifest 中宣告的 `sha256` 完全一致。
   - 任何路徑不存在、重複、路徑不安全（包含 `..` 或絕對路徑）、非一般檔案（例如 symlink）或雜湊不相符時，必須 fail-closed。