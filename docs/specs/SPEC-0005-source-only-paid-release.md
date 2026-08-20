---
id: SPEC-0005
status: accepted
owner: hibiki-maintainers
authority: release-policy
last_reviewed: 2026-08-21
review_after_days: 30
related_adrs: [ADR-0001]
source_globs: ["installer/**", "tools/**", ".github/**", "schemas/release-manifest-v1.schema.json"]
---

# SPEC-0005：source-only GitHub 與官方簽章交付

## 公開面

GitHub 只放原始碼、依賴鎖定、建置腳本、測試證據、SBOM 與文字 release manifest。禁止
release asset、Packages、container、Actions artifact 或任何 EXE／DLL／SYS／MSI／MSIX／
VST3／PE-COFF 輸出。Public CI 可在 ephemeral runner 編譯與測試，job 結束即刪除輸出。

## 官方建置 custody

1. 受保護 annotated source tag 觸發兩個 clean builder，產生並比對 unsigned hashes。
2. 通過 user-space、DSP、Driver Verifier、HLK、升級／回復與音訊 soak 測試後，提交
   exact driver package 給 Microsoft Partner Center。
3. 將 Microsoft-signed driver 與 user-space payload 交給隔離 release builder；Hibiki
   Authenticode 加 RFC3161 timestamp 簽署 installer。
4. 在 Secure Boot/HVCI 開啟且 TESTSIGNING 關閉的乾淨環境安裝、升級、rollback、uninstall。
5. 產生 `ReleaseManifest v1`，至少記錄 source tag、commit、toolchain、dependency lock、
   unsigned/signed hash、driver signature、installer signer、SBOM 與 test run。
6. 由人類手動把同一份 canonical installer 上傳 Gumroad；AI 不接觸憑證、Partner Center
   或 Gumroad 帳密。

## GPL 與更新語意

付款取得的是官方簽章建置、便利交付、更新入口與支援，不是 runtime 授權。安裝後離線
永久可用，無序號、activation、裝置綁定、功能鎖或遙停。GPL 允許買家再散布，因此同版
所有買家取得相同 hash，不嵌 email、水印或 buyer-specific binary。

App 只匿名讀取簽章 metadata；發現更新時開啟 Gumroad Library，不直接下載成品。若專案
停止維護，最後一版官方 installer、manifest 與驗證資訊公開，保留既有使用者的離線使用權。

## 不可逾越的邊界

簽章私鑰、購買的 ISO 標準文件、顧客資料與 Partner Center credentials 永不進 repository。
簽章後檔案不是 bit-for-bit 可重建層；公開可重建的是 unsigned payload 與其 hash。
