# Hibiki DSP 目標機器檢查清單

這份清單對應 V1 M0（toolchain 就位）。全部打勾後才能把 M1（WinUI XAML 正式建置）與 M2（PortCls/driver）的驗收證據當成正式 target evidence。

## 需求總覽

| 項目 | 最低要求 | 檢查方式 |
| --- | --- | --- |
| OS | Windows 11 24H2+ x64（build 26100 以上） | `winver` 或 `[Environment]::OSVersion` |
| 架構 | x64 | `[Environment]::Is64BitOperatingSystem -eq $true` |
| 磁碟 | 80 GB 以上可用空間 | `Get-PSDrive C` |
| PowerShell | 7.x | `pwsh -Command '$PSVersionTable.PSVersion.ToString()'` |
| Visual Studio | 2026（major 18）含 C++ workload | vswhere 檢查 |
| Windows SDK/WDK | 10.0.26100 家族（QFE 不限） | doctor.ps1 |

## Windows SDK/WDK 安裝

從 [Microsoft 官方下載頁](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)安裝對應 Windows 11 的 SDK 與 WDK。最低版本為 **10.0.26100**；同家族內的 QFE 更新（例如 10.0.26100.6584 或更新）皆可接受。

安裝後確認以下路徑存在（版本號以實際安裝為準）：

```powershell
Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory | Select-Object Name
```

至少要有一個 `10.0.26100.x` 目錄。

## 驗證

```powershell
pwsh -File tools/doctor.ps1 -CheckOnly
```

所有檢查必須 OK。若 SDK/WDK 顯示 MISSING，依 Detail 訊息排查：

- 找不到 Include/build/Tools：SDK 或 WDK 未裝齊，重新執行 installer。
- 缺套件 metadata：只裝了其中一個（SDK 或 WDK），兩者都要裝。

## 後續步驟

doctor 全綠後，依序跑：

1. `pwsh -File tools/verify.ps1`（contract suite）
2. `pwsh -File tools/build-preview.ps1 -Target WinUI`（正式 XAML build）
3. PortCls/driver 建置與實機驗證（M2）
