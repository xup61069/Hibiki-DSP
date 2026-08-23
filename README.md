# Hibiki DSP

> Windows 11 的 Audio Scene OS：把系統音訊、遊戲、影音、DAW 與多聲道輸出放進同一個安全、可驗證的 DSP 控制面。

Hibiki DSP 是一個公開開發中的 Windows 音訊平台。長期目標是「一鍵變好聽」的 Easy
模式，加上可選的 Expert Matrix、per-App 路由、校正、IR、VST3 與 Hibiki ASIO；所有
自有原始碼、規格、建置腳本與驗證證據都以本 repository 為唯一真值。
**它目前不是可供一般使用者安裝的成品**，但每一行已完成的程式碼都有可重跑的測試與
證據。

> **English**: Hibiki DSP is an open-source, in-development Windows audio platform —
> an "Audio Scene OS" that unifies system audio, games, media, DAW and multichannel
> output behind one safe, verifiable DSP control plane. This is *not* a consumer
> installer yet; everything implemented so far ships as source with reproducible
> tests. Documentation is primarily in Traditional Chinese.

## 目錄

- [給一般訪客](#給一般訪客)：這是什麼、目前狀態、怎麼試
- [給 AI 協作者](#給-ai-協作者)：接手入口與必跑命令
- [邁向 V1](#邁向-v1還差什麼)：距離第一個正式版的誠實評估
- [預覽與驗證詳情](#預覽與驗證詳情)：所有 preview 與 probe 的完整說明
- [來源公開與正式版本](#來源公開與正式版本)／[授權與貢獻](#授權與貢獻)

---

## 給一般訪客

### 這個專案在做什麼？

想像 Windows 的音訊控制中心：遊戲、電影、瀏覽器、DAW 各有各的音量與輸出，但系統
只給你一條總音量。Hibiki DSP 要做的是在 Windows 與你的喇叭／耳機之間放進一層
可驗證的 DSP：

- **Easy 模式**：一鍵依場景（遊戲／電影／音樂）調好聲音。
- **Expert 模式**：多聲道 Matrix、per-App 音量與路由、房間校正、IR 相位、VST3 外掛
  與 Hibiki ASIO。
- **安全第一**：所有音量變更走有上限、可回復的控制面；不攔截、不綁定、不加 DRM。

### 目前的成熟度

它是公開開發中的平台：核心 DSP 與控制面已有大量可重跑的合約測試；Windows 整合
（音量聯動、per-App 控制、裝置目錄）已在 user-space 驗證到「控制面」等級；
**虛擬 driver、簽章與消費者安裝版還不存在**。

| 區域 | 已有可重跑證據 | 尚未可對外承諾 |
| --- | --- | --- |
| DSP／控制面 | C++ contract tests、RT graph、Scene、per-output volume、limiter、ISO 226-derived formula boundary、PEQ／IR 基礎 | 真實喇叭／耳機聲學效果或 ISO 認證 |
| Windows 音量 | `IAudioEndpointVolume` dB/mute bridge、safety state 與 Group Master 單次增益 contract | target 24H2 的長時間實機／服務重啟 soak |
| per-App | session catalog、暫時 handle、volume／route／route-rule command、Expert 預設與歧義 fail-closed | 所有 App 的實體 capture/re-send、Chrome 單分頁自動攔截 |
| UX | UI-independent control model、source-only WinUI shell、本機 formal XAML build／UIA smoke | target 環境完整無障礙實測與長時間 preview 驗收 |
| Driver／發行 | MS-PL SYSVAD-derived source boundary、本機 WDK `.sys` 建置＋Inf2Cat 封裝、self-signed test-sign evidence、INF source、installer/manifest policy | 可安裝／載入的 WaveRT driver、runtime audio、HLK、Microsoft 簽章、Gumroad 正式交付 |

詳細已完成項目、證據與限制看 [baseline](docs/state/BASELINE.md)。不要把 contract test 或
source gate 誤解為已完成硬體驗收。

### 三個常見誤解

1. **「我可以下載安裝了嗎？」** 不行。repository 只有原始碼與測試；沒有任何 EXE/
   MSI/driver 發佈物。正式版未來會經過 Microsoft 簽章與獨立 release pipeline。
2. **「跑 preview 會動到我的系統音量嗎？」** 預設不會。所有會寫入系統音量或開啟
   實體 sink 的功能都是明確 opt-in，且會恢復原值。
3. **「這些測試通過代表聲音會變好聽嗎？」** 測試驗證的是控制面與安全性；聲學效果
   需要實體硬體驗證，那部分還沒發生。

### 想試試看？（三分鐘路徑）

需要 Windows x64 與 PowerShell 7。以下命令只建置並啟動 user-space 預覽，所有輸出
都留在 ignored 的 `.local/`，不修改系統、不安裝任何東西：

```powershell
git clone https://github.com/xup61069/Hibiki-DSP.git
cd Hibiki-DSP
pwsh -File tools/run-preview.ps1 -Build
```

會啟動 user-space Engine Preview 加上一個桌面 UI；關閉 UI 後引擎一併停止。更多
選項（WASAPI sink、session routing、系統音量聯動）全部是明確旗標，詳見
[預覽與驗證詳情](#預覽與驗證詳情)。

---

## 邁向 V1：還差什麼？

**V1 的定義**（依 [SPEC-0005](docs/specs/SPEC-0005-source-only-paid-release.md)）：
第一個可安裝的正式版——包含 Microsoft 簽章的虛擬音訊端點、Easy/Expert 控制面、
Authenticode 簽章 installer，經 Gumroad 交付。

距離 V1 的缺口分三類，性質完全不同：

| 類型 | 內容 | 能不能靠寫程式解決？ |
| --- | --- | --- |
| **工程** | driver 的 install/load/runtime-audio 接線與實機驗證（本機已能建置並測試簽署 PortCls adapter `.sys`，但尚未安裝／出聲）；WinUI target 環境複驗；長時間 soak 工具；installer 打包整合 | ✅ 可以，已在進行 |
| **環境** | 一台鎖定規格的目標機器（Windows 11 24H2+ x64、VS 2026、SDK/WDK >= 10.0.26100 最低基線）；測試用實體音訊裝置 | ❌ 要準備機器 |
| **行政／法務** | Microsoft 硬體開發者帳號與驅動簽章流程；Authenticode 憑證；Gumroad 帳號；ISO 226 係數授權確認 | ❌ 要申請與等待 |

### 里程碑（相依順序）

| # | 里程碑 | 目前狀態 | 主要卡點 |
| --- | --- | --- | --- |
| M0 | 目標機器 toolchain 就位（`doctor.ps1` 全綠） | 本機已符合 ADR-0005 的 SDK/WDK >= 10.0.26100 最低基線；driver 的 Hyper-V VM 隔離載入測試環境建置中（Issue #462）；正式 target 機器仍待指定與複驗 | 環境：鎖定 target 機器尚未到位 |
| M1 | WinUI XAML 正式建置＋無障礙 smoke | 本機 VS2026 已完成 formal XAML build 與 UIA smoke；target 環境複驗待 M0 | 工程（小）：target 複驗＋M0 |
| M2 | 第一個可安裝的簽章 WaveRT 虛擬端點 | 本機已有第一次 kernel-mode PortCls adapter `.sys` build、Inf2Cat 封裝與 self-signed test-sign evidence；仍未安裝／載入，離 Microsoft-signed installable endpoint 還差 runtime 接線與行政流程 | **工程：install/load/runtime 接線與實機驗證** ＋ 簽章帳號（行政） |
| M3 | 引擎 → 虛擬端點實際出聲＋長時間 soak | driver 已有 local build/test-sign evidence；引擎到虛擬端點的實機音訊一次都沒跑過 | M2 ＋ 環境 |
| M4 | 簽章 installer＋Gumroad 正式交付 | ReleaseManifest 政策與 installer 來源已定義 | M3 ＋ 憑證／帳號（行政） |

### 白話評估

- **控制面與 DSP 的程式大致就緒**：今天為止已有數十個 fail-closed 合約切片與
  user-space live probe（音量聯動、per-App 控制、裝置目錄、自動化時間軸鏈）。
- **最大的單一工程缺口是 driver 的 install/load/runtime 驗證**——本機已能把通過合約測試的
  原始碼建成本地 kernel-mode `.sys`、封裝並做 self-signed test-sign；但「真的能安裝、載入、
  出聲且通過長時間 soak」還沒發生。
- **近期非工程前置是 M0 的 target 機器**；Microsoft Hardware Program／正式簽章不阻擋
  source、DSP、UI、一般 CI 或本機 test-signing，只在 consumer driver／installer release
  進入 M2/M4 驗收時成為必要行政流程。
- 所以誠實的答案是：**V1 沒有日期**。瓶頸不在「能不能編出 `.sys`」，而在實機 install/load/
  runtime audio 驗證的環境到位與簽章／發行帳號的行政流程。M0 到位後，driver 要從現有的
  build/test-sign evidence 繼續往可載入端點推進；M3/M4 再疊上實機 soak 與簽章等待期。

---

## 給 AI 協作者

**入口順序（canonical）**：先讀 [AGENTS.md](AGENTS.md)（三層規則與 scope-triggered gates），再依
[AI 接手頁](docs/AI_HANDOFF.md) 操作，多 session 並行規則見
[docs/ai/MULTI_AGENT.md](docs/ai/MULTI_AGENT.md)。它們會指向唯一的 active handoff、
目前環境限制、已驗證命令、不能碰的資料，以及 **一個**下一步。不要從聊天紀錄、舊
registry、私人裝置 ID 或未提交的 build output 推斷專案狀態。

**執行環境**：所有 `tools/*.ps1` gates 以 UTF-8（無 BOM）儲存並使用 .NET Core API，
必須用 PowerShell 7（`pwsh`）執行。Windows 內建的 PowerShell 5.1 會把中文註解解成
亂碼、缺少必要方法，直接跑必失敗。機器沒有 `pwsh` 時先安裝：
`winget install --id Microsoft.PowerShell`；本機第一次執行若被 Execution Policy
擋下，加 `-ExecutionPolicy Bypass`。

**驗證門檻（always-run + conditional）**：每個寫入切片執行最小共同檢查；其餘依範圍觸發，完整對照表見
[AGENTS.md](AGENTS.md) 第三層。

```powershell
pwsh -File tools/handoff-check.ps1 -Issue <n>
pwsh -File tools/docs-check.ps1
pwsh -File tools/source-policy.ps1
git diff --check
```

條件式 gates（依變更範圍）：需要 build/toolchain evidence 用 `doctor.ps1 -CheckOnly`；
C/C++、CMake、schema、contract 或 tests 用 `verify.ps1`；workflow/release policy 用
`source-only-ci-check.ps1`；UI 用 `control-model-check.ps1`、`build-preview.ps1` 與
`winui-shell-check.ps1`；engine 整合用
`build-engine-preview.ps1`、`engine-preview-smoke.ps1`、`control-model-check.ps1`、
`control-model-engine-smoke.ps1`；extensions 用 `extension-check.ps1`；installer/distribution
identity 用 `installer-check.ps1`、`distribution-check.ps1`；driver boundary 用
`driver-source-check.ps1`（driver 簽章屬 release 階段）；live probes 一律明確 opt-in。

需要記錄 target environment 時才執行 `pwsh -File tools/probe-environment.ps1`；輸出只留在
ignored `.local/`，純文件小改不必為了形式探測機器。

在鎖定的 Windows 11 24H2+/VS 2026/SDK-WDK 機器上，將 Compatibility Preview 改為
`pwsh -File tools/build-preview.ps1 -Target WinUI`，以取得正式 XAML build evidence；
Desktop Compatibility Preview 只能驗證本機 ViewModel／啟動 smoke，不得代替 XAML、
無障礙、driver 或發行驗收。

多個 gate 另提供 `-SelfTest`（不碰機器、不寫檔即可驗證 gate 自身邏輯，例：
`pwsh -File tools/docs-check.ps1 -SelfTest`）。寫入切片必須
Issue／非 main branch／handoff／draft PR 一對一；並行 writer、branch occupied 或狀態不明時
worktree 隔離才是硬要求，單一 writer 時仍建議使用。認領前檢查 branch 佔位；細節見
`docs/ai/MULTI_AGENT.md`。

**真值順序**：產品行為以 [Specs](docs/specs/INDEX.md) 為準；架構理由以
[ADRs](docs/adr/) 為準；main 的事實以 [baseline](docs/state/BASELINE.md) 和
[evidence](evidence/0000-foundation/) 為準。

---

## 公開 repository

[github.com/xup61069/Hibiki-DSP](https://github.com/xup61069/Hibiki-DSP) 是唯一官方公開 source
入口。可用下列方式在新電腦取得同一份交接資料：

```powershell
git clone https://github.com/xup61069/Hibiki-DSP.git
```

---

## 預覽與驗證詳情

### 工具鏈需求

目標環境是 **Windows 11 24H2+ x64、Visual Studio 2026、Windows SDK/WDK >= 10.0.26100（ADR-0005 最低基線）**。
本機若低於此版本，仍可跑 user-space contract tests，但不能宣稱 driver 或正式 preview 已驗證。

本節的完整必跑命令清單以上方「給 AI 協作者」的單一真值為準，不在這裡重複維護。

驗證輸出一律留在 ignored 的 `.local/`；本 GitHub repository 不上傳 EXE、DLL、SYS、MSI、
MSIX、VST3 或 CI artifact。多個 gate 提供 `-SelfTest` 模式（不碰機器、不寫檔），CI 的
`Gate self-test sweep` step 會探索並逐一執行它們。

### 目前的 preview 狀態

目前可重跑的是 unsigned user-space preview baseline（`tools/verify.ps1` 與 C# control-model
check），不是可安裝產品。正式 WinUI shell 需要 target toolchain 的 XAML build；它即使成功也不含
虛擬 driver、系統攔截或正式簽章，絕不會上傳 GitHub。

```powershell
pwsh -File tools/build-preview.ps1 -Target WinUI
```

在非 target 機器若 XAML compiler 或 Windows App Runtime 不能使用，可建立自帶 .NET runtime 的
Desktop Compatibility Preview：

```powershell
pwsh -File tools/build-preview.ps1 -Target DesktopCompat
```

它以同一個 `EasyControlViewModel` 展示連線、場景選擇、一鍵改善、輸出群組、路由健康與安全音量的控制面，不需要
Windows App Runtime；但不含 XAML 正式 UI、driver、系統攔截或 accessibility evidence。所有輸出都在
`.local/preview/`，不可加入 Git。
Engine Preview 連線後也會在 user-space 以 `IMMDeviceEnumerator` 枚舉本機 render/capture metadata，
讓預覽顯示裝置數量與預設輸出 metadata；預設只提供 bounded catalog snapshot，不開啟 physical
WASAPI sink、不切換實體裝置，也不修改 Windows 系統音量。Desktop Compatibility Preview 只顯示
render/capture 數量與預設輸出 metadata；正式 WinUI shell 才提供實體輸出選擇器。
預覽也會顯示 IR 相位 policy 的 Game／Balanced／Movie／Bypass 與預估延遲。C++ control-plane 已能將
bounded WAV kernel 轉成 minimum／mixed／linear phase，並透過 `AudioEngineModel` 的
prepare→commit／rollback attachment 在 user-space preview 的固定 graph render 中實際執行 IR；
Desktop Compatibility Preview 可選擇 IR WAV 並取得 prepare/commit Ack。這仍不是 loadable driver、
實體 WASAPI sink 或可宣稱的聲學校正證據。
加入 `-SmokeTest` 可同時做 3 秒的無視窗啟動檢查；它不會連接音訊引擎。只想重跑控制面可用
`-Target ControlModel`。

### 啟動完整本機預覽

要實際開啟本機預覽（同時啟動 user-space Engine Preview 與桌面 UI），使用：

```powershell
pwsh -File tools/run-preview.ps1 -Build
```

啟動器預設使用 `-Ui Auto`：若已安裝符合版本的 Windows App Runtime 1.7，就開啟
WinUICompat；否則自動退回不需要 Runtime 的 DesktopCompat，不會再讓使用者直接撞到缺少
Runtime 的錯誤。要固定指定介面可使用 `-Ui WinUICompat`、`-Ui FormalWinUI`（formal
MSBuild/XAML shell，需先以 `tools/build-preview.ps1 -Target WinUI` 建置或加 `-Build`）或
`-Ui DesktopCompat`。

Windows 使用者也可以直接雙擊 repository 根目錄的 `Start-HibikiPreview.cmd`；它會執行同一個
流程，先建置必要的 unsigned preview，再啟動 Engine Preview 與可用的桌面 UI。
若要明確啟用 shared-mode WASAPI sink，可雙擊同一層的
`Start-HibikiPreview-Wasapi.cmd`；它等同於加入 `-EnableWasapiOutput`，不會安裝 driver 或
改變正式系統路徑。
若要啟動正式 XAML 殼（需 Windows App Runtime 1.7 x64），可雙擊同一層的
`Start-HibikiPreview-FormalWinUI.cmd`；它等同於加入 `-Build -Ui FormalWinUI -SmokeTest`。

DesktopCompat 視窗開啟後會自動嘗試連接本機引擎；WinUICompat 則按「連接預覽引擎」。若沒有
連線，兩者都會安全顯示「尚未連接」，不會改動 Windows 音量或任何實體裝置。要固定使用
自帶 .NET runtime 的 DesktopCompat，可執行
`pwsh -File tools/run-preview.ps1 -Build -Ui DesktopCompat`；直接雙擊
`.local/preview/DesktopCompat/Hibiki.DesktopPreview.exe` 只會開 UI，不會自動啟動引擎。

### Windows 系統音量聯動（opt-in）

若要明確測試 Windows 系統音量聯動，才使用：

```powershell
pwsh -File tools/run-preview.ps1 -Build -EnableSystemVolume
```

這個選項會讀取目前 Windows render endpoint 音量、監聽外部音量鍵，並把 Preview 的音量調整
寫回同一 endpoint；預設預覽不會寫入系統音量。若要做明確的端到端 write-through 測試，可執行：

```powershell
pwsh -File tools/live-system-volume-check.ps1 -WriteTest
```

它會啟動 Engine Preview，送出 IPC 音量命令，讀回 Windows endpoint 並在結束前恢復原值；
這會短暫改變本機音量，只能視為 user-space evidence。只想驗證 broker 是否可用而不送出音量命令，
可執行 `pwsh -File tools/engine-preview-smoke.ps1 -EnableSystemVolume -StatusOnly`。

### shared-mode WASAPI sink（opt-in）

若要明確啟動現有的 shared-mode WASAPI sink，使用：

```powershell
pwsh -File tools/run-preview.ps1 -Build -EnableWasapiOutput
pwsh -File tools/engine-preview-smoke.ps1 -EnableWasapiOutput -StatusOnly
```

這個旗標只會在 physical catalog 找到支援的 active default render endpoint（2／5.1／7.1、
44.1／48／96／192 kHz）後啟動 dedicated sink worker；worker 未回報 Ready 前，主輸出保持
Pending／Degraded 並 fail-closed。它驗證的是 user-space WASAPI output boundary，不等於
WaveRT driver、per-App capture/re-send、ASIO physical delivery 或完整音訊播放；正常預覽仍不
會啟動 sink。裝置切換的 handoff/crossfade 核心已在 `AudioEngineModel`，Engine Preview 目前
只綁定明確 opt-in 的預設端點，避免把未驗證的實體交付誤報成完成。

若要驗證 shared-mode sink 的實際 handoff，可執行額外的 opt-in live check：

```powershell
pwsh -File tools/live-wasapi-handoff-check.ps1
```

它只送靜音 block，輸出 mix format 與 aggregate worker counters；沒有可用 endpoint 時會如實
記錄 `wasapi=unavailable`。這是 user-space handoff 邊界證據，不能當成已完成的 WaveRT driver、
HLK 或 Microsoft signing 驗收。

### Per-App session routing（opt-in）

若要在 Expert 預覽中查看 Windows App／工作階段清單與測試 per-App 控制，必須明確啟用 session
routing：

```powershell
pwsh -File tools/run-preview.ps1 -Build -EnableSessionRouting
```

這會以 `IAudioSessionManager2` 枚舉目前 default render endpoint 的 bounded 工作階段 metadata，
並由 COM worker 透過固定 queue 接收 App 音量、lane/output 與 route-rule 命令。UI 只顯示安全的
暫時 handle／摘要，不保存 PID、session instance 或 endpoint ID；每個命令都必須由使用者明確送出，
且目前只驗證控制面與 Windows session volume 邊界，實體 per-App capture/re-send 與 DSP delivery
仍標示為 unverified。可用下列命令重跑：

```powershell
pwsh -File tools/engine-preview-smoke.ps1 -EnableSessionRouting -StatusOnly
pwsh -File tools/control-model-engine-smoke.ps1 -EnableSessionRouting
```

若要驗證單一 App/session 音量的真實 Windows 讀回與恢復，可使用明確寫入旗標：

```powershell
pwsh -File tools/live-session-volume-check.ps1 -WriteTest
```

探針會自動啟動 Engine Preview，只建立自己的無聲 shared-mode session，透過 IPC/control
queue/COM worker 使用 bounded catalog handle 暫時衰減約 3 dB、讀回並在結束前恢復原始 dB/mute；
不輸出 session/endpoint identity，也不代表實體 per-App capture/re-send 或 DSP delivery。結果記錄於
`evidence/0000-foundation/session-volume-live-v1.json`。
同一個 probe 也會送出一個暫時的 `SessionRouteCommand`，確認 route catalog 回報 `Ready`；這是
控制面 graph transaction 證據，不代表實體音訊已完成 per-App 重送。

另有獨立的 App/session enumeration 探針，只讀取目前的工作階段清單：

```powershell
pwsh -File tools/live-audio-session-check.ps1
```

它只輸出 session／active 數量與固定 identity 語意，不輸出 PID、session ID、endpoint ID 或顯示
名稱；這代表 enumeration boundary 可重跑，不代表每個 App 已完成實際 Lane routing 或 DSP
delivery。

### 裝置目錄與 process-loopback 探針（opt-in）

另有兩個不會修改系統狀態的 live probe，只輸出匿名彙整，不列印真實 endpoint／session identity：

```powershell
pwsh -File tools/live-device-catalog-check.ps1
pwsh -File tools/live-process-loopback-check.ps1
```

`live-device-catalog-check` 會枚舉本機 render/capture 數量、sequence 與 payload 大小等 wire 結果，
暫存輸出只留在 ignored 的 `.local/`；它驗證的是 worker-owned enumeration boundary，不代表已完成
的 driver／WaveRT／HLK 或 Microsoft signing 驗收。
`live-process-loopback-check` 會回報匿名格式與 frame aggregate；若本機 Audio Service 不提供
process-loopback，會如實記錄 `loopback=unavailable`，此時仍只是 source compile evidence，不能
當成 Chrome 單分頁 tabCapture、實體 per-App routing 或 signed driver 證據。

### WinUI 相容殼

`.local/preview/WinUICompat/Hibiki.WinUI.exe` 是需要 Windows App Runtime 1.7 的實驗性 WinUI
相容預覽，不是正式 XAML/accessibility evidence。建置並做啟動 smoke：

```powershell
pwsh -File tools/build-preview.ps1 -Target WinUICompat -SmokeTest
```

若缺少 Runtime，會出現「Required components of the Windows App Runtime are missing」；請安裝
Windows App Runtime 1.7 x64（可從 [Microsoft 官方歷史下載頁](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads-archive)
選擇 Installer (x64)），或改用不需要 Runtime 的 Desktop Compatibility Preview。這個錯誤不代表
Desktop Compatibility Preview 損壞。

### C# 控制模型往返

要只驗證 C# control model 與 C++ Engine Preview 的實際命令往返，可執行：

```powershell
pwsh -File tools/control-model-engine-smoke.ps1
```

它會送出 −18 dB 音量、讀回引擎快照，再送出 Game One-Tap SceneApply；這是控制面驗證，仍不
代表已經有實體音訊輸出。

---

## 來源公開與正式版本

所有 Hibiki 自有 source 都會公開；driver 與 user-space 依 component license 分隔。公開 GitHub
只放 source、文字 manifest、SBOM 與 evidence。未來正式版必須由隔離 release pipeline 產生
Microsoft-signed virtual-audio driver 與 Authenticode installer，並透過 Gumroad 交付；執行期
不使用 activation、裝置綁定或 DRM。詳見 [source policy](SOURCE_POLICY.md) 與
[release spec](docs/specs/SPEC-0005-source-only-paid-release.md)。

## 授權與貢獻

Hibiki user-space 為 GPL-3.0-only；SYSVAD-derived driver 為 MS-PL；SDK/schema 為 Apache-2.0；
文件為 CC-BY-4.0。見 [LICENSES/README.md](LICENSES/README.md)、[THIRD_PARTY.yml](THIRD_PARTY.yml)、
[CONTRIBUTING.md](CONTRIBUTING.md) 和 [TRADEMARKS.md](TRADEMARKS.md)。禁止提交私密校正資料、
endpoint/serial identity、簽章金鑰、顧客資料、ISO 受限內容或任何編譯產物。
