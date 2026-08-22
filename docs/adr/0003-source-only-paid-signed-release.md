# ---
# id: ADR-0003
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-21
# last_reviewed: 2026-08-21
# review_after_days: 90
# supersedes: []
# related_specs: [SPEC-0005]
# source_globs: ["installer/**", "tools/**", ".github/**", "schemas/release-manifest-v1.schema.json"]
# ---

# ADR-0003：source-only 公開與付費簽章交付

## Status

Accepted

## Date

2026-08-21

## Context

Hibiki 要讓所有自有程式碼可被 GitHub、不同電腦與不同 AI 接續閱讀，但 Windows 虛擬
audio driver 在正式安全開機環境仍需要受信任簽章。若把可安裝 binary 放進 GitHub，會
違反 source-only 發行約定；若把簽章流程交給每個 runtime 使用者，又無法保證一般安裝體驗。

## Decision

採兩層交付：公開 repository 只提供可重建的 unsigned source layer；官方隔離環境產生
Microsoft-signed driver 與 Hibiki Authenticode installer，透過 Gumroad 一次買斷、終身更新
交付。runtime 不驗證購買資格，所有買家拿相同 canonical hash，GPL 權利完整保留。

## Consequences

- GitHub 可被新 AI 直接 clone、檢查、編譯與測試，不需要私人 binary 才能理解架構。
- 官方仍可提供 Secure Boot／HVCI 可安裝的可信 driver，但簽章 secret 不進 public CI。
- 付費價值是簽章、便利、更新與支援；不能建立在禁止 GPL 再散布或 DRM 上。
- source-only 使用者若自行安裝 driver，必須自行處理 test-signing／BYO signing；這不是消費者
  一鍵安裝流程，文件必須明示。

## Alternatives rejected

- GitHub 直接放 unsigned／signed binary：違反 source-only 與供應鏈 custody。
- runtime license key／activation：破壞 GPL 離線使用與再散布權利。
- 把 production certificate 放進 GitHub Actions：fork／PR 可執行任意程式碼，不是安全邊界。
