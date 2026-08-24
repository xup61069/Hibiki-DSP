# Hibiki DSP

> 給 Windows 11 的 Audio Scene OS：用一套可驗證的控制面管理系統音訊、遊戲、影音、DAW 與多聲道輸出。

Hibiki DSP 是一個公開開發中的 Windows 音訊平台。產品目標是讓一般使用者用 Easy
模式快速選擇場景，也讓進階使用者在 Expert 模式管理輸出群組、per-App 路由、Matrix、
校正、IR、VST3 與 Hibiki ASIO。

> **目前狀態：source-only developer preview。** 這不是可供一般使用者安裝的成品；
> GitHub 不提供 EXE、DLL、SYS、MSI、MSIX、VST3 或其他編譯產物。現階段可重跑的是
> 原始碼、控制面預覽、測試與匿名證據，不是正式音訊產品的完整驗收。

**English:** Hibiki DSP is an open-source Windows audio platform in development. It aims
to combine one-tap audio scenes with expert routing, calibration and plugin workflows.
Today it is a source-only developer preview, not a consumer installer. Documentation is
primarily in Traditional Chinese.

## 為什麼做 Hibiki？

Windows 上的遊戲、瀏覽器、影音播放器與 DAW 各自管理音量、裝置和外掛，跨應用程式的
操作很容易變成一堆互不相通的設定。Hibiki 想把它們收斂成同一個控制面：

- **Easy**：選擇遊戲、電影、音樂等 Scene，用明確的命令、Ack 與安全音量狀態完成操作。
- **Expert**：管理輸出群組、Lane／Matrix、per-App 音量與路由規則、PEQ／IR 校正、
  VST3 自動化與 ASIO 邊界。
- **可驗證**：產品行為寫進 Spec，架構決策寫進 ADR；實作以 fail-closed 合約、測試與
  evidence 說明「已證明什麼」和「還沒證明什麼」。

Hibiki 不攔截廠商 ASIO、WASAPI Exclusive 或 RAW 路徑，也不使用 activation、裝置綁定
或 DRM。

## 今天可以做什麼？

目前最完整的體驗是 unsigned、driver-free 的 user-space preview。它會啟動本機 Engine
Preview 與桌面控制介面，可查看 Easy／Expert 控制流程、Scene、輸出群組、路由健康、
安全音量、裝置摘要與 IR phase policy。

| 層級 | 已有可重跑證據 | 仍未證明／未交付 |
| --- | --- | --- |
| DSP／控制面 | C++20 RT graph、Scene transaction、輸出群組音量、Matrix／路由、limiter、PEQ／IR 基礎與多項 fail-closed contract tests | 真實喇叭／耳機的聲學效果、校正品質或第三方標準認證 |
| Windows user-space | Engine Preview、裝置／session catalog、系統與 session 音量 write-through probe、route transaction、shared-mode WASAPI 邊界 | 實體 per-App capture/re-send、完整 DSP delivery 或長時間實機播放 |
| UX | UI-independent C# control model、Desktop／WinUI compatibility preview、本機 formal XAML build 與 UIA smoke | 鎖定 target 機器上的完整 accessibility 與長時間驗收 |
| Driver | WDK source build、Inf2Cat、self-signed test package；隔離 Hyper-V guest 已完成 test-signed 安裝、PnP start 與穩定重啟（ProblemCode 0） | 實體音訊播放、WaveRT streaming 行為、HLK、Microsoft 簽章 |
| 發行 | source-only policy、ReleaseManifest schema、installer／rollback 來源與 gates | 正式 signed installer、consumer upgrade／rollback 驗收與 Gumroad 交付 |

完整的 main 狀態、來源 commit 與限制以 [baseline](docs/state/BASELINE.md) 為準；子系統位置與
契約看 [project map](docs/PROJECT_MAP.md)。Contract test、source gate、preview 或 live probe
都不能單獨當成實體音訊、driver、HLK、簽章或 release evidence。

