---
id: SPEC-0005
status: accepted
owner: hibiki-maintainers
authority: release-policy
last_reviewed: 2026-08-31
review_after_days: 30
related_adrs: [ADR-0001, ADR-0006, ADR-0007, ADR-0008, ADR-0009]
source_globs: ["installer/**", "tools/**", ".github/**", "schemas/release-manifest-v1.schema.json", "SOURCE_POLICY.md"]
---

# SPEC-0005：source-only GitHub 發布與無簽章交付

## 公開面

GitHub 只放原始碼、依賴鎖定、建置腳本、測試證據、SBOM、notices 與文字 release manifest。
Release page 只識別 source tag 與 release notes；不得上傳 GitHub release asset、Packages、
container、Actions artifact 或任何 EXE／DLL／SYS／MSI／MSIX／VST3／PE-COFF 輸出。Public CI
可在 ephemeral runner 編譯與測試，job 結束即刪除輸出，且不得要求、保存或使用任何 signing
permission。公開 `verify.yml` 必須同時執行 CMake/CTest、docs/source policy、WinUI source shell、
driver source boundary、extension/installer/control-model、stable identity 與 JSON parse gates；對
   `v*` source tag 還必須執行 read-only source-tag provenance gate，且該 step 必須為 tag-scoped
   並不得用 `continue-on-error` 忽略失敗。任何 gate 失敗都不得視為可交付 source tag。

## 發布流程

1. 維護者先完成 source commit，再建立只新增或更新
   `release/manifests/<tag>.json` 的 single-parent provenance metadata commit，並以受保護 annotated
   source tag 直接指向該 metadata commit（不得經由 nested tag）；不產生或上傳 repository release
   artifact。為避免 Git hash
   自我參照，manifest 的 `source_tag` 必須等於 tag，而 `source_commit` 必須等於 metadata commit
   的唯一直接 parent；metadata commit 不得改動任何其他 product 或 policy 路徑。
2. Public CI 在 ephemeral workspace 執行 user-space、DSP、driver source 與相關測試；對 `v*` tag
   另驗證 annotated tag 的直接 commit target、唯一 parent、regular `100644` manifest blob、既有
   `ReleaseManifest v1` schema、tag/commit identity 與不做 rename 偵測的 metadata diff。HLK／WHCP
   與任何形式的簽章都不是驗收項目。
3. 官方 source tag 使用 `SourceReleaseManifest v1`（見 SPEC-0025），外部安裝程式則支援外部交付的 `ReleaseManifest v1`，至少記錄 source tag、commit、toolchain digest、dependency
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
