# Hibiki DSP

> Windows 11 的 Audio Scene OS：把系統音訊、遊戲、影音、DAW 與多聲道輸出放進同一個安全、可驗證的 DSP 控制面。

Hibiki DSP 是公開開發中的 Windows 音訊平台，不是已可供一般使用者安裝的成品。長期目標是
「一鍵變好聽」的 Easy 模式，加上可選的 Expert Matrix、per-App 路由、校正、IR、VST3 與
Hibiki ASIO；所有自有原始碼、規格、建置腳本與驗證證據都以本 repository 為唯一真值。

## 公開 repository

[github.com/xup61069/Hibiki-DSP](https://github.com/xup61069/Hibiki-DSP) 是唯一官方公開 source
入口。可用下列方式在新電腦取得同一份交接資料：

```powershell
git clone https://github.com/xup61069/Hibiki-DSP.git
```

## 現在可以相信什麼

| 區域 | 已有可重跑證據 | 尚未可對外承諾 |
| --- | --- | --- |
| DSP／控制面 | C++ contract tests、RT graph、Scene、per-output volume、limiter、ISO 226-derived formula boundary、PEQ／IR 基礎 | 真實喇叭／耳機聲學效果或 ISO 認證 |
| Windows 音量 | `IAudioEndpointVolume` dB/mute bridge、safety state 與 Group Master 單次增益 contract | target 24H2 的長時間實機／服務重啟 soak |
| per-App | session catalog、暫時 handle、volume／route／route-rule command、Expert 預設與歧義 fail-closed | 所有 App 的實體 capture/re-send、Chrome 單分頁自動攔截 |
| UX | UI-independent control model、source-only WinUI Easy/Expert shell | 此機器上的 WinUI 可執行 preview、完整無障礙實測 |
| Driver／發行 | MS-PL SYSVAD-derived source boundary、INF source、installer/manifest policy | 可載入 WaveRT driver、Microsoft 簽章、Gumroad 正式交付 |

詳細已完成項目、證據與限制看 [baseline](docs/state/BASELINE.md)。不要把 contract test 或
source gate 誤解為已完成硬體驗收。

## 立即開始

目標環境是 **Windows 11 24H2+ x64、Visual Studio 2026、Windows SDK/WDK 10.0.28000.2526**。
本機若低於此版本，仍可跑 user-space contract tests，但不能宣稱 driver 或正式 preview 已驗證。

```powershell
pwsh -File tools/doctor.ps1 -CheckOnly
pwsh -File tools/verify.ps1
pwsh -File tools/control-model-check.ps1
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
```

驗證輸出一律留在 ignored 的 `.local/`；本 GitHub repository 不上傳 EXE、DLL、SYS、MSI、
MSIX、VST3 或 CI artifact。

## 給下一個 AI／換電腦

先讀 [AGENTS.md](AGENTS.md)，再依 [AI 接手頁](docs/AI_HANDOFF.md) 操作。它會指向唯一的
active handoff、目前環境限制、已驗證命令、不能碰的資料，以及 **一個**下一步。不要從聊天
紀錄、舊 registry、私人裝置 ID 或未提交的 build output 推斷專案狀態。

產品行為以 [Specs](docs/specs/INDEX.md) 為準；架構理由以 [ADRs](docs/adr/) 為準；main 的
事實以 [baseline](docs/state/BASELINE.md) 和 [evidence](evidence/0000-foundation/) 為準。

## 來源公開與正式版本

所有 Hibiki 自有 source 都會公開；driver 與 user-space 依 component license 分隔。公開 GitHub
只放 source、文字 manifest、SBOM 與 evidence。未來正式版必須由隔離 release pipeline 產生
Microsoft-signed virtual-audio driver 與 Authenticode installer，並透過 Gumroad 交付；執行期
不使用 activation、裝置綁定或 DRM。詳見 [source policy](SOURCE_POLICY.md) 與
[release spec](docs/specs/SPEC-0005-source-only-paid-release.md)。

## 目前 preview 狀態

目前可重跑的是 unsigned user-space preview baseline（`tools/verify.ps1` 與 C# control-model
check），不是可安裝產品。WinUI shell 需要 target toolchain 的 XAML build 才能產生本機 UI preview；
它即使成功也不含虛擬 driver、系統攔截或正式簽章，絕不會上傳 GitHub。

```powershell
pwsh -File tools/build-preview.ps1 -Target WinUI
```

失敗時保留 `.local/preview/` 的診斷輸出，不可把它加入 Git；只想重跑控制面可用
`-Target ControlModel`。

## 授權與貢獻

Hibiki user-space 為 GPL-3.0-only；SYSVAD-derived driver 為 MS-PL；SDK/schema 為 Apache-2.0；
文件為 CC-BY-4.0。見 [LICENSES/README.md](LICENSES/README.md)、[THIRD_PARTY.yml](THIRD_PARTY.yml)、
[CONTRIBUTING.md](CONTRIBUTING.md) 和 [TRADEMARKS.md](TRADEMARKS.md)。禁止提交私密校正資料、
endpoint/serial identity、簽章金鑰、顧客資料、ISO 受限內容或任何編譯產物。
