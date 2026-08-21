# VST3 state compatibility review

本文件是第三方 VST3 plugin state 進入 Hibiki `Vst3PluginStateMigrationRegistryV1`
前的人工審查清單。VST3 state 是 plugin 定義的 opaque bytes；Hibiki 不解析、不修改，
也不把未審查的 state 放進 GitHub、Issue、AI context、CI artifact 或 Gumroad 交付物。

## 准入資料

- 記錄 plugin name、VST3 class UID、module SHA-256、來源版本與目標版本。
- 每一條 migration 只能有一個明確的 `(source identity, source version,
  target identity, target version)`；不得用 wildcard 或只依檔名判斷身份。
- Store metadata 必須與 registry identity 完全相等；class UID、module hash 或
  版本不符時，Scene preflight 必須 fail-closed。
- 變更說明需描述 state 來源、目標、大小上限、成功／失敗行為與 reviewer；不能提交
  真實使用者 state 或未授權 SDK/標準資料。

## 必跑測試

1. identity mismatch、version mismatch、缺少 handler、handler 回傳失敗都必須拒絕，
   且不得改變既有 Scene、graph revision 或 active graph。
2. 檢查 0 bytes、1 byte、`1 MiB`、`1 MiB + 1`、destination 太小與 null buffer；
   上限外或容量不足一律拒絕。
3. 檢查 duplicate target identity、source/target 相同但版本不同，以及重複註冊；
   registry 必須拒絕歧義規則。
4. 呼叫期間只能使用 caller-owned buffer；不得配置、鎖定、等待、存取網路／檔案系統、
   啟動 plugin process 或從 RT callback 執行 migration。
5. Scene preflight 成功後才允許 Prepare；Commit 前任一 state 錯誤都必須可 rollback，
   並保留上一個 Scene 與 revision。

## 發行與隱私

- 公開內容只放 identity metadata、schema、測試結果摘要與 source commit；state bytes、
  個人資料、商業 plugin 內容與 dump 必須留在受控的本機／隔離環境。
- 未完成審查或無法取得合法 redistribution 權利的 plugin，標記為 quarantined，
  不得進入 Low Latency Lane 的 trusted/certified 清單。
- 審查結果需附 `TaskHandoff`／`EvidenceManifest` 的 commit、toolchain 與測試範圍，
  以便換裝置或換 AI 後重做，而不是依賴聊天記憶。

