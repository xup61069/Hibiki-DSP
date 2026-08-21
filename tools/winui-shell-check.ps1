[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$shell = Join-Path $repo 'apps/winui-shell'
$required = @('Hibiki.WinUI.csproj', 'App.xaml', 'App.xaml.cs', 'MainWindow.xaml', 'MainWindow.xaml.cs')
$missing = @($required | Where-Object { -not (Test-Path (Join-Path $shell $_)) })
if ($missing.Count -gt 0) { throw "WinUI shell files missing: $($missing -join ', ')" }

$project = Get-Content (Join-Path $shell 'Hibiki.WinUI.csproj') -Raw
$lock = Get-Content (Join-Path $repo 'build/toolchain-lock.yml') -Raw
if (-not $project.Contains('<UseWinUI>true</UseWinUI>') -or
    -not $project.Contains('<WindowsPackageType>None</WindowsPackageType>') -or
    -not $project.Contains('net8.0-windows10.0.26100.0') -or
    -not $project.Contains('<Platforms>x64</Platforms>')) {
  throw 'WinUI project must target WinUI/x64/Windows 11 build 26100.'
}
$projectVersion = [regex]::Match($project, 'WindowsAppSDKVersion[^>]*>([^<]+)<').Groups[1].Value
$lockVersion = [regex]::Match($lock, 'app_sdk:\s*"([^"]+)"').Groups[1].Value
if ([string]::IsNullOrWhiteSpace($projectVersion) -or $projectVersion -ne $lockVersion) {
  throw "WinUI App SDK version $projectVersion does not match toolchain lock $lockVersion."
}

$xaml = Get-Content (Join-Path $shell 'MainWindow.xaml') -Raw
$codeBehind = Get-Content (Join-Path $shell 'MainWindow.xaml.cs') -Raw
foreach ($requiredText in @('x:Name="RootGrid"', 'ItemsSource="{Binding Scenes}"',
    'ItemsSource="{Binding OutputGroups}"', 'SelectedOutputGroup', 'IsExpert',
    'ItemsSource="{Binding PhysicalDevices}"', 'SelectedPhysicalDeviceId',
    'DeviceSwitchStatusText', 'OnSwitchDeviceClick',
    'RequestedVolumeDb', 'Muted', 'IrAddedDelayMs', 'x:Name="ExpertPanel"',
    'EffectiveVolumeDb', 'SafetyStatusText', 'VolumeOriginText', 'VolumeActuatorText',
    'ItemsSource="{Binding Expert.RouteHealth}"', 'Expert.RouteSummary',
    'ItemsSource="{Binding Expert.MatrixRoutes}"', 'ItemsSource="{Binding Expert.DspGraph}"',
    'ItemsSource="{Binding Expert.Vst3Lanes}"', 'Expert.Calibration.Mode',
    'AutomationProperties.Name="Hibiki DSP 音訊場景控制"',
    'AutomationProperties.Name="一鍵改善聲音"',
    'AutomationProperties.Name="系統音量"',
    'AutomationProperties.Name="Expert 詳細模式"',
    'AutomationProperties.LiveSetting="Polite"', 'CustomSceneId', 'CustomSceneName',
    'CustomSceneDescription', 'OnAddCustomSceneClick')) {
  if (-not $xaml.Contains($requiredText)) { throw "WinUI shell binding missing: $requiredText" }
}
if (-not $codeBehind.Contains('RootGrid.DataContext = ViewModel') -or
    -not $codeBehind.Contains('ViewModel.ConnectAsync') -or
    -not $codeBehind.Contains('ViewModel.DisconnectAsync')) {
  throw 'WinUI shell must bind the control model and close its pipe client.'
}

$forbidden = @('AudioEngine', 'IAudioClient', 'Wasapi', 'CreateThread', 'NamedPipeServer')
foreach ($token in $forbidden) {
  if ($xaml.Contains($token) -or $codeBehind.Contains($token)) {
    throw "WinUI shell must not own audio/driver work: $token"
  }
}

Write-Output "WinUI source shell checks passed (Windows App SDK $lockVersion)."
