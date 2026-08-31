---
id: SPEC-0005
status: accepted
owner: hibiki-maintainers
authority: release-policy
last_reviewed: 2026-08-25
review_after_days: 30
related_adrs: [ADR-0001, ADR-0006, ADR-0007]
source_globs: ["installer/**", "tools/**", ".github/**", "schemas/release-manifest-v1.schema.json"]
---

# SPEC-0005：source-only GitHub 發布與無簽章交付

## 公開面

GitHub 只放原始碼、依賴鎖定、建置腳本、測試證據、SBOM、notices 與文字 release manifest。
Release page 只識別 source tag 與 release notes；不得上傳 GitHub release asset、Packages、
container、Actions artifact 或任何 EXE／DLL／SYS／MSI／MSIX／VST3／PE-COFF 輸出。Public CI
可在 ephemeral runner 編譯與測試，job 結束即刪除輸出，且不得要求、保存或使用任何 signing
permission。公開 `verify.yml` 必須同時執行 CMake/CTest、docs/source policy、WinUI source shell、
driver source boundary、extension/installer/control-model、stable identity 與 JSON parse gates；
任何 gate 失敗都不得視為可交付 source tag。

## 發布流程

1. 維護者建立受保護 annotated source tag 與 release notes；不產生或上傳 repository release
   artifact。
2. Public CI 在 ephemeral workspace 執行 user-space、DSP、driver source 與相關測試；HLK／WHCP
   與任何形式的簽章都不是驗收項目。
3. 產生 `ReleaseManifest v1` 文字紀錄，至少記錄 source tag、commit、toolchain digest、dependency
   lock digest、required non-empty `distribution_id`、payload SHA-256 清單、driver package/catalog
   內容 hash、SBOM digest 與 test run。Manifest 與 schema 不得包含 Microsoft signature thumbprint、
   installer signer thumbprint、RFC3161 timestamp 或 signed payload hash 欄位。`product_version` 為
   1–64 字元的非空字串，`toolchain_digest` 必須符合 SHA-256 hex 格式（64 字元 [0-9a-fA-F]）；
   `unsigned_files[]` 最多 1024 筆且每個路徑為 1–260 字元，每筆項目僅允許 path 與 sha256 兩個
   宣告欄位（additionalProperties false）；`tests[]` 最多 256 項且每項為 1–120 字元的非空標籤。
   所有 SHA-256 digest 欄位均帶明確 `maxLength`（64 字元），`source_tag` 必須完全符合
   `v<major>.<minor>.<patch>` 加上至多 32 個 `[A-Za-z0-9._:+-]` 後綴。人類可讀的 `product_version`、
   `distribution_id`、`unsigned_files[].path` 與 `tests[]` 標籤皆拒絕 C0/C1 控制字元（含 DEL），
   且保留既有長度上限，與安裝程式的嚴格模式一致。
4. 使用者依 source policy 自行從 source tag 重建；repository 不交付 binary、簽章或 HLK 保證。

## GPL 與更新語意

專案沒有付費授權、paid channel、activation、序號、裝置綁定、功能鎖或遙停。GPL 權利完整保留；
任何人可重建、修改與再散布。更新資訊只能指向 source tag／release notes，不得內建 binary 下載、
簽章 metadata 讀取或憑證邏輯。

## 不可逾越的邊界

私密金鑰、憑證、購買的 equal-loudness 標準文件與顧客資料永不進 repository、Issue、prompt 或 CI log。公開面
永遠以可重建的 source、source tag、文字 manifest、SBOM、notices 與其內容 hash 為準。