## 安全試跑

需要 Windows x64、Git 與 PowerShell 7（`pwsh`）。建置工具不足時，腳本會直接回報缺少的
dependency；所有輸出都留在 ignored 的 `.local/`。

```powershell
git clone https://github.com/xup61069/Hibiki-DSP.git
cd Hibiki-DSP
pwsh -NoProfile -File tools/run-preview.ps1 -Build
```

也可以在 repository 根目錄雙擊 `Start-HibikiPreview.cmd`。預設流程：

- 不安裝 driver 或服務；
- 不寫入 Windows 系統音量；
- 不開啟實體 WASAPI sink；
- 自動選擇可用的 WinUICompat，否則退回自帶 runtime 的 DesktopCompat；
- 關閉 UI 時一併停止 Engine Preview。

若只想驗證程式與合約，不開 UI：

```powershell
pwsh -NoProfile -File tools/verify.ps1
pwsh -NoProfile -File tools/control-model-engine-smoke.ps1
```

第二個命令會跨程序送出控制命令並讀回 Ack／狀態；它仍然只是 user-space 控制證據。

## 進階 preview 與 probe

下列功能全部是明確 opt-in。請先看命令的影響；帶 `-WriteTest` 的 probe 會短暫修改本機
狀態，再於結束前恢復。

| 命令 | 會做什麼 | 證據邊界 |
| --- | --- | --- |
| `tools/run-preview.ps1 -Build -EnableSystemVolume` | 讓 preview 讀寫目前的 Windows render endpoint 音量 | 可能改變系統音量；不是 driver evidence |
| `tools/run-preview.ps1 -Build -EnableWasapiOutput` | 開啟現有的 shared-mode WASAPI sink worker | 不是 WaveRT、per-App 重送或完整播放驗收 |
| `tools/run-preview.ps1 -Build -EnableSessionRouting` | 枚舉目前 endpoint 的 App sessions，啟用 volume／route 控制面 | 不代表已完成實體 per-App capture 與 DSP delivery |
| `tools/live-system-volume-check.ps1 -WriteTest` | 經 IPC 短暫調整 endpoint 約 3 dB、讀回並恢復 | user-space volume write-through evidence |
| `tools/live-session-volume-check.ps1 -WriteTest` | 建立無聲測試 session，調整音量、讀回並恢復，同時驗證 route command | user-space session／route evidence |
| `tools/live-wasapi-handoff-check.ps1` | 對 sink handoff 送出靜音 block，回報匿名 counters | 不證明可聽輸出或 driver streaming |
| `tools/live-audio-session-check.ps1`、`tools/live-device-catalog-check.ps1` | 匿名枚舉 session／裝置摘要 | 不輸出私人 endpoint、session 或裝置 identity |
| `tools/live-process-loopback-check.ps1` | 探測本機 process-loopback 能力與匿名 aggregate | 不等於 Chrome 單分頁捕捉或完整 per-App routing |

若要固定 UI，可在 `run-preview.ps1` 加上 `-Ui DesktopCompat`、`-Ui WinUICompat` 或
`-Ui FormalWinUI`。正式 XAML shell 的建置入口是：

```powershell
pwsh -NoProfile -File tools/build-preview.ps1 -Target WinUI
```

目標產品基線是 Windows 11 24H2+ x64、Visual Studio 2026 與 Windows SDK／WDK
`>= 10.0.26100`。較低或不同的環境可能仍能跑部分 user-space tests，但不能據此宣稱
正式 WinUI、driver 或 release 已驗證。

## 距離 V1 還有什麼？

[SPEC-0005](docs/specs/SPEC-0005-source-only-paid-release.md) 定義的 V1 是正式可安裝版本：
Microsoft-signed 虛擬音訊 driver、Easy／Expert 控制面、Authenticode-signed installer，
經隔離 release pipeline 驗證後由 Gumroad 交付。

目前仍需要：

