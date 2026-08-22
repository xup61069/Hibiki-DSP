# ---
# id: ADR-0005
# status: accepted
# owner: hibiki-maintainers
# authority: architecture
# date: 2026-08-23
# last_reviewed: 2026-08-23
# review_after_days: 90
# supersedes: []
# related_specs: []
# source_globs: ["build/toolchain-lock.yml", "tools/doctor.ps1"]
# ---

# ADR-0005：SDK/WDK 改採最低基線鎖定

## Status

Accepted

## Date

2026-08-23

## Context

原 toolchain lock 精確釘住 SDK/WDK 10.0.28000.2526，但該版本從未實際安裝在現有機器上；本機可用且已被 Issue #394 驗證能編出 kernel-mode .sys 的是 26100 家族（SDK 套件 10.1.26100.8249、WDK 套件 10.1.26100.6584）。同時，SDK 與 WDK 的 QFE 更新週期獨立，精確單一版號的鎖法會在其中任一元件更新時失效。

## Decision

toolchain lock 對 Windows SDK 與 WDK 改為最低基線 **10.0.26100**：

- on-disk kit 目錄（Include/build/Tools）必須存在且版本 ≥ 10.0.26100.0。
- 安裝套件 metadata 必須屬於 10.1.26100.* 家族（SDK 與 WDK 各自獨立比對）。
- doctor.ps1 以家族前綴比對取代精確字串比對，SelfTest 更新為五個案例（family-match、missing-sdk-metadata、wrong-family-package、below-minimum-directory、missing-kit-directory）。

此決策依據 `build/toolchain-lock.yml` notes 的規定，以新 ADR 記錄。evidence 引用 Issue #394 在本機 26100 工具鏈上成功建置 .sys 並通過 Inf2Cat 的紀錄（commit 8871d54、evidence/0000-foundation/driver-sys-build-v1.json）。

## Consequences

- M0 的 toolchain 判定不再被不存在的版號卡死；本機即符合最低要求。
- 未來 SDK 或 WDK 任一元件 QFE 更新不需要改 repo；只有跨家族升級（例如 10.0.30xxx）才需要新 ADR。
- 正式 target 機器若使用不同 QFE，仍以同一基線判定，不需逐一登記版號。
