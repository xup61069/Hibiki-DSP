---
id: SPEC-0026
status: accepted
owner: hibiki-maintainers
authority: release-policy
last_reviewed: 2026-09-01
review_after_days: 30
related_adrs: [ADR-0006, ADR-0007, ADR-0008, ADR-0009, ADR-0010, ADR-0011]
source_globs: ["SOURCE_POLICY.md", "README.md", "schemas/portable-preview-package-manifest-v1.schema.json", "tools/portable-preview-package.ps1", "tools/portable-preview-package-check.ps1", "tools/build-preview.ps1", "tools/docs-check.ps1", "tools/distribution-check.ps1"]
---

# SPEC-0026：Portable User-Space Preview Distribution

## 目標與範圍

定義官方 `v1.0.0` GitHub Release 唯一允許的使用者 binary：unsigned、driver-free、
Windows x64 的 DesktopCompat portable preview。使用者下載、比對 SHA-256、解壓並雙擊
entry point 後，可以在不安裝 .NET runtime、driver 或服務的情況下啟動控制面 preview。

這是 ADR-0011 的窄例外，取代 SPEC-0005 的「Release page 不得有任何 asset」條文；其餘
source-only、no-signing、no-HLK、no-paid-channel 與 source-tag provenance 規則不變。

## 發布合約

1. Release tag 不得改寫；DesktopCompat payload 必須從乾淨、detached 的 annotated `v1.0.0`
   source-tag checkout 建置。package manifest 必須記錄 tag commit、其 source manifest 宣告的
   source commit、raw source-manifest SHA-256 與實際 packager commit。
2. Release 僅可有兩個人工上傳 asset：`Hibiki-DSP-v1.0.0-portable-win-x64.zip` 與同名
   `.sha256` sidecar。sidecar 必須恰有一行：`<64 lowercase SHA-256 hex> *Hibiki-DSP-v1.0.0-portable-win-x64.zip`。
   GitHub 自動產生的 source archive 不算人工上傳 asset。
3. ZIP root 必須有 `hibiki-portable-preview-manifest-v1.json`。除該 manifest 外，ZIP regular
   files 必須與 manifest `files[]` 完全相同；不允許 duplicate、absolute、drive-qualified、
   backslash、`.`、`..`、control-character、Windows reserved-name、trailing dot/space path、
   symlink/reparse point、blocked driver/installer/updater/debug-symbol extension 或未宣告檔案。
4. Manifest 必須符合 `PortablePreviewPackageManifest v1`，記錄 package kind、產品版本、
   source tag/commits 與 source-manifest hash、distribution identity、Windows 11 x64 platform、
   self-contained runtime、entry point、明確 driver/installer exclusion、limitations 及每個 payload
   的 SHA-256/size。ZIP 自身 hash 只能放在 sidecar，不得寫回 embedded manifest。每個 package
   必須另含 `PORTABLE_PREVIEW_README.txt` 與該 source-tag checkout 的 `THIRD_PARTY.yml`；
   self-contained .NET runtime 是可歸屬、已 hash 的 user-space payload，不是 opaque dependency。
5. `Hibiki.DesktopPreview.exe` 是唯一 entry point。它是 DesktopCompat user-space preview；
   package 不提供 Engine Preview、driver 或任何會改變 machine state 的 launcher。它可保存使用者
   UI 偏好到 per-user profile，但不得安裝、註冊或變更系統音訊元件。

## 驗證與使用流程

1. Releaser 從 clean detached `v1.0.0` checkout 驗證 provenance，並用
   `tools/build-preview.ps1 -Target DesktopCompat -SmokeTest` 建置。
2. `tools/portable-preview-package.ps1` 產生 ZIP/sidecar；
   `tools/portable-preview-package-check.ps1` 必須以 `-SourceRepository` 指向乾淨、detached 的
   `v1.0.0` source-tag checkout，驗證 annotated tag 的直接 target、source commit、raw
   source-manifest SHA-256、distribution identity、archive sidecar、safe extraction、manifest
   與每個 payload hash，並在乾淨解壓目錄執行 entry-point launch smoke。
3. 發布後重新下載兩個 GitHub asset，重跑 archive check，並以
   `-ExpectedArchiveSha256 <上傳前獨立記錄的值>` 比對下載 ZIP；不可只用同一下載位置的
   sidecar。讀回 release tag、asset 名稱與 SHA-256。這些是 package/launch evidence，不是
   driver 或 physical-audio evidence。

使用者可在 PowerShell 執行：

```powershell
$zip = '.\Hibiki-DSP-v1.0.0-portable-win-x64.zip'
$expected = ((Get-Content "$zip.sha256" -Raw).Trim() -split ' \*', 2)[0]
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -cne $expected) { throw 'SHA-256 mismatch; do not run this download.' }
Expand-Archive .\Hibiki-DSP-v1.0.0-portable-win-x64.zip .\Hibiki-DSP
Start-Process .\Hibiki-DSP\Hibiki.DesktopPreview.exe
```

Windows may display an unsigned/reputation warning. Continue only after the hash matches the
official Release sidecar. The preview opens an offline control surface; it does not start an
engine, create an endpoint, or produce physical audio.

## 不可逾越的限制

package 不含 driver、INF、CAT、SYS、installer、service、endpoint control、WaveRT streaming、
WASAPI output、VST3、automatic update、signing 或憑證。它不能聲稱可聽實體音訊、正式 consumer
install/upgrade/rollback、長時間 soak 或 accessibility certification。Git 與 CI 仍不得存放或
上傳 binary。