1. 鎖定 Windows 11 24H2+ target 機器與實體音訊測試環境。
2. 完成引擎到 WaveRT 虛擬端點的實際 streaming、可聽輸出與長時間 soak。
3. 在 target 環境複驗正式 WinUI、accessibility、安裝、升級、rollback 與 uninstall。
4. 完成 HLK／WHCP、Microsoft driver signing、Authenticode 與 release custody。
5. 完成 ISO 226 相關授權確認與正式交付所需帳號／行政流程。

因此目前沒有可信的 V1 日期。Test-signed driver 在隔離 guest 能安裝、PnP start 並穩定重啟，
是重要的 driver 啟動證據，但不是實體播放、HLK、Microsoft signing 或可發行產品。

## 專案真值與導覽

| 想知道什麼 | 請看 |
| --- | --- |
| 不可違反的規則、流程預設、驗證門檻 | [AGENTS.md](AGENTS.md) |
| 新環境／新 AI 的 canonical 入口 | [docs/START_HERE.md](docs/START_HERE.md) |
| main 已合併、可重跑的能力與限制 | [docs/state/BASELINE.md](docs/state/BASELINE.md) |
| 子系統位置與目前契約 | [docs/PROJECT_MAP.md](docs/PROJECT_MAP.md) |
| 產品行為與架構理由 | [Specs](docs/specs/INDEX.md)／[ADRs](docs/adr/) |
| 匿名驗證紀錄 | [evidence/0000-foundation](evidence/0000-foundation/) |
| 多 AI／多 worktree 協作 | [docs/ai/MULTI_AGENT.md](docs/ai/MULTI_AGENT.md) |
| 貢獻方式 | [CONTRIBUTING.md](CONTRIBUTING.md) |

Repository 內容的權威順序、active Issue handoff 與衝突處理以
[START_HERE](docs/START_HERE.md) 為準，不要用聊天紀錄或未提交的 build output 推斷專案狀態。

## 給開發者與 AI 協作者

開始寫入前，先讀 [AGENTS.md](AGENTS.md) 與 [START_HERE](docs/START_HERE.md)，取得
maintainer／orchestrator 指派，並確認專用 Issue、handoff block、非 `main` branch 與
`scope_globs`。有其他 writer、branch occupancy 或狀態不確定時，必須使用隔離 worktree。

每個寫入切片至少執行：

```powershell
pwsh -NoProfile -File tools/handoff-check.ps1 -Issue <n>
pwsh -NoProfile -File tools/docs-check.ps1
pwsh -NoProfile -File tools/source-policy.ps1
git diff --check
```

C/C++、UI、engine、driver、installer、extension、workflow 或 release policy 變更各有額外
條件式 gates；完整對照表只以 [AGENTS.md](AGENTS.md) 第三層為準。需要 target-machine
evidence 時才執行環境 probe；私人路徑與裝置資料只留在 `.local/`。

## Source、發行與授權

[github.com/xup61069/Hibiki-DSP](https://github.com/xup61069/Hibiki-DSP) 是唯一官方公開
source 入口。GitHub 只存放 source、依賴鎖定、建置腳本、文字 manifest、SBOM 與 evidence；
正式 signed payload 未來由隔離 release pipeline 產生與保管。詳見
[SOURCE_POLICY.md](SOURCE_POLICY.md) 與 [SPEC-0005](docs/specs/SPEC-0005-source-only-paid-release.md)。

- user-space：GPL-3.0-only
- SYSVAD-derived driver：MS-PL
- SDK／schema：Apache-2.0
- 文件：CC-BY-4.0

完整授權與第三方資訊見 [LICENSES/README.md](LICENSES/README.md) 與
[THIRD_PARTY.yml](THIRD_PARTY.yml)；商標政策見 [TRADEMARKS.md](TRADEMARKS.md)。

禁止提交簽章金鑰、憑證、顧客資料、真實 endpoint／serial identity、私人校正檔、ISO 受限
內容或任何編譯產物。
