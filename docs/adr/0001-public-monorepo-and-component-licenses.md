# ADR-0001：公開 monorepo 與 component licenses

## Status

Accepted

## Date

2026-08-21

## Context

Hibiki 必須可由不同電腦、不同 AI 與外部貢獻者接續開發，同時包含 GPL user-space、
SYSVAD-derived driver、Windows toolchain 與付費簽章安裝器。單一 blanket license 會掩蓋
driver 與第三方依賴的義務。

## Decision

使用一個公開 monorepo。Hibiki user-space 採 GPL-3.0-only；driver 保留 MS-PL；SDK/schema
採 Apache-2.0；文件 CC-BY-4.0；每個依賴固定 source、commit、hash、SPDX 與 redistribution obligations。
GitHub 只發布 source 與文字驗證資訊，正式 signed installer 透過 Gumroad 交付。

## Alternatives considered

- 全部使用單一 GPL：拒絕，SYSVAD-derived code 不能任意改標 GPL。
- 封閉 installer／runtime DRM：拒絕，與「所有 Hibiki source 公開」及 GPL 權利衝突。
- 多個 repository：拒絕作為起點，會讓 AI handoff、schema 與 driver/engine ABI 分裂。

## Consequences

公開 source、stable IDs、ADR、Spec 與 evidence 成為跨 AI 的唯一記憶。Microsoft SDK/WDK、
Partner Center 與簽章服務仍是外部 prerequisite；正式 release 必須額外做 SBOM、簽章與 source mapping。
